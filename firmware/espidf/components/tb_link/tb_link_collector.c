#include "tb_link_collector.h"
#include "tb_link_journal.h"
#include "tb_link_telemetry_codec.h"
#include "tb_gps_outbox.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/task.h"
#include "nvs.h"

#define LEGACY_JOURNAL "/gps/telemetry.log"
#define JOURNAL_DIRECTORY "/gps/tblite"
#define LEGACY_BUDGET (256u * 1024u)
#define FAT_RESERVE (384u * 1024u)
#define POST_BYTES 8192u
#define STAGED_RECORDS 4u
#define META_KEY "publisher1"
#define META_FORMAT UINT32_C(0x54424c31)

typedef struct {
    uint32_t version;
    char core_uid[33], cloud_uid[65], endpoint[257];
    uint64_t reserved_end;
    uint64_t reference_after_ms;
    tb_lite_reference_t reference;
} publisher_t;

typedef struct {
    const char *phase, *disconnect_reason, *poll_error, *cloud_stage;
    uint32_t received, rejected, discarded, durable, delivered, invalid_ack;
    uint32_t core_boot, frames, guard, core_heap, writes_failed, staged;
    uint32_t capacity, dropped_operational, dropped_health, replayed, recovered_tail;
    uint32_t disconnects, failures;
    uint64_t last_rx_ms, last_ack, pending, bytes, total_bytes, free_bytes, oldest_ms;
    uint64_t parked_sample_ms, reference_ms, last_success_ms, last_success_tick, last_attempt_ms, sequence_reserved_end;
    int storage_error, http_status;
    esp_err_t uplink_error;
    bool core_online, ready, core_reset_valid;
    uint32_t core_reset;
    tb_lite_fuel_window_t fuel_window;
    uint64_t fuel_window_time;
} collector_state_t;

typedef struct {
    tb_lite_snapshot_t snapshot;
    tb_lite_link_health_t link;
    uint64_t sequence, captured_ms;
    tb_lite_event_t event;
    bool health, online;
} staged_record_t;

typedef struct {
    tb_lite_ticket_t ticket[TB_LITE_BATCH_MAX];
    size_t count, length;
    unsigned health;
    bool legacy;
} upload_batch_t;

static tb_lite_journal_t s_journal;
static tb_gps_outbox_t s_legacy;
static SemaphoreHandle_t s_filesystem;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static collector_state_t s_state;
static int s_mount_status; /* 0 pending, 1 mounted, <0 error. */
static publisher_t s_meta;
static nvs_handle_t s_nvs;
static bool s_nvs_open, s_meta_valid;
static uint64_t s_next_sequence, s_legacy_oldest;
static staged_record_t s_stage[STAGED_RECORDS];
static unsigned s_stage_head, s_stage_count;
static char s_stage_json[TB_LITE_RECORD_MAX];
static uint32_t s_lost_operational, s_lost_health;

static uint64_t ticks_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static uint64_t epoch_ms(void)
{
    struct timeval now;
    if (gettimeofday(&now,NULL) != 0 || now.tv_sec < 1704067200) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_usec / 1000;
}

static collector_state_t state_snapshot(void)
{
    portENTER_CRITICAL(&s_mux);
    collector_state_t state = s_state;
    portEXIT_CRITICAL(&s_mux);
    return state;
}

static void storage_state(const char *phase, int error)
{
    uint64_t legacy_pending = tb_gps_outbox_pending(&s_legacy);
    uint64_t oldest = tb_lite_journal_oldest(&s_journal);
    if (legacy_pending && (!s_journal.pending || !s_legacy_oldest || (oldest && s_legacy_oldest < oldest)))
        oldest = s_legacy_oldest;
    portENTER_CRITICAL(&s_mux);
    s_state.phase = phase;
    s_state.storage_error = error;
    s_state.ready = s_journal.ready && s_meta_valid && s_legacy.ready;
    s_state.pending = s_journal.pending + legacy_pending;
    s_state.bytes = s_journal.bytes + s_legacy.file_bytes;
    s_state.capacity = s_journal.capacity;
    s_state.oldest_ms = oldest;
    if (s_journal.acknowledged > s_state.last_ack) s_state.last_ack = s_journal.acknowledged;
    s_state.reference_ms = s_meta.reference.started_ms;
    s_state.sequence_reserved_end = s_meta.reserved_end;
    s_state.recovered_tail = s_journal.recovered_tail_bytes + (uint32_t)s_legacy.recovered_tail_bytes;
    portEXIT_CRITICAL(&s_mux);
}

void tb_link_collector_storage(esp_err_t result)
{
    uint64_t total = 0, available = 0;
    if (result == ESP_OK) (void)esp_vfs_fat_info("/gps", &total, &available);
    portENTER_CRITICAL(&s_mux);
    s_mount_status = result == ESP_OK ? 1 : -1;
    s_state.total_bytes = total;
    s_state.free_bytes = available;
    if (result != ESP_OK) {
        s_state.phase = "filesystem_unavailable";
        s_state.storage_error = result;
    }
    portEXIT_CRITICAL(&s_mux);
}

