#include "core_internal.h"
#include "telemetry.h"
#include "tbx_j1939.h"
#include "tbx_j1587_bridge.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"

#define LIVE_CLIENTS 4
#define MAX_SAFE_SEQUENCE UINT64_C(9007199254740991)
/* Drain faster after ACK rather than retaining a large JSON tree during a
 * potentially 45-second HTTP wait. Decoder and durable queue sizes are unchanged. */
#define BRIDGE_BATCH_LIMIT 8U
#define BRIDGE_BATCH_BYTES 8192U
static const char *TAG = "tb_core";
typedef struct { char line[128]; size_t length; } live_line_t;
typedef struct {
    bool valid, have_speed, have_bearing, have_altitude, have_accuracy, have_time;
    double lat, lon, speed, bearing, altitude, accuracy;
    uint64_t time_ms, received_us, accepted_us;
    char source[20];
} gps_fix_t;
static struct {
    QueueHandle_t live;
    SemaphoreHandle_t lock;
    uint32_t live_dropped, live_sent, live_rejected;
    volatile uint32_t live_clients; /* shared with capture task: fast empty-client check */
    uint32_t uplink_attempts, uplink_success;
    char uplink_error[80];
    uint64_t uplink_at, link_at;
    cJSON *link_status;
    gps_fix_t gps;
} s;

uint64_t tb_clock_milliseconds(void)
{
    struct timeval now;
    gettimeofday(&now, NULL);
    if (now.tv_sec >= 1704067200) return (uint64_t)now.tv_sec * 1000 + now.tv_usec / 1000;
    return esp_timer_get_time() / 1000;
}

static void json_optional_number(cJSON *object, const char *key, bool available, double value)
{
    if (available) cJSON_AddNumberToObject(object, key, value);
}

bool tb_core_append_gps(cJSON *sample)
{
    return tb_core_append_gps_at(sample, esp_timer_get_time());
}

bool tb_core_append_gps_at(cJSON *sample, uint64_t captured_us)
{
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    gps_fix_t fix = s.gps;
    xSemaphoreGive(s.lock);
    if (!fix.valid || captured_us < fix.accepted_us || captured_us < fix.received_us ||
        captured_us - fix.received_us > UINT64_C(15000000)) return false;
    cJSON_AddStringToObject(sample, "gps_status", "FIX");
    cJSON_AddBoolToObject(sample, "has_fix", true);
    cJSON_AddNumberToObject(sample, "lat", fix.lat);
    cJSON_AddNumberToObject(sample, "lon", fix.lon);
    cJSON_AddStringToObject(sample, "gps_source", fix.source);
    json_optional_number(sample, "gps_speed_kmh", fix.have_speed, fix.speed);
    if (fix.have_speed && !cJSON_GetObjectItemCaseSensitive(sample, "speed_kmh")) {
        cJSON_AddNumberToObject(sample, "speed_kmh", fix.speed);
        cJSON_AddStringToObject(sample, "speed_source", fix.source);
    }
    json_optional_number(sample, "heading_deg", fix.have_bearing, fix.bearing);
    json_optional_number(sample, "altitude_m", fix.have_altitude, fix.altitude);
    json_optional_number(sample, "gps_accuracy_m", fix.have_accuracy, fix.accuracy);
    if (fix.have_time) cJSON_AddNumberToObject(sample, "gps_time_ms", (double)fix.time_ms);
    return true;
}

void tb_live_can(uint32_t id, bool extended, uint8_t dlc, const uint8_t data[8])
{
    /* Never format/queue a TCP RAW frame without an actual consumer. */
    if (!s.live || !s.live_clients || !extended || dlc > 8) return;
    live_line_t line;
    char hex[17];
    for (unsigned i = 0; i < dlc; ++i) snprintf(hex + i * 2, 3, "%02X", data[i]);
    hex[dlc * 2] = 0;
    uint32_t pgn = (id >> 8) & 0x3ffff;
    if (((id >> 16) & 255) < 240) pgn &= 0x3ff00;
    int length = snprintf(line.line, sizeof(line.line), "TB1,%" PRIu64 ",%08" PRIX32 ",%" PRIu32 ",%" PRIu32 ",%u,%s\n",
        tb_clock_milliseconds(), id, pgn, id & 255, dlc, hex);
    if (length <= 0 || (size_t)length >= sizeof(line.line)) return;
    line.length = length;
    if (xQueueSend(s.live, &line, 0) != pdTRUE) ++s.live_dropped;
}

