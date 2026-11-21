#include "tb_link_collector.h"
#include "tb_link_telemetry_codec.h"
#include "tb_gps_outbox.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/task.h"
#include "nvs.h"

#define JOURNAL "/gps/telemetry.log"
#define JOURNAL_BUDGET (256u * 1024u)
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
    const char *phase;
    uint32_t received, rejected, discarded, durable, delivered, invalid_ack;
    uint32_t core_boot, frames, guard, core_heap, writes_failed;
    uint64_t last_rx_ms, last_ack, pending, bytes, total_bytes, free_bytes;
    uint64_t parked_sample_ms, reference_ms;
    bool core_online, ready;
    int storage_error, http_status;
    esp_err_t uplink_error;
} collector_state_t;

static tb_gps_outbox_t s_outbox;
static SemaphoreHandle_t s_filesystem;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static collector_state_t s_state;
static int s_mount_status; /* 0 pending, 1 mounted, <0 error. */
static publisher_t s_meta;
static nvs_handle_t s_nvs;
static bool s_nvs_open, s_meta_valid;
static uint64_t s_next_sequence;

static uint64_t ticks_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static uint64_t epoch_ms(void)
{
    struct timeval now;
    if (gettimeofday(&now,NULL) != 0 || now.tv_sec < 1704067200) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_usec / 1000;
}

static void storage_state(const char *phase, int error)
{
    portENTER_CRITICAL(&s_mux);
    s_state.phase = phase;
    s_state.storage_error = error;
    s_state.ready = s_outbox.ready && s_meta_valid;
    s_state.pending = tb_gps_outbox_pending(&s_outbox);
    s_state.bytes = s_outbox.file_bytes;
    s_state.reference_ms = s_meta.reference.started_ms;
    portEXIT_CRITICAL(&s_mux);
}

void tb_link_collector_storage(esp_err_t result)
{
    uint64_t total=0, available=0;
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
           memchr(meta->core_uid,0,sizeof(meta->core_uid)) &&
           memchr(meta->cloud_uid,0,sizeof(meta->cloud_uid)) &&
           memchr(meta->endpoint,0,sizeof(meta->endpoint)) &&
           !strcmp(meta->core_uid,config->telemetry_core_uid) &&
           !strcmp(meta->cloud_uid,config->cloud_device_uid) &&
           !strcmp(meta->endpoint,config->uplink_url) && tb_lite_reference_valid(&meta->reference);
}

static esp_err_t save_publisher(const publisher_t *meta)
{
    esp_err_t err = nvs_set_blob(s_nvs,META_KEY,meta,sizeof(*meta));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err == ESP_OK) s_meta = *meta;
    else s_meta_valid = false;
    return err;
}

