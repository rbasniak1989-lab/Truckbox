#include "core_internal.h"
#include "protocols.h"
#include "telemetry.h"
#include "telemetry_profile.h"
#include "tbx_j1587_bridge.h"
#include "tbx_j1708_active.h"
#include "tbx_trip.h"
#include "tbx_fuel.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/twai.h"
#include "driver/uart.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "tb_capture";
static struct {
    SemaphoreHandle_t lock, trip_persist_lock;
    nvs_handle_t nvs;
    bool nvs_open, can_ready, j1708_ready;
    char can_error[64], j1708_error[64];
    uint32_t can_frames, can_rtr, can_errors, j1708_frames, j1708_checksum_errors, j1708_oversize, uart_overruns;
    uint64_t speed_at, rpm_at, fuel_at, total_at;
    uint64_t speed_can_at, rpm_can_at, fuel_can_at, total_can_at, distance_counter_at;
    bool distance_counter_valid;
    uint8_t distance_counter_sa;
    uint32_t distance_counter_raw;
    tbx_j1708_request_scheduler_t requests;
    tbx_j1708_active_t active;
    uint8_t observed_mid[32];
    volatile bool emergency_stop;
    tbx_trip_t trip;
    double speed, rpm, fuel_rate, distance_total;
    double distance_trip, fuel_trip;
    bool distance_trip_valid, fuel_trip_valid;
    QueueHandle_t uart_events;
    bool speed_j1587, rpm_j1587, fuel_j1587, total_j1587;
    uint8_t fuel_address;
    tbx_fuel_t fuel_model;
    uint64_t fuel_model_at;
    bool fuel_derived;
    char j1708_last[TB_RECORD_MAX];
    uint64_t j1708_at;
} s;

static void request_abort_tx(void *unused)
{
    (void)unused;
    /* Program the GPIO latch first, then detach the UART matrix. With DI
     * fixed LOW this releases the dominant driver even during a byte. */
    gpio_set_level(GPIO_NUM_21, 0);
    esp_rom_gpio_connect_out_signal(GPIO_NUM_21, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
    if (s.j1708_ready) (void)uart_set_rx_full_threshold(UART_NUM_1, 64);
}

static bool request_begin_tx(const uint8_t *bytes, size_t length, void *unused)
{
    (void)unused;
    size_t rx_buffered = 0;
    if (s.emergency_stop || !s.j1708_ready || !bytes || length != 5 ||
        gpio_get_level(GPIO_NUM_20) == 0 ||
        uart_wait_tx_done(UART_NUM_1, 0) != ESP_OK ||
        uart_get_buffered_data_len(UART_NUM_1, &rx_buffered) != ESP_OK || rx_buffered) return false;
    /* Route only the two Rev.B.1 pins documented by the original source.
     * Inverted UART idle HIGH becomes DE LOW; RX is never inverted. */
    esp_err_t err = uart_set_rx_full_threshold(UART_NUM_1, 1);
    if (err == ESP_OK) err = uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_TXD_INV);
    if (err == ESP_OK) err = uart_set_pin(UART_NUM_1, 21, 20, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK && !s.emergency_stop && uart_tx_chars(UART_NUM_1, (const char *)bytes, length) == (int)length) return true;
    request_abort_tx(NULL);
    return false;
}