static void live_task(void *unused)
{
    int clients[LIVE_CLIENTS];
    for (unsigned i = 0; i < LIVE_CLIENTS; ++i) clients[i] = -1;
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0) { ESP_LOGE(TAG, "TCP socket failed"); vTaskDelete(NULL); return; }
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    fcntl(listener, F_SETFL, O_NONBLOCK);
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_port = htons(35000), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(listener, LIVE_CLIENTS) < 0) {
        ESP_LOGE(TAG, "TCP listen failed"); close(listener); vTaskDelete(NULL); return;
    }
    live_line_t line;
    for (;;) {
        int incoming;
        while ((incoming = accept(listener, NULL, NULL)) >= 0) {
            fcntl(incoming, F_SETFL, O_NONBLOCK);
            bool accepted = false;
            for (unsigned i = 0; i < LIVE_CLIENTS; ++i) if (clients[i] < 0) {
                clients[i] = incoming; ++s.live_clients; accepted = true; break;
            }
            if (!accepted) { ++s.live_rejected; close(incoming); }
        }
        if (xQueueReceive(s.live, &line, pdMS_TO_TICKS(25)) != pdTRUE) continue;
        for (unsigned i = 0; i < LIVE_CLIENTS; ++i) if (clients[i] >= 0) {
            int written = send(clients[i], line.line, line.length, MSG_DONTWAIT);
            /* Disconnect slow clients rather than stalling capture or emitting partial CSV lines. */
            if (written != (int)line.length) { close(clients[i]); clients[i] = -1; --s.live_clients; }
            else ++s.live_sent;
        }
    }
}

typedef struct { char bytes[2048]; size_t length; bool overflow; } response_t;
static esp_err_t response_event(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && response) {
        if (event->data_len < 0 || response->length + event->data_len >= sizeof(response->bytes)) {
            response->overflow = true; return ESP_FAIL;
        }
        memcpy(response->bytes + response->length, event->data, event->data_len);
        response->length += event->data_len;
        response->bytes[response->length] = 0;
    }
    return ESP_OK;
}

static bool numeric_sequence(const cJSON *number, uint64_t *sequence)
{
    if (!cJSON_IsNumber(number) || !isfinite(number->valuedouble) || number->valuedouble < 0 ||
        number->valuedouble > (double)MAX_SAFE_SEQUENCE) return false;
    uint64_t n = (uint64_t)number->valuedouble;
    if ((double)n != number->valuedouble) return false;
    *sequence = n;
    return true;
}

typedef struct {
    char *payload;
    uint64_t sequences[BRIDGE_BATCH_LIMIT];
    size_t count;
} bridge_batch_t;

/* Always consumes batch, including errors. Only the compact wire payload and
 * exact offered sequence IDs survive; no cJSON tree crosses the network wait. */
static bool prepare_bridge_batch(cJSON *batch, bridge_batch_t *prepared)
{
    memset(prepared, 0, sizeof(*prepared));
    const cJSON *samples = cJSON_GetObjectItemCaseSensitive(batch, "samples");
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(batch, "count");
    bool valid = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(batch, "ok")) &&
        cJSON_IsArray(samples) && cJSON_IsNumber(count);
    const cJSON *sample;
    cJSON_ArrayForEach(sample, samples) {
        uint64_t candidate;
        if (!valid || prepared->count >= BRIDGE_BATCH_LIMIT ||
            !numeric_sequence(cJSON_GetObjectItemCaseSensitive(sample, "seq"), &candidate) ||
            (prepared->count && candidate <= prepared->sequences[prepared->count - 1])) {
            valid = false;
            break;
        }
        prepared->sequences[prepared->count++] = candidate;
    }
    valid = valid && prepared->count > 0 && count->valuedouble == (double)prepared->count;
    if (valid) prepared->payload = cJSON_PrintUnformatted(batch);
    cJSON_Delete(batch);
    if (prepared->payload && strlen(prepared->payload) <= BRIDGE_BATCH_BYTES) return true;
    cJSON_free(prepared->payload);
    memset(prepared, 0, sizeof(*prepared));
    return false;
}