static bool correct_owner(const publisher_t *meta, const tb_config_t *config)
{
    return meta->version == META_FORMAT && meta->reserved_end < TB_LITE_SAFE_SEQ &&
           (!meta->reference_after_ms || (meta->reference_after_ms >= TB_LITE_EPOCH_MS &&
            meta->reference_after_ms <= TB_LITE_SAFE_SEQ)) &&
           memchr(meta->core_uid, 0, sizeof(meta->core_uid)) &&
           memchr(meta->cloud_uid, 0, sizeof(meta->cloud_uid)) &&
           memchr(meta->endpoint, 0, sizeof(meta->endpoint)) &&
           !strcmp(meta->core_uid, config->telemetry_core_uid) &&
           !strcmp(meta->cloud_uid, config->cloud_device_uid) &&
           !strcmp(meta->endpoint, config->uplink_url) && tb_lite_reference_valid(&meta->reference);
}

static esp_err_t save_publisher(const publisher_t *meta)
{
    esp_err_t err = nvs_set_blob(s_nvs, META_KEY, meta, sizeof(*meta));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err == ESP_OK) s_meta = *meta;
    return err;
}

static bool absent(const char *path)
{
    struct stat information;
    return stat(path, &information) != 0 && errno == ENOENT;
}

static bool legacy_exists(void)
{
    return !absent(LEGACY_JOURNAL) || !absent(LEGACY_JOURNAL ".bak") || !absent(LEGACY_JOURNAL ".tmp");
}

static esp_err_t prepare_publisher(const tb_config_t *config)
{
    if (s_nvs_open) {
        nvs_close(s_nvs);
        s_nvs_open = false;
    }
    esp_err_t err = nvs_open("tb_link_lite", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) return err;
    s_nvs_open = true;
    publisher_t meta = {0};
    size_t size = sizeof(meta);
    err = nvs_get_blob(s_nvs, META_KEY, &meta, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* A missing counter beside an existing queue is not permission to reuse IDs. */
        if (legacy_exists() || !absent(JOURNAL_DIRECTORY)) return ESP_ERR_INVALID_STATE;
        meta.version = META_FORMAT;
        strcpy(meta.core_uid, config->telemetry_core_uid);
        strcpy(meta.cloud_uid, config->cloud_device_uid);
        strcpy(meta.endpoint, config->uplink_url);
        err = save_publisher(&meta);
        if (err != ESP_OK) return err;
    } else {
        if (err != ESP_OK) return err;
        if (size != sizeof(meta) || !correct_owner(&meta, config)) return ESP_ERR_INVALID_STATE;
        s_meta = meta;
    }
    s_next_sequence = 0;
    s_meta_valid = true;
    return ESP_OK;
}

static esp_err_t allocate_sequence(uint64_t *sequence)
{
    if (!s_meta_valid) return ESP_ERR_INVALID_STATE;
    if (!s_next_sequence || s_next_sequence > s_meta.reserved_end) {
        publisher_t next = s_meta;
        uint64_t first;
        if (!tb_lite_sequence_range(tb_config()->sequence_floor, next.reserved_end, &first, &next.reserved_end))
            return ESP_ERR_INVALID_SIZE;
        esp_err_t error = save_publisher(&next);
        if (error != ESP_OK) {
            s_meta_valid = false;
            return error;
        }
        s_next_sequence = first;
    }
    *sequence = s_next_sequence++;
    return ESP_OK;
}

typedef struct { char body[TB_LITE_SNAPSHOT_MAX]; size_t used; bool overflow; } reply_t;

static esp_err_t receive_snapshot(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    reply_t *reply = event->user_data;
    if (event->data_len < 0 || (size_t)event->data_len >= sizeof(reply->body) - reply->used) {
        reply->overflow = true;
        return ESP_FAIL;
    }
    memcpy(reply->body + reply->used, event->data, event->data_len);
    reply->used += event->data_len;
    reply->body[reply->used] = 0;
    return ESP_OK;
}