static esp_err_t prepare_publisher(const tb_config_t *config)
{
    if (s_nvs_open) { nvs_close(s_nvs); s_nvs_open = false; }
    esp_err_t err = nvs_open("tb_link_lite",NVS_READWRITE,&s_nvs);
    if (err != ESP_OK) return err;
    s_nvs_open = true;
    publisher_t meta = {0};
    size_t size = sizeof(meta);
    err = nvs_get_blob(s_nvs,META_KEY,&meta,&size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        struct stat info;
        /* Do not recreate a lost sequence counter next to any previous log. */
        if (stat(JOURNAL,&info) == 0 || errno != ENOENT ||
            stat(JOURNAL ".bak",&info) == 0 || errno != ENOENT ||
            stat(JOURNAL ".tmp",&info) == 0 || errno != ENOENT) return ESP_ERR_INVALID_STATE;
        meta.version = META_FORMAT;
        strcpy(meta.core_uid,config->telemetry_core_uid);
        strcpy(meta.cloud_uid,config->cloud_device_uid);
        strcpy(meta.endpoint,config->uplink_url);
        err = save_publisher(&meta);
        if (err != ESP_OK) return err;
    } else {
        if (err != ESP_OK) return err;
        if (size != sizeof(meta) || !correct_owner(&meta,config)) return ESP_ERR_INVALID_STATE;
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
        if (!tb_lite_sequence_range(tb_config()->sequence_floor,next.reserved_end,&first,&next.reserved_end))
            return ESP_ERR_INVALID_SIZE;
        esp_err_t err = save_publisher(&next);
        if (err != ESP_OK) return err;
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
    if (event->data_len < 0 || (size_t)event->data_len >= sizeof(reply->body)-reply->used) {
        reply->overflow = true;
        return ESP_FAIL;
    }
    memcpy(reply->body + reply->used,event->data,event->data_len);
    reply->used += event->data_len;
    reply->body[reply->used] = 0;
    return ESP_OK;
}

static bool pull_snapshot(const tb_config_t *config, tb_lite_snapshot_t *snapshot)
{
    tb_network_status_t network;
    tb_network_status(&network);
    if (!network.station_connected || !config->pair_key[0] || esp_get_free_heap_size() < 18000u) return false;
    char url[128], authorization[sizeof(config->pair_key)+8];
    snprintf(url,sizeof(url),"http://%s%s",tb_network_wifi_uplink()?config->core_host:TB_CORE_AP_IP,TB_LITE_SNAPSHOT_PATH);
    snprintf(authorization,sizeof(authorization),"Bearer %s",config->pair_key);
    reply_t reply = {0};
    esp_http_client_config_t request = {.url=url,.method=HTTP_METHOD_GET,.timeout_ms=1600,
        .disable_auto_redirect=true,.event_handler=receive_snapshot,.user_data=&reply,
        .buffer_size=768,.buffer_size_tx=512};
    esp_http_client_handle_t http = esp_http_client_init(&request);
    if (!http) return false;
    bool ok = esp_http_client_set_header(http,"Authorization",authorization) == ESP_OK &&
              esp_http_client_perform(http) == ESP_OK && esp_http_client_get_status_code(http) == 200 &&
              esp_http_client_is_complete_data_received(http) && !reply.overflow;
    esp_http_client_cleanup(http);
    return ok && tb_lite_snapshot_decode(reply.body,reply.used,snapshot) &&
           (!config->telemetry_core_uid[0] || !strcmp(snapshot->uid,config->telemetry_core_uid));
}

static bool publisher_configured(const tb_config_t *config)
{
    return config->lite_upload_enabled && tb_lite_uid_valid(config->telemetry_core_uid) &&
        tb_lite_uid_valid(config->cloud_device_uid) && config->pair_key[0] && config->uplink_token[0] &&
        !strncmp(config->uplink_url,"https://",8) && config->uplink_url[8] &&
        config->sequence_floor && config->sequence_floor < TB_LITE_SAFE_SEQ;
}

static bool ensure_journal(const tb_config_t *config)
{
    portENTER_CRITICAL(&s_mux);
    int mount = s_mount_status;
    portEXIT_CRITICAL(&s_mux);
    if (mount != 1 || xSemaphoreTake(s_filesystem,pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool ready = s_outbox.ready && s_meta_valid;
    static uint64_t retry_at;
    uint64_t now = ticks_ms();
    if (!ready && now >= retry_at) {
        retry_at = now + 60000u;
        esp_err_t error = s_meta_valid ? ESP_OK : prepare_publisher(config);
        int file_error = error == ESP_OK ? tb_telemetry_outbox_init(&s_outbox,JOURNAL,JOURNAL_BUDGET) : error;
        ready = file_error == 0;
        storage_state(ready?"ready":error!=ESP_OK?"sequence_unavailable":"journal_fault",file_error);
    }
    xSemaphoreGive(s_filesystem);
    return ready;
}

static bool queue_record(const tb_lite_snapshot_t *snapshot, bool online, uint64_t captured,
                         bool health, tb_lite_event_t event)
{
    if (xSemaphoreTake(s_filesystem,pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool stored = false;
    if (s_outbox.ready && s_meta_valid && (health || captured >= s_meta.reference_after_ms)) {
        char record[TB_LITE_RECORD_MAX];
        uint64_t sequence = 0;
        esp_err_t error = allocate_sequence(&sequence);
        size_t length = 0;
        if (!health && error == ESP_OK) {
            publisher_t candidate = s_meta;
            if (tb_lite_reference_update(&candidate.reference,snapshot,captured)) error = save_publisher(&candidate);
            if (error == ESP_OK)
                length = tb_lite_operational(record,sizeof(record),sequence,captured,snapshot,&s_meta.reference);
        } else if (error == ESP_OK) {
            portENTER_CRITICAL(&s_mux);
            uint32_t dropped = s_state.discarded;
            portEXIT_CRITICAL(&s_mux);
            length = tb_lite_health(record,sizeof(record),sequence,captured,snapshot,online,
                (unsigned)tb_gps_outbox_pending(&s_outbox),dropped,esp_get_minimum_free_heap_size(),event);
        }
        int res = error != ESP_OK ? error : length ? tb_gps_outbox_append(&s_outbox,record) : EMSGSIZE;
        stored = res == 0;
        storage_state(stored?"ready":res==ENOSPC?"queue_full":error?"sequence_unavailable":"journal_write_failed",res);
    }
    xSemaphoreGive(s_filesystem);
    return stored;
}

static void update_space(void)
{
    if (xSemaphoreTake(s_filesystem,pdMS_TO_TICKS(100)) != pdTRUE) return;
    uint64_t total, free;
    if (esp_vfs_fat_info("/gps",&total,&free) == ESP_OK) {
        portENTER_CRITICAL(&s_mux);
        s_state.total_bytes = total;
        s_state.free_bytes = free;
        portEXIT_CRITICAL(&s_mux);
    }
    xSemaphoreGive(s_filesystem);
}

static void collector_task(void *unused)
{
    (void)unused;
    const tb_config_t *config = tb_config();
    bool publish = publisher_configured(config);
    tb_lite_cursor_t cursor = {0};
    tb_lite_snapshot_t last = {0};
    bool was_online = false;
    uint64_t health_due = TB_LITE_HEALTH_MS;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(TB_LITE_OPERATIONAL_MS));
        if (tb_web_ota_busy()) continue;
        bool ready = publish && ensure_journal(config);
        tb_lite_snapshot_t current;
        bool online = pull_snapshot(config,&current);
        uint64_t now = ticks_ms(), utc = epoch_ms(), captured = 0;
        tb_lite_event_t event = TB_LITE_NO_EVENT;
        if (online && !was_online) event = last.boot_id ? TB_LITE_CORE_RESTORED : TB_LITE_CORE_BOOT;
        else if (!online && was_online) event = TB_LITE_CORE_LOST;
        else if (online && current.boot_id != last.boot_id) event = TB_LITE_CORE_BOOT;
        else if (online && ((current.guard ^ last.guard) & TB_LITE_MID_CONFLICT)) event = TB_LITE_MID_STATE;
        if (online) last = current;
        was_online = online;
        bool candidate = online && tb_lite_candidate(&cursor,&last,utc,&captured);
        bool stored = candidate && ready && queue_record(&last,true,captured,false,TB_LITE_NO_EVENT);
        if (stored || (candidate && !publish)) cursor = (tb_lite_cursor_t){last.boot_id,last.local_seq,captured};
        bool health = publish && last.boot_id && utc && (event != TB_LITE_NO_EVENT || now >= health_due);
        bool health_stored = health && ready && queue_record(&last,online,utc,true,event);
        if (now >= health_due) {
            health_due = now + TB_LITE_HEALTH_MS;
            if (ready) update_space();
        }
        portENTER_CRITICAL(&s_mux);
        s_state.received += online;
        s_state.rejected += !online;
        if (online) s_state.last_rx_ms = now;
        s_state.core_online = online;
        s_state.core_boot = last.boot_id;
        s_state.guard = last.guard;
        s_state.frames = last.frames;
        s_state.core_heap = last.heap_minimum;
        s_state.parked_sample_ms = online && (last.valid & TB_LITE_BIT(TB_LITE_SPEED)) &&
            last.values[TB_LITE_SPEED] >= 0 && last.values[TB_LITE_SPEED] <= 0.1 ? now : 0;
        s_state.durable += stored + health_stored;
        s_state.discarded += (publish && candidate && !stored) + (health && !health_stored);
        if (online && !candidate && publish && !utc) s_state.discarded++;
        portEXIT_CRITICAL(&s_mux);
    }
}

static esp_err_t send_record(const char *record, uint64_t sequence, bool health, int *status)
{
    const tb_config_t *config = tb_config();
    cJSON *token = cJSON_CreateString(config->uplink_token);
    if (!token) return ESP_ERR_NO_MEM;
    char sig[sizeof(config->uplink_token)*6 + 16];
    bool encoded = cJSON_PrintPreallocated(token,sig,sizeof(sig),false);
    cJSON_Delete(token);
    if (!encoded) return ESP_ERR_INVALID_SIZE;
    char body[TB_LITE_RECORD_MAX+sizeof(sig)+120];
    int length = snprintf(body,sizeof(body),"{\"device_uid\":\"%s\",\"token\":%s,\"samples\":[%s]}",config->cloud_device_uid,sig,record);
    if (length <= 0 || (size_t)length >= sizeof(body)) return ESP_ERR_INVALID_SIZE;
    return tb_link_collector_post(body,(size_t)length,sequence,health,status);
}

static void uploader_task(void *unused)
{
    (void)unused;
    uint32_t wait_ms = 1000;
    /* Leave headroom for the boot-time OTA image validation. */
    vTaskDelay(pdMS_TO_TICKS(18000));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
        if (tb_web_ota_busy() || !epoch_ms() || esp_get_free_heap_size()<46000u) continue;
        char record[TB_LITE_RECORD_MAX];
        if (xSemaphoreTake(s_filesystem,pdMS_TO_TICKS(100)) != pdTRUE) continue;
        int read = s_outbox.ready && s_meta_valid ? tb_gps_outbox_peek(&s_outbox,record,sizeof(record)) : ENOENT;
        if (read && read != ENOENT) {
            s_outbox.ready = false;
            storage_state("journal_read_failed",read);
        }
        xSemaphoreGive(s_filesystem);
        if (read) { wait_ms = 1000; continue; }
        bool health;
        uint64_t sequence = 0;
        int status = 0;
        esp_err_t result = tb_lite_record_meta(record,&sequence,&health)
            ? send_record(record,sequence,health,&status) : ESP_ERR_INVALID_RESPONSE;
        /* Complete this peek under the same lock before offering another item. */
        xSemaphoreTake(s_filesystem,portMAX_DELAY);
        int ack_error = result == ESP_OK ? tb_gps_outbox_ack(&s_outbox) : 0;
        if (result != ESP_OK || ack_error) tb_gps_outbox_retry(&s_outbox);
        if (ack_error) storage_state("journal_ack_failed",ack_error);
        if (result == ESP_OK && !ack_error) storage_state("ready",0);
        xSemaphoreGive(s_filesystem);
        portENTER_CRITICAL(&s_mux);
        s_state.http_status = status;
        s_state.uplink_error = result;
        if (result == ESP_OK && !ack_error) { s_state.last_ack=sequence; s_state.delivered++; }
        if (result == ESP_ERR_INVALID_RESPONSE) s_state.invalid_ack++;
        if (ack_error) s_state.writes_failed++;
        portEXIT_CRITICAL(&s_mux);
        wait_ms = result == ESP_OK && !ack_error ? 1000 : wait_ms < 15000 ? wait_ms*2 : 30000;
    }
}

esp_err_t tb_link_collector_start(SemaphoreHandle_t filesystem_lock)
{
    s_filesystem = filesystem_lock;
    const tb_config_t *config = tb_config();
    const bool publish = publisher_configured(config);
    s_state.phase = publish ? "waiting_filesystem" : config->lite_upload_enabled ? "configuration_required" : "shadow";
    if (publish && xTaskCreate(uploader_task,"tb_lite_https",8192,NULL,3,NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return xTaskCreate(collector_task,"tb_lite_poll",6144,NULL,3,NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t tb_link_collector_reset_reference(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) return ESP_OK;
    cJSON *body = NULL;
    esp_err_t err = tb_http_read_json(request,128,&body);
    if (err != ESP_OK) return err;
    bool authorized = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"authorized")) &&
                      cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"stationary"));
    cJSON_Delete(body);
    if (!authorized || tb_web_ota_busy() || xSemaphoreTake(s_filesystem,pdMS_TO_TICKS(100)) != pdTRUE)
        return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"park and authorize the new Link reference");
    uint64_t now = ticks_ms(), utc = epoch_ms();
    portENTER_CRITICAL(&s_mux);
    uint64_t parked = s_state.parked_sample_ms;
    portEXIT_CRITICAL(&s_mux);
    err = ESP_ERR_INVALID_STATE;
    if (s_meta_valid && s_outbox.ready && utc && parked && now>=parked && now-parked<=12000u) {
        publisher_t candidate = s_meta;
        candidate.reference = (tb_lite_reference_t){0};
        candidate.reference_after_ms = utc;
        err = save_publisher(&candidate);
        storage_state(err==ESP_OK?"ready":"sequence_unavailable",err);
    }
    xSemaphoreGive(s_filesystem);
    if (err != ESP_OK) return httpd_resp_send_err(request,HTTPD_400_BAD_REQUEST,"need fresh stationary Core, clock and writable Link metadata");
    httpd_resp_set_type(request,"application/json");
    return httpd_resp_sendstr(request,"{\"ok\":true,\"reference\":\"waiting_new_reading\"}");
}