static bool batch_contains(const bridge_batch_t *batch, uint64_t sequence)
{
    for (size_t i = 0; i < batch->count; ++i)
        if (batch->sequences[i] == sequence) return true;
    return false;
}

static void bridge_task(void *unused)
{
    uint32_t delay_ms = 5000;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        if (tb_web_ota_busy() || !tb_config()->pair_key[0]) continue;
        cJSON *batch = tb_store_batch(BRIDGE_BATCH_LIMIT, BRIDGE_BATCH_BYTES);
        if (!batch) continue;
        bridge_batch_t prepared;
        if (!prepare_bridge_batch(batch, &prepared)) continue;
        response_t response = {0};
        char link_host[16], uplink_url[64];
        tb_network_link_host(link_host, sizeof(link_host));
        snprintf(uplink_url, sizeof(uplink_url), "http://%s/api/uplink", link_host);
        esp_http_client_config_t config = { .url = uplink_url, .method = HTTP_METHOD_POST,
            .timeout_ms = 45000, .event_handler = response_event, .user_data = &response, .buffer_size = 1024,
            .disable_auto_redirect = true };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = ESP_ERR_NO_MEM;
        int status = 0;
        if (client) {
            char authorization[80];
            snprintf(authorization, sizeof(authorization), "Bearer %s", tb_config()->pair_key);
            esp_http_client_set_header(client, "Authorization", authorization);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, prepared.payload, strlen(prepared.payload));
            ++s.uplink_attempts;
            err = esp_http_client_perform(client);
            status = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);
        }
        /* HTTP is complete. The ACK guard needs sequence IDs, not payload text. */
        cJSON_free(prepared.payload);
        prepared.payload = NULL;
        bool acknowledged = false;
        if (err == ESP_OK && status >= 200 && status < 300 && !response.overflow) {
            cJSON *result = cJSON_Parse(response.bytes);
            uint64_t ack;
            if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ok")) &&
                numeric_sequence(cJSON_GetObjectItemCaseSensitive(result, "ack_seq"), &ack) &&
                batch_contains(&prepared, ack) && tb_store_ack(ack) == ESP_OK) acknowledged = true;
            cJSON_Delete(result);
        }
        xSemaphoreTake(s.lock, portMAX_DELAY);
        s.uplink_at = esp_timer_get_time();
        if (acknowledged) { ++s.uplink_success; s.uplink_error[0] = 0; delay_ms = 100; }
        else {
            snprintf(s.uplink_error, sizeof(s.uplink_error), "link_http:%d %s%s", status, esp_err_to_name(err),
                response.overflow ? " response_too_large" : " no_valid_ack");
            delay_ms = delay_ms < 30000 ? delay_ms * 2 : 60000;
            if (delay_ms < 5000) delay_ms = 5000;
            if (delay_ms > 60000) delay_ms = 60000;
        }
        xSemaphoreGive(s.lock);
    }
}

static esp_err_t batch_handler(httpd_req_t *request)
{
    char query[80], value[20];
    size_t limit = 8;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
        char *end; unsigned long n = strtoul(value, &end, 10);
        if (!*value || *end || n < 1 || n > 32) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid limit");
        limit = n;
    }
    cJSON *batch = tb_store_batch(limit, 24576);
    if (!batch) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "storage busy");
    }
    return tb_http_send_json(request, batch);
}

static esp_err_t read_form(httpd_req_t *request, char *body, size_t capacity)
{
    if (request->content_len <= 0 || (size_t)request->content_len >= capacity) return ESP_ERR_INVALID_SIZE;
    size_t used = 0;
    unsigned timeouts = 0;
    while (used < (size_t)request->content_len) {
        int got = httpd_req_recv(request, body + used, request->content_len - used);
        if (got == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 4) continue;
        if (got <= 0) return ESP_FAIL;
        used += got;
    }
    body[used] = 0;
    return ESP_OK;
}

/* Preserve native Android compatibility while rejecting browser cross-site
 * form writes to the legacy unauthenticated gateway endpoints. */