static const char *pull_snapshot(const tb_config_t *config, tb_lite_snapshot_t *snapshot, bool *retry)
{
    tb_network_status_t network;
    tb_network_status(&network);
    *retry = false;
    if (!network.station_connected) {
        tb_network_retry_link_station();
        return "wifi_no_ip";
    }
    if (!config->pair_key[0]) return "pairing_missing";
    if (esp_get_free_heap_size() < 18000u) return "heap_guard";
    char url[128], authorization[sizeof(config->pair_key) + 8];
    snprintf(url, sizeof(url), "http://%s%s",
             tb_network_wifi_uplink() ? config->core_host : TB_CORE_AP_IP, TB_LITE_SNAPSHOT_PATH);
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->pair_key);
    reply_t reply = {0};
    esp_http_client_config_t request = {.url = url, .method = HTTP_METHOD_GET, .timeout_ms = 1600,
        .disable_auto_redirect = true, .event_handler = receive_snapshot, .user_data = &reply,
        .buffer_size = 768, .buffer_size_tx = 512};
    esp_http_client_handle_t http = esp_http_client_init(&request);
    if (!http) return "http_allocation";
    esp_err_t result = esp_http_client_set_header(http, "Authorization", authorization);
    if (result == ESP_OK) result = esp_http_client_perform(http);
    int status = esp_http_client_get_status_code(http);
    bool complete = esp_http_client_is_complete_data_received(http);
    esp_http_client_cleanup(http);
    *retry = true;
    if (status == 401 || status == 403) { *retry = false; return "pairing_rejected"; }
    if (status == 503) return "core_busy";
    if (reply.overflow) { *retry = false; return "snapshot_too_large"; }
    if (result != ESP_OK) return result == ESP_ERR_TIMEOUT ? "core_http_timeout" : "core_transport";
    if (status != 200 || !complete) return "core_http_response";
    if (!tb_lite_snapshot_decode(reply.body, reply.used, snapshot)) return "invalid_snapshot";
    *retry = false;
    return !config->telemetry_core_uid[0] || !strcmp(snapshot->uid, config->telemetry_core_uid)
        ? NULL : "core_uid_mismatch";
}

static bool publisher_configured(const tb_config_t *config)
{
    return config->lite_upload_enabled && tb_lite_uid_valid(config->telemetry_core_uid) &&
        tb_lite_uid_valid(config->cloud_device_uid) && config->pair_key[0] && config->uplink_token[0] &&
        !strncmp(config->uplink_url, "https://", 8) && config->uplink_url[8] &&
        config->sequence_floor && config->sequence_floor < TB_LITE_SAFE_SEQ;
}