static void request_event(const char *event, uint16_t pid, const char *detail, void *unused)
{
    (void)unused;
    /* An authorized 17-PID test must not fill flash with request/echo logs.
     * Counters in /api/state still track each TX, RX and timeout. */
    /* REQUEST is emitted while the 5-byte UART TX is still in flight.
     * Do not fsync to flash under the RX/TX mutex during that deadline:
     * persistent TX_ECHO after the bytes are received is stronger evidence
     * and cannot falsely delay echo validation past 20ms. */
    if ((!strcmp(event,"REQUEST") && !tb_config()->full_decode_enabled) ||
        (!tb_config()->full_decode_enabled && !s.active.config.sequential_scan &&
         (!strcmp(event,"TX_ECHO") || !strcmp(event,"RESPONSE_CANDIDATE") ||
          !strcmp(event,"TIMEOUT"))))
        return;
    cJSON *row = cJSON_CreateObject();
    if (!row) return;
    cJSON_AddStringToObject(row, "sample_kind", "J1708_REQUEST_EVENT");
    cJSON_AddNumberToObject(row, "schema_version", 2);
    cJSON_AddNumberToObject(row, "boot_id", tb_boot_id());
    cJSON_AddNumberToObject(row, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddBoolToObject(row, "time_valid", false);
    cJSON_AddStringToObject(row, "time_source", "boot_monotonic");
    cJSON_AddStringToObject(row, "event", event);
    cJSON_AddStringToObject(row, "detail", detail);
    cJSON_AddNumberToObject(row, "pid", pid);
    if (s.active.configured) {
        cJSON_AddNumberToObject(row, "local_mid", s.active.config.local_mid);
        for (unsigned i = 0; i < TBX_J1708_REQUEST_COUNT; ++i)
            if (s.requests.pids[i].pid == pid) cJSON_AddNumberToObject(row, "target_mid", s.active.config.target_mid[i]);
        if (!strcmp(event, "REQUEST")) {
            char raw[11];
            for (unsigned i = 0; i < 5; ++i) snprintf(raw + i * 2, 3, "%02X", s.active.tx_frame[i]);
            cJSON_AddStringToObject(row, "tx_frame", raw);
        }
    }
    cJSON_AddStringToObject(row, "validation", "EM_VALIDACAO");
    char *json = cJSON_PrintUnformatted(row);
    if (json && strlen(json) < TB_RECORD_MAX) tb_store_submit(json);
    free(json); cJSON_Delete(row);
}

static void request_task(void *unused)
{
    (void)unused;
    for (;;) {
        bool ota_busy = tb_web_ota_busy();
        xSemaphoreTake(s.lock, portMAX_DELAY);
        uint64_t now = esp_timer_get_time();
        if (ota_busy && s.requests.armed) tbx_j1708_active_stop(&s.active, "OTA_BUSY");
        size_t buffered = 0;
        if (s.j1708_ready && (gpio_get_level(GPIO_NUM_20) == 0 ||
            uxQueueMessagesWaiting(s.uart_events) ||
            (uart_get_buffered_data_len(UART_NUM_1, &buffered) == ESP_OK && buffered)))
            tbx_j1708_active_note_rx(&s.active, NULL, 0, now);
        tbx_j1708_active_tick(&s.active, now);
        xSemaphoreGive(s.lock);
        vTaskDelay(1);
    }
}

typedef struct { uint32_t version; double distance, fuel; bool distance_valid, fuel_valid; } trip_blob_v1_t;
typedef struct { uint32_t version; double distance, fuel; bool distance_valid, fuel_valid;
    bool counter_valid; uint8_t counter_sa; uint32_t counter_raw; } trip_blob_t;

static void persist_trip(void)
{
    if (!s.nvs_open || !s.trip_persist_lock) return;
    xSemaphoreTake(s.trip_persist_lock, portMAX_DELAY);
    xSemaphoreTake(s.lock, portMAX_DELAY);
    trip_blob_t blob = { .version = 2, .distance = s.distance_trip, .fuel = s.fuel_trip,
        .distance_valid = s.distance_trip_valid, .fuel_valid = s.fuel_trip_valid,
        .counter_valid = s.distance_counter_valid, .counter_sa = s.distance_counter_sa, .counter_raw = s.distance_counter_raw };
    xSemaphoreGive(s.lock);
    esp_err_t err = nvs_set_blob(s.nvs, "trip", &blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(s.nvs);
    if (err != ESP_OK) ESP_LOGW(TAG, "Trip checkpoint: %s", esp_err_to_name(err));
    xSemaphoreGive(s.trip_persist_lock);
}

static void mirror_trip_locked(void)
{
    s.distance_trip = s.trip.total_km; s.distance_trip_valid = s.trip.has_distance;
    s.distance_counter_valid = s.trip.counter_valid; s.distance_counter_sa = s.trip.counter_sa;
    s.distance_counter_raw = s.trip.counter_raw; s.distance_counter_at = s.trip.counter_at_ms;
}

static uint32_t pgn_of(uint32_t id)
{
    uint32_t pgn = (id >> 8) & 0x3ffff;
    if (((id >> 16) & 0xff) < 240) pgn &= 0x3ff00;
    return pgn;
}

static void update_signals(const twai_message_t *frame)
{
    if (!frame->extd || frame->rtr) return;
    uint32_t pgn = pgn_of(frame->identifier);
    uint64_t now = esp_timer_get_time() / 1000;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (pgn == 61444 && frame->data_length_code >= 5) {
        uint16_t raw = frame->data[3] | ((uint16_t)frame->data[4] << 8);
        if (raw <= 0xfaff) { s.rpm = raw * 0.125; s.rpm_at = now; s.rpm_j1587 = false; s.rpm_can_at = now; }
        double rate, delta;
        if (tbx_fuel_ingest(&s.fuel_model, frame->identifier & 255, raw, frame->data[2], now, &rate, &delta)) {
            s.fuel_trip += delta; s.fuel_trip_valid = true;
            s.fuel_rate = rate; s.fuel_at = s.fuel_model_at = now;
            s.fuel_j1587 = false; s.fuel_derived = true; s.fuel_can_at = now;
            s.fuel_address = frame->identifier & 255;
        }
    } else if (pgn == 65265 && frame->data_length_code >= 3) {
        uint16_t raw = frame->data[1] | ((uint16_t)frame->data[2] << 8);
        if (raw <= 0xfaff) {
            double speed = raw / 256.0;
            if (speed > 0.1 && s.requests.armed && !s.active.work_mode)
                tbx_j1708_active_stop(&s.active, "VEHICLE_MOVEMENT_J1939");
            tbx_trip_result_t result = tbx_trip_speed(&s.trip, TBX_TRIP_J1939, frame->identifier & 255, speed, now);
            mirror_trip_locked();
            if (result != TBX_TRIP_SOURCE_IGNORED && result != TBX_TRIP_INVALID) {
            s.speed = speed; s.speed_at = now; s.speed_j1587 = false; s.speed_can_at = now;
            }
        }
    } else if (pgn == 65266 && frame->data_length_code >= 2 &&
        (!s.fuel_model_at || now - s.fuel_model_at > 5000)) {
        uint16_t raw = frame->data[0] | ((uint16_t)frame->data[1] << 8);
        if (raw <= 0xfaff) {
            double rate = raw * 0.05;
            if (s.fuel_at && !s.fuel_derived && !s.fuel_j1587 && s.fuel_address == (frame->identifier & 255) && now > s.fuel_at && now - s.fuel_at <= 3000) {
                s.fuel_trip += (s.fuel_rate + rate) * 0.5 * (now - s.fuel_at) / 3600000.0;
            }
            s.fuel_trip_valid = true;
            s.fuel_rate = rate; s.fuel_at = now; s.fuel_j1587 = false; s.fuel_can_at = now; s.fuel_address = frame->identifier & 255; s.fuel_derived = false;
        }
    } else if (pgn == 65217 && frame->data_length_code >= 4) {
        uint32_t raw = frame->data[0] | ((uint32_t)frame->data[1] << 8) |
            ((uint32_t)frame->data[2] << 16) | ((uint32_t)frame->data[3] << 24);
        uint8_t sa = frame->identifier & 255;
        if (raw <= UINT32_C(0xfaffffff)) {
            tbx_trip_result_t result = tbx_trip_counter(&s.trip, sa, raw, now);
            mirror_trip_locked();
            if (result != TBX_TRIP_SOURCE_IGNORED && result != TBX_TRIP_INVALID) {
            s.distance_total = raw * 0.005; s.total_at = now; s.total_j1587 = false; s.total_can_at = now;
            }
        }
    } else if (pgn == 65248 && frame->data_length_code >= 8 &&
        (!s.distance_counter_at || now - s.distance_counter_at > 30000)) {
        uint32_t raw = frame->data[4] | ((uint32_t)frame->data[5] << 8) |
            ((uint32_t)frame->data[6] << 16) | ((uint32_t)frame->data[7] << 24);
        if (raw <= UINT32_C(0xfaffffff)) { s.distance_total = raw * 0.125; s.total_at = now; s.total_j1587 = false; s.total_can_at = now; }
    }
    xSemaphoreGive(s.lock);
}

static void can_task(void *unused)
{
    twai_message_t frame;
    for (;;) {
        esp_err_t err = twai_receive(&frame, pdMS_TO_TICKS(1000));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) { ++s.can_errors; vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        ++s.can_frames;
        uint64_t captured_us = esp_timer_get_time();
        /* Always decode PGNs, even when per-frame RAW persistence is off. */
        tb_telemetry_can(frame.identifier, frame.extd, frame.rtr, frame.data_length_code, frame.data, captured_us);
        if (frame.rtr) ++s.can_rtr;
        /* Raw frames go to the bounded legacy TCP stream. Cloud samples are 1 Hz snapshots. */
        if (!frame.rtr) tb_live_can(frame.identifier, frame.extd, frame.data_length_code, frame.data);
        update_signals(&frame);
    }
}

/* Called with s.lock held after checksum and complete J1587 layout validation.
 * J1939 remains primary while fresh; both observations are retained by the
 * separate telemetry worker regardless of dashboard source preference. */
static void update_j1587_signal_locked(const tbx_j1587_observation_t *value, void *unused)
{
    (void)unused;
    uint64_t now = value->uptime_ms;
    if (value->quality == TBX_J1587_OBS_EM_VALIDACAO || value->quality == TBX_J1587_OBS_INDISPONIVEL) {
        const bool matched=tbx_j1708_active_note_response(&s.active,value->mid,value->pid,now*1000);
        if (matched && s.active.config.sequential_scan) {
            /* Persist each matching MID/PID observation — not just summary
             * counters. This is a response CANDIDATE, never causal proof. */
            cJSON *row=cJSON_CreateObject();
            if(row){
                cJSON_AddStringToObject(row,"sample_kind","J1708_REQUEST_RESULT");
                cJSON_AddNumberToObject(row,"schema_version",2);
                cJSON_AddNumberToObject(row,"boot_id",tb_boot_id());
                cJSON_AddNumberToObject(row,"uptime_ms",(double)now);
                cJSON_AddBoolToObject(row,"time_valid",false);
                cJSON_AddStringToObject(row,"time_source","boot_monotonic");
                cJSON_AddStringToObject(row,"classification","RESPONSE_CANDIDATE");
                cJSON_AddNumberToObject(row,"pid",value->pid);
                cJSON_AddNumberToObject(row,"mid",value->mid);
                cJSON_AddNumberToObject(row,"local_mid",s.active.config.local_mid);
                cJSON_AddStringToObject(row,"quality",tbx_j1587_observation_quality_name(value->quality));
                if(value->name)cJSON_AddStringToObject(row,"name",value->name);
                if(value->unit)cJSON_AddStringToObject(row,"unit",value->unit);
                if(value->has_numeric && isfinite(value->value))
                    cJSON_AddNumberToObject(row,"value",value->value);
                char rawhex[43]={0};
                const size_t rawlen=value->raw_len<=21u?value->raw_len:21u;
                for(size_t k=0;k<rawlen;k++)snprintf(rawhex+2u*k,3u,"%02X",value->raw[k]);
                cJSON_AddStringToObject(row,"parameter_hex",rawhex);
                for(unsigned k=0;k<TBX_J1708_REQUEST_COUNT;k++)
                    if(s.requests.pids[k].pid==value->pid){
                        cJSON_AddNumberToObject(row,"target_mid",s.active.config.target_mid[k]);
                        const uint32_t sent_ms=s.requests.pids[k].last_sent_ms;
                        if(now>=sent_ms)cJSON_AddNumberToObject(row,"latency_ms",(double)(now-sent_ms));
                        break;
                    }
                char *json=cJSON_PrintUnformatted(row);
                if(json && strlen(json)<TB_RECORD_MAX)(void)tb_store_submit(json);
                free(json);cJSON_Delete(row);
            }
        }
    }
    if (value->quality != TBX_J1587_OBS_EM_VALIDACAO || !value->has_numeric || !isfinite(value->value)) return;
    if (value->pid == 84 && value->value > 0.1 && s.requests.armed && !s.active.work_mode)
        tbx_j1708_active_stop(&s.active, "VEHICLE_MOVEMENT_J1587");
    if (value->pid == 84 && (!s.speed_can_at || now - s.speed_can_at > 5000)) {
        tbx_trip_result_t result = tbx_trip_speed(&s.trip, TBX_TRIP_J1587, value->mid, value->value, now);
        mirror_trip_locked();
        if (result == TBX_TRIP_SOURCE_IGNORED || result == TBX_TRIP_INVALID) return;
        s.speed = value->value; s.speed_at = now; s.speed_j1587 = true;
    } else if (value->pid == 190 && (!s.rpm_can_at || now - s.rpm_can_at > 5000)) {
        s.rpm = value->value; s.rpm_at = now; s.rpm_j1587 = true;
    } else if (value->pid == 183 && (!s.fuel_can_at || now - s.fuel_can_at > 5000)) {
        if (s.fuel_at && s.fuel_j1587 && s.fuel_address == value->mid && now > s.fuel_at && now - s.fuel_at <= 3000)
            s.fuel_trip += (s.fuel_rate + value->value) * 0.5 * (now - s.fuel_at) / 3600000.0;
        s.fuel_trip_valid = true; s.fuel_rate = value->value; s.fuel_at = now; s.fuel_j1587 = true; s.fuel_address = value->mid; s.fuel_derived = false;
    } else if (value->pid == 245 && (!s.total_can_at || now - s.total_can_at > 30000)) {
        s.distance_total = value->value; s.total_at = now; s.total_j1587 = true;
    }
}

static void finish_j1708(uint8_t *frame, size_t size, bool oversize)
{
    if (!size) return;
    uint64_t captured_us = esp_timer_get_time();
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool echo = !oversize && tbx_j1708_active_is_tx_echo(&s.active, frame, size, captured_us);
    if (!echo && !oversize && size <= 21 && tb_protocol_j1708_checksum_valid(frame, size) && frame[0] >= 128) {
        s.observed_mid[frame[0] >> 3] |= (uint8_t)(1u << (frame[0] & 7u));
        if (s.requests.armed && s.active.configured && frame[0] == s.active.config.local_mid) {
            tbx_j1708_active_stop(&s.active, "LOCAL_MID_CONFLICT");
            s.requests.safety_stopped = true;
        }
    }
    xSemaphoreGive(s.lock);
    if (echo) {
        if (tb_config()->full_decode_enabled) tb_telemetry_j1708_echo(frame, size, captured_us, oversize);
        return;
    }
    /* Decode all passive and solicited J1587 PIDs regardless of storage policy. */
    tb_telemetry_j1708(frame, size, captured_us, oversize);
    if (oversize || size > 21) {
        ++s.j1708_oversize;
        /* Bad PASSIVE frames must not masquerade as TX/echo failure. */
        return;
    }
    if (!tb_protocol_j1708_checksum_valid(frame, size)) {
        ++s.j1708_checksum_errors;
        /* Keep this in the passive checksum counter, not in TX collision
         * protection. Confirmed UART/echo failures still stop immediately. */
        return;
    }
    char record[TB_RECORD_MAX];
    if (tb_protocol_j1708_json(record, sizeof(record), captured_us, frame, size)) {
        ++s.j1708_frames;
        uint64_t now = captured_us / 1000;
        xSemaphoreTake(s.lock, portMAX_DELAY);
        snprintf(s.j1708_last, sizeof(s.j1708_last), "%s", record);
        s.j1708_at = now;
        (void)tbx_j1587_bridge_decode(frame, size, now, update_j1587_signal_locked, NULL);
        xSemaphoreGive(s.lock);
    }
}

static void add_sample_identity(cJSON *sample, const char *kind)
{
    cJSON_AddStringToObject(sample, "sample_kind", kind);
    cJSON_AddNumberToObject(sample, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(sample, "boot_id", tb_boot_id());
    cJSON_AddNumberToObject(sample, "schema_version", 2);
    cJSON_AddBoolToObject(sample, "time_valid", false);
    cJSON_AddStringToObject(sample, "time_source", "boot_monotonic");
    struct timeval utc;
    if (gettimeofday(&utc, NULL) == 0 && utc.tv_sec >= 1704067200 &&
        utc.tv_usec >= 0 && utc.tv_usec < 1000000) {
        struct tm broken;
        char date[20], stamp[25];
        if (gmtime_r(&utc.tv_sec, &broken) &&
            strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &broken) == 19) {
            unsigned milliseconds = (unsigned)(utc.tv_usec / 1000) % 1000U;
            int length = snprintf(stamp, sizeof(stamp), "%.19s.%03uZ", date, milliseconds);
            if (length == 24) {
                cJSON_AddStringToObject(sample, "ts", stamp);
                cJSON_ReplaceItemInObject(sample, "time_valid", cJSON_CreateBool(true));
                cJSON_ReplaceItemInObject(sample, "time_source", cJSON_CreateString("system_clock"));
            }
        }
    }
}

static void queue_sample(cJSON *sample)
{
    if (!sample) return;
    char *json = cJSON_PrintUnformatted(sample);
    if (json && strlen(json) < TB_RECORD_MAX) tb_store_submit(json);
    else ESP_LOGW(TAG, "Telemetry snapshot exceeds record capacity");
    free(json); cJSON_Delete(sample);
}

static void snapshot_task(void *unused)
{
    TickType_t wake = xTaskGetTickCount();
    uint64_t window_start = esp_timer_get_time() / 1000;
    uint32_t old_can = 0, old_j1708 = 0, old_checksum = 0, old_overruns = 0;
    uint32_t old_request_sent = 0, old_request_answered = 0, old_request_timeouts = 0;
    unsigned seconds = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        uint64_t now = esp_timer_get_time() / 1000;
        /* Snapshot at 15 s in LEGACY. The 60 s health clock remains active. */
        if (tb_operational_due(tb_config()->full_decode_enabled, seconds)) {
            cJSON *sample = cJSON_CreateObject();
            if (sample) {
                add_sample_identity(sample, "OPERATIONAL");
                bool vehicle_data = false;
                xSemaphoreTake(s.lock, portMAX_DELAY);
                if (s.speed_at && now - s.speed_at <= 5000) {
                    cJSON_AddNumberToObject(sample, "speed_kmh", s.speed);
                    cJSON_AddStringToObject(sample, "speed_source", s.speed_j1587 ? "j1587" : "j1939");
                    vehicle_data = true;
                }
                if (s.rpm_at && now - s.rpm_at <= 5000) {
                    cJSON_AddNumberToObject(sample, "rpm", s.rpm);
                    cJSON_AddStringToObject(sample, "rpm_source", s.rpm_j1587 ? "j1587" : "j1939"); vehicle_data = true;
                }
                if (s.fuel_at && now - s.fuel_at <= 5000) {
                    cJSON_AddNumberToObject(sample, "fuel_rate_lph", s.fuel_rate);
                    cJSON_AddStringToObject(sample, "fuel_source", s.fuel_j1587 ? "j1587" : "j1939"); vehicle_data = true;
                }
                if (s.total_at && now - s.total_at <= 30000) { cJSON_AddNumberToObject(sample, "odometer_km", s.distance_total); vehicle_data = true; }
                if (s.distance_trip_valid) cJSON_AddNumberToObject(sample, "trip_distance_km", s.distance_trip);
                if (s.fuel_trip_valid) cJSON_AddNumberToObject(sample, "trip_fuel_l", s.fuel_trip);
                if (s.fuel_derived) {
                    cJSON_AddBoolToObject(sample, "fuel_derived", true);
                    cJSON_AddStringToObject(sample, "fuel_model", "MASTER_V1_2643Nm_C0_0.9720031611625598");
                }
                xSemaphoreGive(s.lock);
                if (tb_core_append_gps(sample)) vehicle_data = true;
                else { cJSON_AddNullToObject(sample, "gps"); cJSON_AddStringToObject(sample, "gps_status", "NO_FIX"); cJSON_AddBoolToObject(sample, "has_fix", false); }
                /* Do not replace operational cloud state with empty bench/diagnostic rows. */
                if (vehicle_data) queue_sample(sample); else cJSON_Delete(sample);
            }
        }
        if (++seconds < 60) continue;
        seconds = 0;
        cJSON *health = cJSON_CreateObject();
        if (!health) continue;
        add_sample_identity(health, "HEALTH");
        cJSON_AddNumberToObject(health, "window_seconds", (double)(now - window_start) / 1000.0);
        const cJSON *stamp = cJSON_GetObjectItemCaseSensitive(health, "ts");
        if (cJSON_IsString(stamp)) cJSON_AddStringToObject(health, "window_end", stamp->valuestring);
        cJSON_AddNumberToObject(health, "window_start_uptime_ms", (double)window_start);
        cJSON_AddNumberToObject(health, "window_end_uptime_ms", (double)now);
        cJSON *metrics = cJSON_AddObjectToObject(health, "metrics");
        cJSON_AddNumberToObject(metrics, "can_frames", s.can_frames - old_can);
        cJSON_AddNumberToObject(metrics, "j1708_frames", s.j1708_frames - old_j1708);
        cJSON_AddNumberToObject(metrics, "j1708_checksum_errors", s.j1708_checksum_errors - old_checksum);
        cJSON_AddNumberToObject(metrics, "uart_overruns", s.uart_overruns - old_overruns);
        cJSON *context = cJSON_AddObjectToObject(health, "context");
        cJSON_AddStringToObject(context, "role", "core");
        cJSON_AddStringToObject(context, "firmware", tb_version());
        cJSON_AddStringToObject(context, "telemetry_profile",
            tb_config()->full_decode_enabled ? "COMPLETO" : "LEGACY");
        cJSON_AddStringToObject(context, "j1708_framing", "uart_idle_10bits");
        cJSON_AddBoolToObject(context, "j1708_work_mode", s.active.work_mode);
        cJSON_AddNumberToObject(metrics, "j1708_request_sent", s.requests.sent_total - old_request_sent);
        cJSON_AddNumberToObject(metrics, "j1708_request_answered", s.requests.answered_total - old_request_answered);
        cJSON_AddNumberToObject(metrics, "j1708_request_timeouts", s.requests.timeout_total - old_request_timeouts);
        old_request_sent = s.requests.sent_total;
        old_request_answered = s.requests.answered_total;
        old_request_timeouts = s.requests.timeout_total;
        /* Append optional sensors last so this routine removes readings when
         * the complete HEALTH approaches the 768-byte storage limit. */
        tb_telemetry_append_j1587_health(health);
        old_can = s.can_frames; old_j1708 = s.j1708_frames;
        old_checksum = s.j1708_checksum_errors; old_overruns = s.uart_overruns;
        window_start = now;
        queue_sample(health);
    }
}

static void j1708_task(void *unused)
{
    uint8_t frame[64], chunk[64];
    size_t length = 0;
    bool oversize = false;
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(s.uart_events, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            ++s.uart_overruns; length = 0; oversize = false;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            tbx_j1708_active_note_link_error(&s.active, "UART_OVERFLOW");
            xSemaphoreGive(s.lock);
            uart_flush_input(UART_NUM_1); xQueueReset(s.uart_events); continue;
        }
        if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) {
            ++s.uart_overruns; length = 0; oversize = false;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            tbx_j1708_active_note_link_error(&s.active, "UART_FRAME_OR_PARITY_ERROR");
            xSemaphoreGive(s.lock);
            continue;
        }
        if (event.type != UART_DATA) continue;
        size_t remaining = event.size;
        while (remaining) {
            size_t requested = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            int got = uart_read_bytes(UART_NUM_1, chunk, requested, pdMS_TO_TICKS(20));
            if (got <= 0) { ++s.uart_overruns; oversize = true; break; }
            remaining -= got;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            tbx_j1708_active_note_rx(&s.active, chunk, got, esp_timer_get_time());
            xSemaphoreGive(s.lock);
            for (int i = 0; i < got; ++i) {
                if (length < sizeof(frame)) frame[length++] = chunk[i];
                else oversize = true;
                if (length == 5 && !oversize) {
                    xSemaphoreTake(s.lock, portMAX_DELAY);
                    bool echo = tbx_j1708_active_is_tx_echo(&s.active, frame, length, esp_timer_get_time());
                    xSemaphoreGive(s.lock);
                    if (echo) { finish_j1708(frame, length, false); length = 0; }
                }
            }
        }
        /*
         * Hardware timeout is one 8N1 character (10 bit times), not two bits.
         * This is receive-only idle framing, verified by the message checksum.
         * Busy/noisy/coalesced traffic is reported as oversize/checksum errors.
         */
        if (event.timeout_flag) { finish_j1708(frame, length, oversize); length = 0; oversize = false; }
    }
}