static bool legacy_write_origin_ok(httpd_req_t *request)
{
    size_t length = httpd_req_get_hdr_value_len(request, "Origin");
    if (!length) return true;
    char origin[192], host[128], expected[160];
    if (length >= sizeof(origin) ||
        httpd_req_get_hdr_value_str(request, "Origin", origin, sizeof(origin)) != ESP_OK ||
        httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host)) != ESP_OK) return false;
    snprintf(expected, sizeof(expected), "http://%s", host);
    if (!strcmp(origin, expected)) return true;
    snprintf(expected, sizeof(expected), "https://%s", host);
    return !strcmp(origin, expected);
}

static esp_err_t ack_handler(httpd_req_t *request)
{
    if (!legacy_write_origin_ok(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "cross-site write rejected");
    char content_type[80] = {0};
    httpd_req_get_hdr_value_str(request, "Content-Type", content_type, sizeof(content_type));
    uint64_t sequence = 0;
    bool valid = false;
    if (!strncmp(content_type, "application/json", 16)) {
        cJSON *root = NULL;
        if (tb_http_read_json(request, 256, &root) == ESP_OK) valid = numeric_sequence(cJSON_GetObjectItemCaseSensitive(root, "ack_seq"), &sequence);
        cJSON_Delete(root);
    } else {
        char body[256], value[32];
        if (read_form(request, body, sizeof(body)) == ESP_OK &&
            httpd_query_key_value(body, "ack_seq", value, sizeof(value)) == ESP_OK &&
            value[0] >= '0' && value[0] <= '9') {
            char *end; errno = 0; sequence = strtoull(value, &end, 10);
            valid = !errno && !*end && sequence <= MAX_SAFE_SEQUENCE;
        }
    }
    if (!valid || tb_store_ack(sequence) != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid or unoffered ack_seq");
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true); cJSON_AddNumberToObject(result, "ack_seq", sequence);
    return tb_http_send_json(request, result);
}

static bool optional_json_number(const cJSON *object, const char *name, double minimum, double maximum, double *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < minimum || item->valuedouble > maximum) return false;
    *value = item->valuedouble; return true;
}

static bool gps_from_json(const cJSON *gps, gps_fix_t *fix, const char *source)
{
    memset(fix, 0, sizeof(*fix));
    if (!cJSON_IsObject(gps) || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(gps, "valid"))) return false;
    if (!optional_json_number(gps, "lat", -90, 90, &fix->lat) || !optional_json_number(gps, "lon", -180, 180, &fix->lon)) return false;
    fix->valid = true;
    fix->have_speed = optional_json_number(gps, "speed_kmh", 0, 400, &fix->speed);
    fix->have_bearing = optional_json_number(gps, "bearing_deg", 0, 360, &fix->bearing);
    fix->have_altitude = optional_json_number(gps, "altitude_m", -1000, 20000, &fix->altitude);
    fix->have_accuracy = optional_json_number(gps, "accuracy_m", 0, 100000, &fix->accuracy);
    double time_ms;
    fix->have_time = optional_json_number(gps, "time_ms", 1704067200000.0, 4102444800000.0, &time_ms);
    if (fix->have_time) fix->time_ms = time_ms;
    fix->received_us = esp_timer_get_time();
    fix->accepted_us = fix->received_us;
    double age_ms;
    if (optional_json_number(gps, "age_ms", 0, 15000, &age_ms)) {
        uint64_t age_us = (uint64_t)age_ms * 1000;
        fix->received_us = fix->received_us > age_us ? fix->received_us - age_us : 0;
    }
    snprintf(fix->source, sizeof(fix->source), "%s", source);
    return true;
}

static void accept_gps(const gps_fix_t *fix)
{
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.gps = *fix;
    xSemaphoreGive(s.lock);
    if (fix->have_time) {
        uint64_t current_ms = fix->time_ms + ((uint64_t)esp_timer_get_time() - fix->received_us) / 1000;
        struct timeval utc = { .tv_sec = current_ms / 1000, .tv_usec = (current_ms % 1000) * 1000 };
        settimeofday(&utc, NULL);
    }
}