static bool refresh_capacity(void)
{
    uint64_t total, free;
    if (esp_vfs_fat_info("/gps", &total, &free) != ESP_OK) return false;
    uint64_t available = free + s_journal.bytes > FAT_RESERVE ? free + s_journal.bytes - FAT_RESERVE : 0;
    s_journal.capacity = available < TB_LITE_JOURNAL_LIMIT ? (uint32_t)available : TB_LITE_JOURNAL_LIMIT;
    portENTER_CRITICAL(&s_mux);
    s_state.total_bytes = total;
    s_state.free_bytes = free;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

static void refresh_legacy_head(void)
{
    s_legacy_oldest = 0;
    if (!tb_gps_outbox_pending(&s_legacy)) return;
    char record[TB_LITE_RECORD_MAX];
    uint64_t sequence;
    bool health;
    if (!tb_gps_outbox_peek(&s_legacy, record, sizeof(record)))
        (void)tb_lite_record_meta(record, &sequence, &health, &s_legacy_oldest);
    tb_gps_outbox_retry(&s_legacy);
}

/* Opens the old journal for draining, never for new writes. Both formats
 * share the existing sequence reservation and its exact UID and endpoint. */
static bool ensure_journal(const tb_config_t *config)
{
    if (s_journal.ready && s_meta_valid && s_legacy.ready) return true;
    portENTER_CRITICAL(&s_mux);
    int mounted = s_mount_status;
    portEXIT_CRITICAL(&s_mux);
    static uint64_t retry_at;
    uint64_t now = ticks_ms();
    if (mounted != 1 || now < retry_at) return false;
    retry_at = now + 5000u;
    esp_err_t meta_error = s_meta_valid ? ESP_OK : prepare_publisher(config);
    if (meta_error != ESP_OK) {
        storage_state("sequence_unavailable", meta_error);
        return false;
    }
    if (!s_legacy.ready) {
        int legacy_error = legacy_exists() ? tb_telemetry_outbox_init(&s_legacy, LEGACY_JOURNAL, LEGACY_BUDGET) : 0;
        if (legacy_error) {
            storage_state("legacy_journal_fault", legacy_error);
            return false;
        }
        s_legacy.ready = true;
        refresh_legacy_head();
    }
    char identity[100];
    int length = snprintf(identity, sizeof(identity), "%s\n%s", s_meta.core_uid, s_meta.cloud_uid);
    uint32_t owner = tb_outbox_crc32((const unsigned char *)identity, (size_t)length);
    int error = s_journal.ready ? 0 : tb_lite_journal_open(&s_journal, JOURNAL_DIRECTORY, owner, TB_LITE_JOURNAL_LIMIT);
    if (!error && s_journal.sequence > s_meta.reserved_end) error = EINVAL;
    if (!error && !refresh_capacity()) error = EIO;
    if (error) s_journal.ready = false;
    storage_state(error ? "journal_fault" : "ready", error);
    return !error;
}

static void account_drop(bool health)
{
    if (health) ++s_lost_health;
    else ++s_lost_operational;
}

static void stage_record(const tb_lite_snapshot_t *snapshot, bool online, uint64_t captured,
                         const tb_lite_link_health_t *health, tb_lite_event_t event)
{
    if (s_stage_count == STAGED_RECORDS || (health && s_stage_count == STAGED_RECORDS - 1)) {
        account_drop(health != NULL);
        return;
    }
    staged_record_t *item = &s_stage[(s_stage_head + s_stage_count++) % STAGED_RECORDS];
    *item = (staged_record_t){.snapshot = *snapshot, .online = online, .captured_ms = captured,
        .health = health != NULL, .event = event};
    if (health) item->link = *health;
}

static void flush_stage(const tb_config_t *config)
{
    if (xSemaphoreTake(s_filesystem, pdMS_TO_TICKS(100)) != pdTRUE) return;
    unsigned saved = 0;
    if (ensure_journal(config)) {
        if (!refresh_capacity()) storage_state("filesystem_info_unavailable", EIO);
        else while (s_stage_count) {
            staged_record_t *item = &s_stage[s_stage_head];
            if (!item->sequence) {
                esp_err_t error = allocate_sequence(&item->sequence);
                if (error != ESP_OK) { storage_state("sequence_unavailable", error); break; }
            }
            if (!s_stage_json[0]) {
                size_t length;
                if (item->health) length = tb_lite_health(s_stage_json, sizeof(s_stage_json), item->sequence,
                    item->captured_ms, &item->snapshot, item->online, &item->link, item->event);
                else {
                    publisher_t next = s_meta;
                    if (item->captured_ms >= next.reference_after_ms &&
                        tb_lite_reference_update(&next.reference, &item->snapshot, item->captured_ms))
                        (void)save_publisher(&next);
                    length = tb_lite_operational(s_stage_json, sizeof(s_stage_json), item->sequence,
                        item->captured_ms, &item->snapshot, &s_meta.reference);
                }
                if (!length) s_stage_json[0] = '\0';
            }
            int error = s_stage_json[0] ? tb_lite_journal_append(&s_journal, item->sequence,
                item->captured_ms, item->health, s_stage_json) : EMSGSIZE;
            if (error && error != ENOSPC && error != EMSGSIZE) {
                s_journal.ready = false;
                storage_state("journal_write_failed", error);
                break; /* Retry the very same sequence and bytes after reopening. */
            }
            if (error) account_drop(item->health);
            else ++saved;
            s_stage_head = (s_stage_head + 1) % STAGED_RECORDS;
            --s_stage_count;
            s_stage_json[0] = '\0';
            storage_state(error == ENOSPC ? "queue_full" : error ? "record_too_large" : "ready", error);
        }
        if (s_journal.ready && (s_lost_operational || s_lost_health)) {
            int err = tb_lite_journal_drop(&s_journal, s_lost_operational, s_lost_health);
            if (!err) s_lost_operational = s_lost_health = 0;
            else storage_state("loss_checkpoint_failed", err);
        }
    }
    portENTER_CRITICAL(&s_mux);
    s_state.durable += saved;
    s_state.staged = s_stage_count;
    s_state.dropped_operational = s_journal.dropped_operational + s_lost_operational;
    s_state.dropped_health = s_journal.dropped_health + s_lost_health;
    s_state.discarded = s_state.dropped_operational + s_state.dropped_health;
    portEXIT_CRITICAL(&s_mux);
    xSemaphoreGive(s_filesystem);
}

static tb_lite_link_health_t link_health(const collector_state_t *s, uint64_t utc)
{
    tb_network_status_t network;
    tb_network_status(&network);
    uint64_t now = ticks_ms();
    bool route = tb_link_collector_has_route();
    bool age_valid = !s->pending || (s->oldest_ms && utc >= s->oldest_ms);
    return (tb_lite_link_health_t){.link_boot_id = tb_boot_id(), .link_uptime_ms = now,
        .link_reset_reason = (uint32_t)esp_reset_reason(), .heap_free = esp_get_free_heap_size(),
        .heap_minimum = esp_get_minimum_free_heap_size(), .largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        .wifi_connected = network.station_connected, .wifi_rssi = network.rssi,
        .internet_reachable = !route ? 0 : s->http_status > 0 && now - s->last_attempt_ms <= 90000 ? 1 : -1,
        .cloud_connected = route && s->last_success_tick && !s->failures && now - s->last_success_tick <= 90000,
        .cloud_last_success_ms = s->last_success_ms, .last_http_status = s->http_status,
        .consecutive_failures = s->failures, .cloud_error_stage = s->cloud_stage,
        .capacity = s->capacity, .used = (uint32_t)s->bytes, .pending = (uint32_t)s->pending,
        .oldest_age_valid = age_valid, .oldest_age = age_valid && s->pending ? (uint32_t)((utc - s->oldest_ms) / 1000) : 0,
        .dropped = s->discarded, .dropped_operational = s->dropped_operational, .dropped_health = s->dropped_health,
        .replayed = s->replayed, .recovered_tail_bytes = s->recovered_tail, .link_disconnects = s->disconnects,
        .disconnect_reason = s->disconnect_reason, .poll_failures = s->rejected};
}

static void collector_task(void *unused)
{
    (void)unused;
    const tb_config_t *config = tb_config();
    const bool publish = publisher_configured(config);
    tb_lite_cursor_t cursor = {0};
    tb_lite_fuel_counter_t previous_flow = {0};
    tb_lite_snapshot_t last = {0};
    tb_lite_contact_t contact = {0};
    uint64_t health_due = ticks_ms() + TB_LITE_HEALTH_MS;
    vTaskDelay(pdMS_TO_TICKS(375));
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TB_LITE_OPERATIONAL_MS));
        if (tb_web_ota_busy()) continue;
        tb_lite_snapshot_t current;
        const char *failure = NULL;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            bool retry;
            failure = pull_snapshot(config, &current, &retry);
            if (!failure || !retry || attempt == 2) break;
            vTaskDelay(pdMS_TO_TICKS(180u * (attempt + 1)));
        }
        bool seen = !failure;
        uint64_t now = ticks_ms(), utc = epoch_ms(), captured;
        tb_lite_event_t event = tb_lite_contact_update(&contact, seen ? current.boot_id : 0);
        if (seen && event == TB_LITE_NO_EVENT && ((current.guard ^ last.guard) & TB_LITE_MID_CONFLICT))
            event = TB_LITE_MID_STATE;
        if (seen) last = current;
        bool online = contact.boot && !contact.disconnected;
        portENTER_CRITICAL(&s_mux);
        s_state.received += seen;
        s_state.rejected += !seen;
        if (seen) s_state.last_rx_ms = now;
        s_state.poll_error = failure;
        s_state.core_online = online;
        if (event == TB_LITE_CORE_LOST) {
            ++s_state.disconnects;
            s_state.disconnect_reason = failure;
        }
        s_state.core_boot = last.boot_id;
        s_state.core_reset = last.reset_reason;
        s_state.core_reset_valid = last.reset_reason_valid;
        s_state.guard = last.guard;
        s_state.frames = last.frames;
        s_state.core_heap = last.heap_minimum;
        s_state.parked_sample_ms = seen && (last.valid & TB_LITE_BIT(TB_LITE_SPEED)) &&
            last.values[TB_LITE_SPEED] >= 0 && last.values[TB_LITE_SPEED] <= 0.1 ? now : 0;
        portEXIT_CRITICAL(&s_mux);
        if (!publish) continue;
        if (seen && tb_lite_candidate(&cursor, &last, utc, &captured)) {
            tb_lite_fuel_window_t window = {0};
            if (cursor.boot_id == last.boot_id) (void)tb_lite_fuel_delta(&previous_flow, &last.flow, &window);
            portENTER_CRITICAL(&s_mux);
            s_state.fuel_window = window;
            s_state.fuel_window_time = window.span_ms ? captured : 0;
            portEXIT_CRITICAL(&s_mux);
            stage_record(&last, true, captured, NULL, TB_LITE_NO_EVENT);
            previous_flow = last.flow;
            cursor.boot_id = last.boot_id;
            cursor.local_seq = last.local_seq;
            if (captured) cursor.captured_ms = captured;
        }
        uint64_t health_time = utc ? utc : seen && last.unix_ms >= TB_LITE_EPOCH_MS ? last.unix_ms : 0;
        if (health_time && (event != TB_LITE_NO_EVENT || now >= health_due)) {
            collector_state_t state = state_snapshot();
            tb_lite_link_health_t health = link_health(&state, health_time);
            stage_record(&last, online, health_time, &health, event);
            if (now >= health_due) health_due = now + TB_LITE_HEALTH_MS;
        }
        flush_stage(config);
    }
}