void tb_link_collector_json(cJSON *root)
{
    portENTER_CRITICAL(&s_mux);
    collector_state_t view = s_state;
    portEXIT_CRITICAL(&s_mux);
    cJSON *o = cJSON_AddObjectToObject(root,"telemetry_collector");
    if (!o) return;
    cJSON_AddStringToObject(o,"phase",view.phase ? view.phase : "starting");
    cJSON_AddBoolToObject(o,"publishing",publisher_configured(tb_config()));
    cJSON_AddBoolToObject(o,"core_online",view.core_online);
    cJSON_AddBoolToObject(o,"storage_ready",view.ready);
    cJSON_AddNumberToObject(o,"storage_error",view.storage_error);
    cJSON_AddNumberToObject(o,"sequence_floor",(double)tb_config()->sequence_floor);
    cJSON_AddNumberToObject(o,"outbox_budget_bytes",JOURNAL_BUDGET);
    cJSON_AddNumberToObject(o,"outbox_bytes",(double)view.bytes);
    cJSON_AddNumberToObject(o,"filesystem_bytes",(double)view.total_bytes);
    cJSON_AddNumberToObject(o,"filesystem_free_bytes",(double)view.free_bytes);
    cJSON_AddNumberToObject(o,"pending",(double)view.pending);
    cJSON_AddNumberToObject(o,"last_durable_ack",(double)view.last_ack);
    cJSON_AddNumberToObject(o,"http_status",view.http_status);
    cJSON_AddStringToObject(o,"uplink_error",esp_err_to_name(view.uplink_error));
    cJSON_AddNumberToObject(o,"invalid_acks",view.invalid_ack);
    cJSON_AddNumberToObject(o,"ack_io_errors",view.writes_failed);
    cJSON_AddNumberToObject(o,"received",view.received);
    cJSON_AddNumberToObject(o,"poll_failures",view.rejected);
    cJSON_AddNumberToObject(o,"durable",view.durable);
    cJSON_AddNumberToObject(o,"delivered",view.delivered);
    cJSON_AddNumberToObject(o,"discarded",view.discarded);
    cJSON_AddNumberToObject(o,"core_boot",view.core_boot);
    cJSON_AddNumberToObject(o,"core_frames",view.frames);
    cJSON_AddNumberToObject(o,"core_guard",view.guard);
    cJSON_AddNumberToObject(o,"core_heap_minimum",view.core_heap);
    cJSON_AddNumberToObject(o,"reference_unix_ms",(double)view.reference_ms);
    if (view.last_rx_ms) cJSON_AddNumberToObject(o,"core_last_rx_age_ms",(double)(ticks_ms()-view.last_rx_ms));
}