static void trip_checkpoint_task(void *unused)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (tb_web_ota_busy()) continue;
        persist_trip();
    }
}

esp_err_t tb_capture_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    s.trip_persist_lock = xSemaphoreCreateMutex();
    const tbx_j1708_active_ops_t active_ops = { .begin_tx = request_begin_tx,
        .abort_tx = request_abort_tx, .event = request_event };
    tbx_j1708_active_init(&s.active, &s.requests, &active_ops, NULL);
    s.emergency_stop = true;
    if (!s.lock || !s.trip_persist_lock) return ESP_ERR_NO_MEM;
    s.nvs_open = nvs_open("tb_trip", NVS_READWRITE, &s.nvs) == ESP_OK;
    if (s.nvs_open) {
        trip_blob_t blob = {0};
        size_t size = sizeof(blob);
        if (nvs_get_blob(s.nvs, "trip", &blob, &size) == ESP_OK &&
            ((size == sizeof(trip_blob_v1_t) && blob.version == 1) || (size == sizeof(blob) && blob.version == 2)) &&
            isfinite(blob.distance) && blob.distance >= 0 && isfinite(blob.fuel) && blob.fuel >= 0) {
            s.distance_trip = blob.distance; s.fuel_trip = blob.fuel;
            s.distance_trip_valid = blob.distance_valid; s.fuel_trip_valid = blob.fuel_valid;
            if (blob.version == 2) { s.distance_counter_valid = blob.counter_valid;
                s.distance_counter_sa = blob.counter_sa; s.distance_counter_raw = blob.counter_raw; }
        }
    }
    tbx_trip_init(&s.trip, s.distance_trip, s.distance_trip_valid);
    if (s.distance_counter_valid) tbx_trip_restore_counter(&s.trip, s.distance_counter_sa, s.distance_counter_raw);
    /* Decode selected J1939 and J1587 in both profiles. LEGACY disables
     * per-frame RAW/DECODED persistence, not the decoder or sensor cache.
     * Active J1708 requests stay explicitly authorized and parked-only. */
    esp_err_t telemetry_err = tb_telemetry_start();
    if (telemetry_err != ESP_OK) return telemetry_err;
    /* GPIO1 is deliberately never an output on the uncorrected B1 watchdog. */
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
    if (tb_config()->repaired_power_gates) {
        gpio_set_level(GPIO_NUM_16, 0); gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    }
    if (!tb_config()->capture_enabled) {
        if (xTaskCreate(snapshot_task, "tb_snapshot", 6144, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        return ESP_OK;
    }
    gpio_set_level(GPIO_NUM_2, 0); gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_4, GPIO_NUM_18, TWAI_MODE_LISTEN_ONLY);
    general.tx_queue_len = 0; general.rx_queue_len = 128;
    general.intr_flags = ESP_INTR_FLAG_IRAM;
    twai_timing_config_t timing = tb_config()->can_baud == 500000 ? (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS() :
        (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err == ESP_OK) err = twai_start();
    if (err == ESP_ERR_NO_MEM) return err;
    if (err == ESP_OK) {
        if (xTaskCreate(can_task, "tb_can", 5120, NULL, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        s.can_ready = true;
    } else snprintf(s.can_error, sizeof(s.can_error), "%s", esp_err_to_name(err));

    gpio_set_level(GPIO_NUM_21, 0); gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
    uart_config_t uart = { .baud_rate = 9600, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    err = uart_driver_install(UART_NUM_1, 2048, 0, 24, &s.uart_events, 0);
    if (err == ESP_OK) err = uart_param_config(UART_NUM_1, &uart);
    if (err == ESP_OK) err = uart_set_pin(UART_NUM_1, 5, 20, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = uart_set_rx_timeout(UART_NUM_1, 1);
    if (err == ESP_OK) err = uart_set_rx_full_threshold(UART_NUM_1, 64);
    if (err == ESP_ERR_NO_MEM) return err;
    if (err == ESP_OK) {
        if (xTaskCreate(j1708_task, "tb_j1708", 5120, NULL, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        s.j1708_ready = true;
        if (xTaskCreate(request_task, "tb_j1708_tx", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    } else snprintf(s.j1708_error, sizeof(s.j1708_error), "%s", esp_err_to_name(err));
    if (xTaskCreate(trip_checkpoint_task, "tb_trip", 3072, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate(snapshot_task, "tb_snapshot", 6144, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "CAN listen-only=%s J1708 receive-only=%s", s.can_ready ? "ready" : s.can_error,
        s.j1708_ready ? "ready" : s.j1708_error);
    return ESP_OK;
}

void tb_capture_reset_trip(void)
{
    if (!s.lock) return;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    tbx_trip_reset(&s.trip);
    mirror_trip_locked();
    s.fuel_trip = 0; tbx_fuel_init(&s.fuel_model); s.fuel_model_at = 0;
    s.distance_trip_valid = s.fuel_trip_valid = true;
    s.speed_at = s.fuel_at = 0;
    s.distance_counter_valid = false; s.distance_counter_at = 0;
    xSemaphoreGive(s.lock);
    persist_trip();
}

static void add_signal_field(cJSON *object, const char *name, bool valid, double value)
{
    if (valid) cJSON_AddNumberToObject(object, name, value);
    else cJSON_AddNullToObject(object, name);
}

void tb_capture_add_json(cJSON *object)
{
    uint64_t now = esp_timer_get_time() / 1000;
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    add_signal_field(object, "rpm", s.rpm_at && now - s.rpm_at <= 5000, s.rpm);
    add_signal_field(object, "speedKph", s.speed_at && now - s.speed_at <= 5000, s.speed);
    add_signal_field(object, "distanceTotalKm", s.total_at && now - s.total_at <= 30000, s.distance_total);
    add_signal_field(object, "distanceTestKm", s.distance_trip_valid, s.distance_trip);
    add_signal_field(object, "fuelLitersTest", s.fuel_trip_valid, s.fuel_trip);
    cJSON_AddStringToObject(object, "trip_distance_quality", s.trip.quality);
    cJSON_AddNumberToObject(object, "trip_distance_gaps", s.trip.gaps);
    cJSON_AddNumberToObject(object, "trip_counter_resets", s.trip.resets);
    cJSON_AddNumberToObject(object, "trip_counter_range_errors", s.trip.implausible);
    cJSON_AddNullToObject(object, "combinationWeightKg");
    cJSON *can = cJSON_AddObjectToObject(object, "can");
    cJSON_AddBoolToObject(can, "enabled", tb_config()->capture_enabled);
    cJSON_AddBoolToObject(can, "ready", s.can_ready);
    cJSON_AddBoolToObject(can, "listen_only", true);
    cJSON_AddNumberToObject(can, "baud", tb_config()->can_baud);
    cJSON_AddNumberToObject(can, "frames", s.can_frames);
    cJSON_AddNumberToObject(can, "remote_frames", s.can_rtr);
    cJSON_AddNumberToObject(can, "read_errors", s.can_errors);
    cJSON_AddStringToObject(can, "error", s.can_error);
    twai_status_info_t status;
    if (s.can_ready && twai_get_status_info(&status) == ESP_OK) {
        cJSON_AddNumberToObject(can, "rx_missed", status.rx_missed_count);
        cJSON_AddNumberToObject(can, "rx_overrun", status.rx_overrun_count);
        cJSON_AddNumberToObject(can, "bus_errors", status.bus_error_count);
    }
    cJSON *j1708 = cJSON_AddObjectToObject(object, "j1708");
    cJSON_AddBoolToObject(j1708, "ready", s.j1708_ready);
    cJSON_AddStringToObject(j1708, "framing", "uart_idle_10bits_checksum_validated");
    cJSON_AddStringToObject(j1708, "error", s.j1708_error);
    cJSON_AddNumberToObject(j1708, "frames", s.j1708_frames);
    cJSON_AddNumberToObject(j1708, "checksum_errors", s.j1708_checksum_errors);
    cJSON_AddNumberToObject(j1708, "oversize_or_coalesced", s.j1708_oversize);
    cJSON_AddNumberToObject(j1708, "uart_overruns", s.uart_overruns);
    /* MIDs actually observed during this boot, to help the operator choose
     * a source address. Not observed != guaranteed free on the entire bus. */
    cJSON *observed = cJSON_AddArrayToObject(j1708, "observed_mids");
    if (observed) {
        for (unsigned mid = 128; mid <= 255; ++mid)
            if (s.observed_mid[mid >> 3] & (1u << (mid & 7u)))
                cJSON_AddItemToArray(observed, cJSON_CreateNumber(mid));
    }
    if (s.j1708_at) {
        cJSON_AddNumberToObject(j1708, "last_frame_age_ms", (double)(now - s.j1708_at));
        cJSON *last = cJSON_Parse(s.j1708_last);
        if (last) cJSON_AddItemToObject(j1708, "last_frame", last);
    }
    cJSON *requests = cJSON_AddObjectToObject(j1708, "requests");
    cJSON_AddBoolToObject(requests, "armed", s.requests.armed);
    cJSON_AddBoolToObject(requests, "hardware_tx_available", s.j1708_ready);
    cJSON_AddBoolToObject(requests, "configured", s.active.configured);
    cJSON_AddBoolToObject(requests, "work_mode", s.active.work_mode);
    cJSON_AddBoolToObject(requests, "work_evidence_ready", s.active.echo_ok >= 3 && s.requests.answered_total > 0);
    cJSON_AddBoolToObject(requests, "safety_stopped", s.requests.safety_stopped);
    cJSON_AddStringToObject(requests, "validation", "EM_VALIDACAO");
    cJSON_AddNumberToObject(requests, "echo_ok", s.active.echo_ok);
    cJSON_AddNumberToObject(requests, "collisions", s.active.collisions);
    cJSON_AddNumberToObject(requests, "echo_timeouts", s.active.echo_timeouts);
    cJSON_AddNumberToObject(requests, "lease_expirations", s.active.lease_expirations);
    cJSON_AddNumberToObject(requests, "authorization_lease_ms",
        s.active.work_mode ? TBX_J1708_WORK_LEASE_US / 1000 : TBX_J1708_ACTIVE_LEASE_US / 1000);
    cJSON_AddNumberToObject(requests, "lease_remaining_ms", s.requests.armed &&
        s.active.lease_deadline_us > (uint64_t)esp_timer_get_time() ?
        (double)(s.active.lease_deadline_us - (uint64_t)esp_timer_get_time()) / 1000.0 : 0);
    cJSON_AddStringToObject(requests, "block_reason", s.active.configured ? "NONE_OR_SAFETY_INTERLOCK" : "EXPLICIT_MIDS_AND_TOPOLOGY_ATTESTATION_REQUIRED");
    cJSON_AddBoolToObject(requests,"sequential_scan",s.active.config.sequential_scan);
    if(s.active.config.sequential_scan){
        cJSON_AddNumberToObject(requests,"start_pid",
            s.active.config.start_pid?s.active.config.start_pid:s.requests.pids[0].pid);
        cJSON_AddNumberToObject(requests,"scan_total",s.active.scan_budget);
        cJSON_AddNumberToObject(requests,"scan_sent",
            s.requests.sent_total>=s.active.scan_sent_baseline?
              s.requests.sent_total-s.active.scan_sent_baseline:0);
        if(s.requests.pending<TBX_J1708_REQUEST_COUNT)
            cJSON_AddNumberToObject(requests,"pending_pid",s.requests.pids[s.requests.pending].pid);
    }
    cJSON_AddNumberToObject(requests, "min_spacing_ms", s.requests.min_spacing_ms);
    cJSON_AddNumberToObject(requests, "timeout_ms", s.requests.timeout_ms);
    cJSON_AddNumberToObject(requests, "sent", s.requests.sent_total);
    cJSON_AddNumberToObject(requests, "responses", s.requests.answered_total);
    cJSON_AddNumberToObject(requests, "timeouts", s.requests.timeout_total);
    cJSON_AddNumberToObject(requests, "tx_errors", s.requests.tx_failure_total);
    cJSON *pids = cJSON_AddArrayToObject(requests, "pids");
    for (unsigned i = 0; i < TBX_J1708_REQUEST_COUNT; ++i) cJSON_AddItemToArray(pids, cJSON_CreateNumber(s.requests.pids[i].pid));
    cJSON *stats = cJSON_AddArrayToObject(requests, "stats");
    for (unsigned i = 0; i < TBX_J1708_REQUEST_COUNT; ++i) {
        const tbx_j1708_request_stat_t *stat = &s.requests.pids[i];
        cJSON *row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "pid", stat->pid);
        cJSON_AddBoolToObject(row, "enabled", stat->enabled);
        if (s.active.configured) cJSON_AddNumberToObject(row, "target_mid", s.active.config.target_mid[i]);
        cJSON_AddNumberToObject(row, "sent", stat->sent);
        cJSON_AddNumberToObject(row, "response_candidates", stat->answered);
        cJSON_AddNumberToObject(row, "timeouts", stat->timed_out);
        cJSON_AddItemToArray(stats, row);
    }
    if (s.active.configured) cJSON_AddNumberToObject(requests, "local_mid", s.active.config.local_mid);
    cJSON_AddNumberToObject(requests, "idle_guard_us", s.active.idle_guard_us);
    xSemaphoreGive(s.lock);
    cJSON_AddStringToObject(object, "telemetry_profile",
        tb_config()->full_decode_enabled ? "COMPLETO" : "LEGACY");
    cJSON_AddNumberToObject(object, "operational_interval_s",
        tb_config()->full_decode_enabled ? 1 : TB_LEGACY_OPERATIONAL_SECONDS);
    tb_telemetry_add_json(object);
}

bool tb_capture_request_control(bool arm, bool authorized, bool stationary)
{
    if (!arm) { s.emergency_stop = true; request_abort_tx(NULL); }
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool accepted = true;
    if (arm) {
        const uint64_t now_ms=(uint64_t)(esp_timer_get_time()/1000);
        const bool speed_fresh=s.speed_at && now_ms>=s.speed_at && now_ms-s.speed_at<=5000u;
        const bool movement=speed_fresh && s.speed>0.1;
        const bool parked_verified=speed_fresh && s.speed<=0.1;
        accepted = s.j1708_ready && !tb_web_ota_busy() &&
            (!movement || s.active.work_mode) &&
            (!s.active.config.sequential_scan || parked_verified) &&
            tbx_j1708_active_arm(&s.active, authorized, stationary, esp_timer_get_time());
        s.emergency_stop = !accepted;
        if (!accepted) tbx_j1708_active_stop(&s.active, "ARM_INTERLOCK_REFUSED");
    } else tbx_j1708_active_stop(&s.active, "OPERATOR_STOP");
    xSemaphoreGive(s.lock);
    char event[384];
    snprintf(event, sizeof(event), "{\"sample_kind\":\"J1708_REQUEST_EVENT\",\"boot_id\":%" PRIu32 ",\"uptime_ms\":%" PRIu64 ",\"action\":\"%s\",\"accepted\":%s,\"reason\":\"%s\"}",
        tb_boot_id(), (uint64_t)(esp_timer_get_time() / 1000), arm ? "arm" : "stop", accepted ? "true" : "false",
        arm ? (accepted ? (s.active.work_mode ? "AUTHORIZED_WORK_MODE" : "AUTHORIZED_PARKED_CONFIGURED") : "SAFETY_OR_CONFIGURATION_REQUIRED") : "operator_stop");
    tb_store_submit(event);
    return accepted;
}


bool tb_capture_request_enable_work(bool authorized, bool stationary)
{
    if (!s.lock || !authorized || !stationary || tb_web_ota_busy() ||
        xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    const uint64_t now = (uint64_t)esp_timer_get_time();
    const uint64_t now_ms = now / 1000u;
    /* Speed 0 must be observed freshly in the vehicle, not merely asserted
     * by the caller. A stopped vehicle can then operate with work-mode TX
     * after passing the controller's 3-echo/response proof. */
    const bool parked = s.speed_at && now_ms >= s.speed_at &&
        now_ms - s.speed_at <= 5000u && s.speed <= 0.1;
    const bool accepted = s.j1708_ready && parked &&
        tbx_j1708_active_enable_work(&s.active, now);
    xSemaphoreGive(s.lock);
    return accepted;
}

static bool request_integer(const cJSON *object, const char *name, int min, int max, int *result)
{
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(number) || !isfinite(number->valuedouble) ||
        number->valuedouble < min || number->valuedouble > max || floor(number->valuedouble) != number->valuedouble) return false;
    *result = number->valueint; return true;
}

bool tb_capture_request_configure(const cJSON *config, char *error, size_t error_size)
{
    tbx_j1708_active_config_t next = { .priority = 8 };
    int local, priority;
    if (!request_integer(config, "local_mid", 128, 255, &local) ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(config, "topology_confirmed"))) {
        snprintf(error, error_size, "local_mid explicito e topology_confirmed=true obrigatorios"); return false;
    }
    next.local_mid = local; next.inverted_de21_attested = true;
    /* Test firmware requires parked, automatic one-by-one scan. No
     * unattended repeating scan or TX during vehicle operation. */
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(config,"sequential_scan"))) {
        snprintf(error,error_size,"use sequential_scan=true, parked-only");return false;
    }
    next.sequential_scan=true;
    const cJSON *start=cJSON_GetObjectItemCaseSensitive(config,"start_pid");
    if(start){
        int pid;
        if(!request_integer(config,"start_pid",0,255,&pid)){
            snprintf(error,error_size,"start_pid invalido");return false;
        }
        next.start_pid=(uint16_t)pid;
    }
    if (cJSON_GetObjectItemCaseSensitive(config, "priority")) {
        if (!request_integer(config, "priority", 1, 8, &priority)) {
            snprintf(error, error_size, "priority deve ser inteiro 1..8"); return false;
        }
        next.priority = priority;
    }
    const cJSON *targets = cJSON_GetObjectItemCaseSensitive(config, "targets");
    const cJSON *preset = cJSON_GetObjectItemCaseSensitive(config, "preset");
    if (cJSON_IsString(preset) &&
        !strcmp(preset->valuestring, "fh2013_capture_20261029") && !targets) {
        /* Evidence: Validator 29/10, 70,386 J1708 frames; MID/PID time
         * correlations, NOT proof of causality or universal Volvo mapping.
         * Order: 26,33,34,37,62,68,117,118,235,236,250,31,32,63,108,158,160.
         * Local MID is NEVER inferred from these target MIDs. */
        const uint8_t recorded_mids[TBX_J1708_REQUEST_COUNT] = {
            128,130,130,130,144,128,140,140,128,128,128,130,130,130,128,128,130
        };
        memcpy(next.target_mid, recorded_mids, sizeof(recorded_mids));
        for (unsigned i = 0; i < TBX_J1708_REQUEST_COUNT; ++i) {
            if (next.target_mid[i] == next.local_mid) {
                snprintf(error, error_size, "MID local conflita com preset de alvos");
                return false;
            }
        }
    } else {
        if (preset || !cJSON_IsArray(targets) ||
            cJSON_GetArraySize(targets) != TBX_J1708_REQUEST_COUNT) {
            snprintf(error, error_size, "informe 17 PIDs/MIDs ou preset historico explicito"); return false;
        }
        tbx_j1708_request_scheduler_t defaults;
        tbx_j1708_request_init(&defaults);
        const cJSON *target;
        cJSON_ArrayForEach(target, targets) {
            int pid, mid;
            if (!request_integer(target, "pid", 0, 255, &pid) ||
                !request_integer(target, "mid", 128, 255, &mid) || mid == local) {
                snprintf(error, error_size, "PID/MID alvo invalido ou igual ao MID local");
                return false;
            }
            unsigned i;
            for (i = 0; i < TBX_J1708_REQUEST_COUNT && defaults.pids[i].pid != pid; ++i) { }
            if (i == TBX_J1708_REQUEST_COUNT || next.target_mid[i]) {
                snprintf(error, error_size, "PID alvo desconhecido ou duplicado"); return false;
            }
            next.target_mid[i] = mid;
        }
    }
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        snprintf(error, error_size, "captura ocupada"); return false;
    }
    bool accepted = false;
    if (s.observed_mid[next.local_mid >> 3] & (1u << (next.local_mid & 7u))) {
        snprintf(error, error_size, "MID local ja observado na rede; selecione MID livre confirmado");
    } else if (s.requests.armed) {
        accepted = next.local_mid == s.active.config.local_mid && next.priority == s.active.config.priority &&
            memcmp(next.target_mid, s.active.config.target_mid, sizeof(next.target_mid)) == 0;
        if (!accepted) snprintf(error, error_size, "pare requisicoes antes de alterar configuracao");
    } else {
        accepted = s.j1708_ready && tbx_j1708_active_configure(&s.active, &next);
        if (!accepted) snprintf(error, error_size, "UART indisponivel ou configuracao recusada");
    }
    xSemaphoreGive(s.lock);
    return accepted;
}