static int read_batch(char *payload, upload_batch_t *batch, size_t limit)
{
    memset(batch, 0, sizeof(*batch));
    char record[TB_LITE_RECORD_MAX];
    tb_lite_ticket_t legacy = {0};
    if (tb_gps_outbox_pending(&s_legacy)) {
        int error = tb_gps_outbox_peek(&s_legacy, record, sizeof(record));
        if (error) return error;
        if (!tb_lite_record_meta(record, &legacy.sequence, &legacy.health, &legacy.captured_ms)) return EBADMSG;
    }
    char token_json[sizeof(tb_config()->uplink_token) * 6 + 16];
    cJSON *token = cJSON_CreateString(tb_config()->uplink_token);
    if (!token) return ENOMEM;
    bool encoded = cJSON_PrintPreallocated(token, token_json, sizeof(token_json), false);
    cJSON_Delete(token);
    if (!encoded) return EMSGSIZE;
    tb_lite_buffer_t body = {.data = payload, .capacity = POST_BYTES};
    tb_lite_append(&body, "{\"device_uid\":\"%s\",\"token\":%s,\"samples\":[", tb_config()->cloud_device_uid, token_json);

    /* A legacy record may be newer than a V2 record after an explicit
     * rollback. Merge the two heads without acknowledging across a hole. */
    for (size_t i = 0; i < limit; ++i) {
        tb_lite_ticket_t entry = {0};
        int error = tb_lite_journal_read(&s_journal, i ? &batch->ticket[i - 1] : NULL, &entry, record, sizeof(record));
        if (error && error != ENOENT) return error;
        if (!error && legacy.sequence == entry.sequence) return EBADMSG;
        if (legacy.sequence && (error == ENOENT || legacy.sequence < entry.sequence)) {
            if (i) break;
            error = tb_gps_outbox_peek(&s_legacy, record, sizeof(record));
            if (error) return error;
            tb_lite_append(&body, "%s", record);
            batch->ticket[0] = legacy;
            batch->count = 1;
            batch->health = legacy.health;
            batch->legacy = true;
            break;
        }
        if (error == ENOENT || strlen(record) + 4 >= POST_BYTES - body.used) break;
        tb_lite_append(&body, "%s%s", i ? "," : "", record);
        batch->ticket[i] = entry;
        batch->count++;
        batch->health += entry.health;
    }
    if (!batch->legacy) tb_gps_outbox_retry(&s_legacy);
    if (!batch->count) return ENOENT;
    tb_lite_append(&body, "]}");
    batch->length = body.used;
    return body.failed ? EMSGSIZE : 0;
}