static esp_err_t link_telemetry_handler(httpd_req_t *request)
{
    if (!tb_http_require_pair(request)) return ESP_OK;
    cJSON *root = NULL;
    esp_err_t err = tb_http_read_json(request, TB_RECORD_MAX - 1, &root);
    if (err != ESP_OK || !cJSON_IsObject(root)) { cJSON_Delete(root); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid telemetry"); }
    /* Learn only the authenticated packet's socket peer, never a host supplied in JSON. */
    tb_network_note_link_socket(httpd_req_to_sockfd(request));
    gps_fix_t fix;
    const cJSON *gps = cJSON_GetObjectItemCaseSensitive(root, "gps");
    if (gps_from_json(gps, &fix, "link_gnss")) accept_gps(&fix);
    else if (cJSON_IsObject(gps) && cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(gps, "valid"))) {
        xSemaphoreTake(s.lock, portMAX_DELAY);
        if (!strcmp(s.gps.source, "link_gnss")) s.gps.valid = false;
        xSemaphoreGive(s.lock);
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    cJSON_Delete(s.link_status); s.link_status = root; s.link_at = esp_timer_get_time();
    xSemaphoreGive(s.lock);
    cJSON *reply = cJSON_CreateObject(); cJSON_AddBoolToObject(reply, "ok", true);
    return tb_http_send_json(request, reply);
}

static bool form_number(const char *body, const char *name, double minimum, double maximum, double *value)
{
    char text[80];
    if (httpd_query_key_value(body, name, text, sizeof(text)) != ESP_OK || !text[0]) return false;
    char *end; errno = 0; double n = strtod(text, &end);
    if (errno || *end || !isfinite(n) || n < minimum || n > maximum) return false;
    *value = n; return true;
}

static esp_err_t gps_handler(httpd_req_t *request)
{
    if (!legacy_write_origin_ok(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "cross-site write rejected");
    char body[1024];
    if (read_form(request, body, sizeof(body)) != ESP_OK) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid GPS form");
    gps_fix_t fix = { .valid = true, .received_us = esp_timer_get_time() };
    fix.accepted_us = fix.received_us;
    if (!form_number(body, "lat", -90, 90, &fix.lat) || !form_number(body, "lon", -180, 180, &fix.lon))
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid coordinates");
    fix.have_speed = form_number(body, "speed_kmh", 0, 400, &fix.speed);
    fix.have_bearing = form_number(body, "bearing_deg", 0, 360, &fix.bearing);
    fix.have_altitude = form_number(body, "altitude_m", -1000, 20000, &fix.altitude);
    fix.have_accuracy = form_number(body, "accuracy_m", 0, 100000, &fix.accuracy);
    double time_ms;
    fix.have_time = form_number(body, "time_ms", 1704067200000.0, 4102444800000.0, &time_ms);
    if (fix.have_time) fix.time_ms = time_ms;
    snprintf(fix.source, sizeof(fix.source), "android");
    accept_gps(&fix);
    cJSON *reply = cJSON_CreateObject(); cJSON_AddBoolToObject(reply, "ok", true);
    return tb_http_send_json(request, reply);
}

/* Intentional destructive operation. This is the new CORE, not the LINK.
 * Refuse until the OTA image is marked VALID (main.c confirms after 15 s). */
static esp_err_t discard_storage_handler(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) return ESP_OK;
    if (tb_web_ota_busy()) { httpd_resp_set_status(request, "409 Conflict"); return httpd_resp_sendstr(request, "OTA in progress"); }
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t image_state = ESP_OTA_IMG_UNDEFINED;
    if (strcmp(tb_version(), "0.2.4") != 0 || !running ||
        esp_ota_get_state_partition(running, &image_state) != ESP_OK ||
        image_state != ESP_OTA_IMG_VALID) {
        { httpd_resp_set_status(request, "409 Conflict"); return httpd_resp_sendstr(request, "Confirm CORE 0.2.4 OTA first (wait 15s), then discard local logs"); }
    }
    cJSON *body = NULL;
    if (tb_http_read_json(request, 256, &body) != ESP_OK || !cJSON_IsObject(body)) {
        cJSON_Delete(body);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid JSON");
    }
    const cJSON *phrase = cJSON_GetObjectItemCaseSensitive(body, "confirm");
    bool authorized = cJSON_IsString(phrase) &&
        !strcmp(phrase->valuestring, "DESCARTAR_REGISTROS");
    cJSON_Delete(body);
    if (!authorized)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "explicit confirmation required");
    esp_err_t scheduled = tb_store_schedule_discard();
    if (scheduled != ESP_OK)
        { httpd_resp_set_status(request, "409 Conflict"); return httpd_resp_sendstr(request, "storage not ready"); }
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddStringToObject(reply, "action", "discard_local_telemetry_at_next_boot");
    cJSON_AddStringToObject(reply, "scope", "storage_log_files_only");
    cJSON_AddBoolToObject(reply, "ota_and_nvs_preserved", true);
    tb_schedule_restart(1500);
    return tb_http_send_json(request, reply);
}