static void upload_result(esp_err_t error, int status, const char *stage, const upload_batch_t *batch, int ack_error)
{
    uint64_t utc = epoch_ms(), now = ticks_ms();
    unsigned replayed = 0;
    if (error == ESP_OK && !ack_error) {
        for (size_t i = 0; i < batch->count; ++i) {
            uint64_t timestamp = batch->ticket[i].captured_ms;
            replayed += timestamp && utc > timestamp && utc - timestamp > 30000;
        }
    }
    portENTER_CRITICAL(&s_mux);
    s_state.http_status = status;
    s_state.uplink_error = error;
    s_state.cloud_stage = ack_error ? "local_ack" : stage;
    s_state.last_attempt_ms = now;
    if (error == ESP_OK && !ack_error) {
        s_state.failures = 0;
        s_state.last_success_ms = utc;
        s_state.last_success_tick = now;
        uint64_t ack = batch->ticket[batch->count - 1].sequence;
        if (ack > s_state.last_ack) s_state.last_ack = ack;
        s_state.delivered += (uint32_t)batch->count;
        s_state.replayed += replayed;
    } else if (s_state.failures < UINT32_MAX) ++s_state.failures;
    if (error == ESP_ERR_INVALID_RESPONSE && status >= 200 && status < 300) s_state.invalid_ack++;
    if (ack_error) s_state.writes_failed++;
    portEXIT_CRITICAL(&s_mux);
}

static void uploader_task(void *unused)
{
    (void)unused;
    const uint64_t ota_grace = ticks_ms() + 18000u;
    uint64_t next_try = ota_grace;
    bool had_route = false;
    char *payload = NULL;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        bool route = tb_link_collector_has_route();
        uint64_t now = ticks_ms();
        if (route && !had_route && now >= ota_grace) next_try = now;
        had_route = route;
        if (now < next_try || tb_web_ota_busy()) continue;
        collector_state_t view = state_snapshot();
        if (!view.ready || !view.pending) continue;
        if (!route || !epoch_ms() || esp_get_free_heap_size() < 52000u ||
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 16000u) {
            const char *stage = !route ? "no_route" : !epoch_ms() ? "clock" : "heap_guard";
            upload_result(ESP_ERR_INVALID_STATE, 0, stage, NULL, 0);
            next_try = now + tb_lite_retry_delay(view.failures + 1, 0);
            continue;
        }
        if (!payload) payload = malloc(POST_BYTES);
        if (!payload) {
            upload_result(ESP_ERR_NO_MEM, 0, "upload_buffer", NULL, 0);
            next_try = now + 3000u;
            continue;
        }
        upload_batch_t batch;
        if (xSemaphoreTake(s_filesystem, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        size_t limit = view.http_status == 400 || view.http_status == 413 ? 1 : TB_LITE_BATCH_MAX;
        int read = s_journal.ready && s_meta_valid && s_legacy.ready ? read_batch(payload, &batch, limit) : ENOENT;
        if (read && read != ENOENT && read != ENOMEM) {
            s_journal.ready = false;
            storage_state("journal_read_failed", read);
        }
        xSemaphoreGive(s_filesystem);
        if (read == ENOMEM) {
            upload_result(ESP_ERR_NO_MEM, 0, "batch_memory", NULL, 0);
            next_try = now + tb_lite_retry_delay(view.failures + 1, 0);
        }
        if (read) continue;
        int status = 0;
        const char *stage = NULL;
        esp_err_t result = tb_link_collector_post(payload, batch.length, batch.ticket[batch.count - 1].sequence,
            (unsigned)batch.count - batch.health, batch.health, !view.http_status && (view.failures & 1u), &status, &stage);
        xSemaphoreTake(s_filesystem, portMAX_DELAY);
        int ack_error = 0;
        if (result == ESP_OK) {
            ack_error = batch.legacy ? tb_gps_outbox_ack(&s_legacy) : tb_lite_journal_commit(&s_journal, batch.ticket, batch.count);
            if (batch.legacy) refresh_legacy_head();
            storage_state(ack_error ? "journal_ack_failed" : s_journal.ready ? "ready" : "journal_read_failed", ack_error);
        } else if (batch.legacy) tb_gps_outbox_retry(&s_legacy);
        xSemaphoreGive(s_filesystem);
        upload_result(result, status, stage, &batch, ack_error);
        view = state_snapshot();
        next_try = ticks_ms() + tb_lite_retry_delay(view.failures, status);
    }
}

esp_err_t tb_link_collector_start(SemaphoreHandle_t filesystem_lock)
{
    s_filesystem = filesystem_lock;
    const tb_config_t *config = tb_config();
    const bool publish = publisher_configured(config);
    s_state.phase = publish ? "waiting_filesystem" : config->lite_upload_enabled ? "configuration_required" : "shadow";
    if (publish && xTaskCreate(uploader_task, "tb_lite_https", 8192, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return xTaskCreate(collector_task, "tb_lite_poll", 7168, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t tb_link_collector_reset_reference(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) return ESP_OK;
    cJSON *body = NULL;
    esp_err_t err = tb_http_read_json(request, 128, &body);
    if (err != ESP_OK) return err;
    bool authorized = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "authorized")) &&
                      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "stationary"));
    cJSON_Delete(body);
    if (!authorized || tb_web_ota_busy() || xSemaphoreTake(s_filesystem, pdMS_TO_TICKS(100)) != pdTRUE)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "park and authorize the new Link reference");
    uint64_t now = ticks_ms(), utc = epoch_ms();
    collector_state_t view = state_snapshot();
    err = ESP_ERR_INVALID_STATE;
    if (s_meta_valid && s_journal.ready && utc && view.parked_sample_ms &&
        now >= view.parked_sample_ms && now - view.parked_sample_ms <= 12000u) {
        publisher_t candidate = s_meta;
        candidate.reference = (tb_lite_reference_t){0};
        candidate.reference_after_ms = utc;
        err = save_publisher(&candidate);
        storage_state(err == ESP_OK ? "ready" : "reference_write_failed", err);
    }
    xSemaphoreGive(s_filesystem);
    if (err != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "need fresh stationary Core, clock and writable Link metadata");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true,\"reference\":\"waiting_new_reading\"}");
}