static esp_err_t reset_handler(httpd_req_t *request)
{
    if (!legacy_write_origin_ok(request)) return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "cross-site write rejected");
    if (request->content_len) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "expected empty body");
    tb_capture_reset_trip();
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", true); cJSON_AddStringToObject(reply, "reset", "trip_counters");
    return tb_http_send_json(request, reply);
}

static esp_err_t sensors_handler(httpd_req_t *request)
{
    char query[96], value[20];
    size_t offset = 0, limit = 16;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "offset", value, sizeof(value)) == ESP_OK) {
            char *end; unsigned long n = strtoul(value, &end, 10);
            if (!value[0] || *end || n > 192) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid offset");
            offset = n;
        }
        if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
            char *end; unsigned long n = strtoul(value, &end, 10);
            if (!value[0] || *end || n < 1 || n > 32) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid limit");
            limit = n;
        }
    }
    return tb_http_send_json(request, tb_telemetry_sensors_json(offset, limit));
}

static esp_err_t sensor_catalog_handler(httpd_req_t *request)
{
    char query[80], value[20];
    size_t offset = 0, limit = 16, count;
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "offset", value, sizeof(value)) == ESP_OK) {
            char *end; unsigned long n = strtoul(value, &end, 10);
            if (!value[0] || *end || n > TBX_J1939_SIGNAL_COUNT) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid offset");
            offset = n;
        }
        if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
            char *end; unsigned long n = strtoul(value, &end, 10);
            if (!value[0] || *end || n < 1 || n > 32) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid limit");
            limit = n;
        }
    }
    const tbx_j1939_signal_t *catalog = tbx_j1939_catalog(&count);
    cJSON *result = cJSON_CreateObject();
    if (!result) return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(result, "schema_version", 2);
    cJSON_AddNumberToObject(result, "total", count);
    cJSON_AddNumberToObject(result, "offset", offset);
    cJSON *signals = cJSON_AddArrayToObject(result, "signals");
    for (size_t i = offset; i < count && i < offset + limit; ++i) {
        const tbx_j1939_signal_t *spec = &catalog[i];
        cJSON *row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "sid", spec->id);
        cJSON_AddNumberToObject(row, "pgn", spec->pgn);
        cJSON_AddStringToObject(row, "name", spec->name);
        cJSON_AddStringToObject(row, "unit", spec->unit);
        cJSON_AddStringToObject(row, "validation", tbx_j1939_validation_name(spec->validation));
        cJSON_AddBoolToObject(row, "implemented", spec->implemented);
        if (!spec->implemented) cJSON_AddStringToObject(row, "quality", "SEM_FORMULA_DOCUMENTADA");
        cJSON_AddBoolToObject(row, "proprietary", spec->proprietary);
        cJSON_AddBoolToObject(row, "derived", spec->derived);
        cJSON_AddStringToObject(row, "evidence", spec->evidence);
        cJSON_AddItemToArray(signals, row);
    }
    return tb_http_send_json(request, result);
}