void tb_link_collector_json(cJSON *root)
{
    collector_state_t view = state_snapshot();
    tb_lite_link_health_t health = link_health(&view, epoch_ms());
    cJSON *o = cJSON_AddObjectToObject(root, "telemetry_collector");
    if (!o) return;
    cJSON_AddStringToObject(o, "phase", view.phase ? view.phase : "starting");
    cJSON_AddBoolToObject(o, "publishing", publisher_configured(tb_config()));
    cJSON_AddBoolToObject(o, "core_online", view.core_online);
    cJSON_AddBoolToObject(o, "storage_ready", view.ready);
    cJSON_AddNumberToObject(o, "storage_error", view.storage_error);
    cJSON_AddNumberToObject(o, "sequence_floor", (double)tb_config()->sequence_floor);
    cJSON_AddNumberToObject(o, "sequence_reserved_end", (double)view.sequence_reserved_end);
    cJSON_AddNumberToObject(o, "outbox_budget_bytes", TB_LITE_JOURNAL_LIMIT);
    cJSON_AddNumberToObject(o, "outbox_bytes", (double)view.bytes);
    cJSON_AddNumberToObject(o, "filesystem_bytes", (double)view.total_bytes);
    cJSON_AddNumberToObject(o, "filesystem_free_bytes", (double)view.free_bytes);
    cJSON_AddNumberToObject(o, "pending", (double)view.pending);
    cJSON_AddNumberToObject(o, "last_durable_ack", (double)view.last_ack);
    cJSON_AddNumberToObject(o, "http_status", view.http_status);
    cJSON_AddStringToObject(o, "uplink_error", esp_err_to_name(view.uplink_error));
    cJSON_AddNumberToObject(o, "invalid_acks", view.invalid_ack);
    cJSON_AddNumberToObject(o, "ack_io_errors", view.writes_failed);
    cJSON_AddNumberToObject(o, "received", view.received);
    cJSON_AddNumberToObject(o, "poll_failures", view.rejected);
    cJSON_AddNumberToObject(o, "durable", view.durable);
    cJSON_AddNumberToObject(o, "delivered", view.delivered);
    cJSON_AddNumberToObject(o, "discarded", view.discarded);
    cJSON_AddNumberToObject(o, "core_boot", view.core_boot);
    cJSON_AddNumberToObject(o, "core_frames", view.frames);
    cJSON_AddNumberToObject(o, "core_guard", view.guard);
    cJSON_AddNumberToObject(o, "core_heap_minimum", view.core_heap);
    cJSON_AddNumberToObject(o, "reference_unix_ms", (double)view.reference_ms);
    if (view.last_rx_ms) cJSON_AddNumberToObject(o, "core_last_rx_age_ms", (double)(ticks_ms() - view.last_rx_ms));
    cJSON_AddNumberToObject(o, "staged", view.staged);
    cJSON_AddNumberToObject(o, "link_heap_free", health.heap_free);
    cJSON_AddNumberToObject(o, "link_heap_minimum", health.heap_minimum);
    cJSON_AddNumberToObject(o, "link_heap_largest_block", health.largest_block);
    cJSON_AddNumberToObject(o, "link_boot_id", health.link_boot_id);
    cJSON_AddNumberToObject(o, "core_boot_id", view.core_boot);
    cJSON_AddNumberToObject(o, "link_reset_reason", health.link_reset_reason);
    if (view.core_reset_valid) cJSON_AddNumberToObject(o, "core_reset_reason", view.core_reset);
    else cJSON_AddNullToObject(o, "core_reset_reason");
    cJSON_AddBoolToObject(o, "wifi_connected", health.wifi_connected);
    cJSON_AddNumberToObject(o, "wifi_rssi", health.wifi_rssi);
    if (health.internet_reachable < 0) cJSON_AddNullToObject(o, "internet_reachable");
    else cJSON_AddBoolToObject(o, "internet_reachable", health.internet_reachable > 0);
    cJSON_AddBoolToObject(o, "cloud_connected", health.cloud_connected);
    cJSON_AddNumberToObject(o, "cloud_last_success_ms", (double)health.cloud_last_success_ms);
    cJSON_AddNumberToObject(o, "cloud_last_http_status", health.last_http_status);
    cJSON_AddNumberToObject(o, "cloud_consecutive_failures", health.consecutive_failures);
    cJSON_AddStringToObject(o, "cloud_error_stage", health.cloud_error_stage ? health.cloud_error_stage : "");
    cJSON_AddNumberToObject(o, "offline_queue_capacity_bytes", health.capacity);
    cJSON_AddNumberToObject(o, "offline_queue_used_bytes", health.used);
    cJSON_AddNumberToObject(o, "offline_queue_pending_records", health.pending);
    if (health.oldest_age_valid) cJSON_AddNumberToObject(o, "offline_queue_oldest_age_seconds", health.oldest_age);
    else cJSON_AddNullToObject(o, "offline_queue_oldest_age_seconds");
    cJSON_AddNumberToObject(o, "offline_queue_dropped_records", health.dropped);
    cJSON_AddNumberToObject(o, "offline_queue_replayed_records", health.replayed);
    cJSON_AddNumberToObject(o, "queue_dropped_operational", health.dropped_operational);
    cJSON_AddNumberToObject(o, "queue_dropped_health", health.dropped_health);
    cJSON_AddNumberToObject(o, "queue_recovered_tail_bytes", health.recovered_tail_bytes);
    cJSON_AddNumberToObject(o, "core_link_disconnect_count", health.link_disconnects);
    cJSON_AddStringToObject(o, "core_link_last_disconnect_reason", health.disconnect_reason ? health.disconnect_reason : "");
    cJSON_AddStringToObject(o, "core_poll_error", view.poll_error ? view.poll_error : "");
    if (view.fuel_window.span_ms) {
        cJSON *window = cJSON_AddObjectToObject(o, "pid183_window");
        if (window) {
            cJSON_AddNumberToObject(window, "utc_ms", (double)view.fuel_window_time);
            cJSON_AddNumberToObject(window, "span_ms", (double)view.fuel_window.span_ms);
            cJSON_AddNumberToObject(window, "covered_ms", (double)view.fuel_window.covered_ms);
            if (view.fuel_window.covered_ms) {
                cJSON_AddNumberToObject(window, "litres", (double)view.fuel_window.microlitres / 1000000.0);
                cJSON_AddNumberToObject(window, "average_lph",
                    (double)view.fuel_window.microlitres * 3.6 / (double)view.fuel_window.covered_ms);
            }
        }
    }
}