static esp_err_t requests_handler(httpd_req_t *request)
{
    if (!tb_http_require_admin(request)) return ESP_OK;
    cJSON *body = NULL;
    if (tb_http_read_json(request, 2048, &body) != ESP_OK || !cJSON_IsObject(body)) {
        cJSON_Delete(body); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(body, "action");
    bool stop = cJSON_IsString(action) && !strcmp(action->valuestring, "stop");
    bool arm = cJSON_IsString(action) && !strcmp(action->valuestring, "arm");
    if (!stop && !arm) { cJSON_Delete(body); return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "action must be arm or stop"); }
    if (arm) {
        char error[160] = {0};
        if (!tb_capture_request_configure(body, error, sizeof(error))) {
            cJSON_Delete(body);
            cJSON *reply = cJSON_CreateObject();
            cJSON_AddBoolToObject(reply, "ok", false);
            cJSON_AddStringToObject(reply, "error", error);
            httpd_resp_set_status(request, "409 Conflict");
            return tb_http_send_json(request, reply);
        }
    }
    bool accepted = tb_capture_request_control(arm,
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "authorized")),
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "stationary")));
    cJSON_Delete(body);
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddBoolToObject(reply, "ok", accepted);
    cJSON_AddBoolToObject(reply, "armed", arm && accepted);
    cJSON_AddBoolToObject(reply, "hardware_tx_available", true);
    cJSON_AddStringToObject(reply, "reason", arm ? (accepted ? "stationary_test_authorized" : "configuration_or_safety_interlock") : "operator_stop");
    if (!accepted) httpd_resp_set_status(request, "409 Conflict");
    return tb_http_send_json(request, reply);
}

esp_err_t tb_application_register_routes(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        { .uri = "/api/sensors", .method = HTTP_GET, .handler = sensors_handler },
        { .uri = "/api/sensors/catalog", .method = HTTP_GET, .handler = sensor_catalog_handler },
        { .uri = "/api/j1708/requests", .method = HTTP_POST, .handler = requests_handler },
        { .uri = "/api/gateway/batch", .method = HTTP_GET, .handler = batch_handler },
        { .uri = "/api/gateway/ack", .method = HTTP_POST, .handler = ack_handler },
        { .uri = "/api/link/telemetry", .method = HTTP_POST, .handler = link_telemetry_handler },
        { .uri = "/api/gps", .method = HTTP_POST, .handler = gps_handler },
        { .uri = "/api/reset", .method = HTTP_POST, .handler = reset_handler },
        { .uri = "/api/storage/discard", .method = HTTP_POST, .handler = discard_storage_handler },
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t tb_application_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    s.live = xQueueCreate(64, sizeof(live_line_t));
    if (!s.lock || !s.live) return ESP_ERR_NO_MEM;
    esp_err_t err = tb_store_init();
    if (err != ESP_OK) return err;
    err = tb_capture_start();
    if (err != ESP_OK) return err;
    if (xTaskCreate(live_task, "tb_tcp", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate(bridge_task, "tb_bridge", 8192, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void tb_application_add_json(cJSON *object)
{
    tb_capture_add_json(object);
    tb_store_add_json(object);
    cJSON *live = cJSON_AddObjectToObject(object, "live_stream");
    cJSON_AddNumberToObject(live, "port", 35000);
    cJSON_AddNumberToObject(live, "clients", s.live_clients);
    cJSON_AddNumberToObject(live, "frames_sent", s.live_sent);
    cJSON_AddNumberToObject(live, "queue_dropped", s.live_dropped);
    cJSON_AddNumberToObject(live, "clients_rejected", s.live_rejected);
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    cJSON *bridge = cJSON_AddObjectToObject(object, "bridge");
    cJSON_AddNumberToObject(bridge, "attempts", s.uplink_attempts);
    cJSON_AddNumberToObject(bridge, "successful_acks", s.uplink_success);
    cJSON_AddStringToObject(bridge, "error", s.uplink_error);
    if (s.link_at) cJSON_AddNumberToObject(bridge, "last_telemetry_age_ms", (esp_timer_get_time() - s.link_at) / 1000);
    if (s.link_status) cJSON_AddItemToObject(object, "link", cJSON_Duplicate(s.link_status, true));
    xSemaphoreGive(s.lock);
    cJSON *gps = cJSON_AddObjectToObject(object, "gps");
    bool valid = tb_core_append_gps(gps);
    cJSON_AddBoolToObject(gps, "valid", valid);
    if (!valid) { cJSON_AddStringToObject(gps, "gps_status", "NO_FIX"); cJSON_AddBoolToObject(gps, "has_fix", false); }
}
