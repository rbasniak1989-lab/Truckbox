#ifndef TB_TELEMETRY_HOST_TEST
#include "telemetry.h"
#include "core_internal.h"
#else
#include "tests/telemetry_host_stubs.h"
#endif
#include "protocols.h"
#include "tb_task_budget.h"
#include "tbx_j1939.h"
#include "tbx_j1939_tp.h"
#include "tbx_j1587_bridge.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#ifndef TB_TELEMETRY_HOST_TEST
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#endif

#define TELEMETRY_QUEUE 128u
#define TELEMETRY_FRAME_MAX 64u
#define TELEMETRY_LATEST 192u
#define TELEMETRY_STALE_MS 5000u
#define TELEMETRY_SCHEMA 2u

typedef struct {
    uint64_t capture_id, captured_us, wall_ms;
    uint32_t id;
    uint16_t supplied_length;
    uint8_t length;
    bool can, extended, rtr, oversize, tx_echo;
    uint8_t bytes[TELEMETRY_FRAME_MAX];
} packet_t;

typedef struct {
    bool used, can, has_value, has_last_valid, raw_only;
    uint16_t signal;
    uint8_t address, raw_length, full_raw_length, component_index, component_count;
    uint32_t pgn;
    uint64_t captured_us, capture_id, last_valid_us;
    double value, last_valid_value;
    const char *name, *unit, *validation, *quality, *component;
    const char *last_valid_quality, *last_valid_validation;
    uint8_t raw[32];
    char text[32];
} latest_t;

static struct {
    QueueHandle_t queue;
    tbx_j1939_t j1939;
    tbx_j1939_tp_t transport;
    uint32_t transport_stats[11];
    latest_t latest[TELEMETRY_LATEST];
    size_t latest_count;
    uint64_t next_capture, captured, queue_dropped, store_accepted,
        store_rejected, serialization_errors, processed, latest_evictions, scheduling_yields;
} s;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    const packet_t *packet;
    const tbx_j1939_tp_message_t *transport;
    cJSON *record, *values;
    unsigned part;
} worker_t;

static void bump(uint64_t *counter)
{
    portENTER_CRITICAL(&s_mux);
    ++*counter;
    portEXIT_CRITICAL(&s_mux);
}

static bool enqueue(packet_t *packet)
{
    struct timeval utc;
    if (gettimeofday(&utc, NULL) == 0 && utc.tv_sec >= 1704067200 &&
        utc.tv_usec >= 0 && utc.tv_usec < 1000000) {
        packet->wall_ms = (uint64_t)utc.tv_sec * 1000 + (uint64_t)utc.tv_usec / 1000;
    }
    portENTER_CRITICAL(&s_mux);
    packet->capture_id = ++s.next_capture;
    ++s.captured;
    portEXIT_CRITICAL(&s_mux);
    if (!s.queue || xQueueSend(s.queue, packet, 0) != pdTRUE) {
        bump(&s.queue_dropped);
        return false;
    }
    return true;
}

bool tb_telemetry_can(uint32_t id, bool extended, bool rtr, uint8_t dlc,
                       const uint8_t *data, uint64_t captured_us)
{
    if (!rtr && dlc && !data) { bump(&s.queue_dropped); return false; }
    packet_t packet = { .can = true, .id = id, .extended = extended,
        .rtr = rtr, .supplied_length = dlc, .captured_us = captured_us };
    packet.length = dlc > 8 ? 8 : dlc;
    packet.oversize = dlc > 8;
    if (!rtr && data) memcpy(packet.bytes, data, packet.length);
    return enqueue(&packet);
}

static bool enqueue_j1708(const uint8_t *frame, size_t length,
                            uint64_t captured_us, bool oversize, bool tx_echo)
{
    if (!frame && length) { bump(&s.queue_dropped); return false; }
    packet_t packet = { .captured_us = captured_us,
        .supplied_length = length > UINT16_MAX ? UINT16_MAX : (uint16_t)length,
        .oversize = oversize || length > TELEMETRY_FRAME_MAX, .tx_echo = tx_echo };
    packet.length = length > sizeof(packet.bytes) ? sizeof(packet.bytes) : length;
    if (frame && packet.length) memcpy(packet.bytes, frame, packet.length);
    return enqueue(&packet);
}

bool tb_telemetry_j1708(const uint8_t *frame, size_t length,
                         uint64_t captured_us, bool oversize)
{ return enqueue_j1708(frame, length, captured_us, oversize, false); }

bool tb_telemetry_j1708_echo(const uint8_t *frame, size_t length,
                             uint64_t captured_us, bool oversize)
{ return enqueue_j1708(frame, length, captured_us, oversize, true); }

static void hex(char *out, const uint8_t *bytes, size_t length)
{
    static const char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 15];
    }
    out[length * 2] = 0;
}

static void add_identity(cJSON *record, const packet_t *packet, unsigned part)
{
    cJSON_AddNumberToObject(record, "schema_version", TELEMETRY_SCHEMA);
    cJSON_AddStringToObject(record, "decoder_version", "0.2.0");
    cJSON_AddStringToObject(record, "sample_kind", part ? "DECODED" : "RAW");
    cJSON_AddStringToObject(record, "origin", packet->tx_echo ? "TX_ECHO" : "PASSIVE_RX");
    cJSON_AddNumberToObject(record, "boot_id", tb_boot_id());
    cJSON_AddNumberToObject(record, "capture_id", (double)packet->capture_id);
    cJSON_AddNumberToObject(record, "uptime_us", (double)packet->captured_us);
    cJSON_AddNumberToObject(record, "part", part);
    cJSON_AddBoolToObject(record, "time_valid", packet->wall_ms != 0);
    cJSON_AddStringToObject(record, "time_source", packet->wall_ms ? "SYSTEM_CLOCK" : "MONOTONIC_BOOT");
    if (packet->wall_ms) cJSON_AddNumberToObject(record, "unix_ms", (double)packet->wall_ms);
    /* The GPS helper refuses a fix received after the frame. In particular a
     * newly acquired fix must never be applied retroactively to a queued RX. */
    if (part) {
        /* A continuation refers to the exact raw capture's location. Never
         * resample GPS or repeat its optional fields on each sensor chunk. */
        cJSON_AddNumberToObject(record, "gps_ref_part", 0);
    } else if (!tb_core_append_gps_at(record, packet->captured_us)) {
        cJSON_AddNullToObject(record, "gps");
        cJSON_AddStringToObject(record, "gps_status", "NO_FIX");
        cJSON_AddBoolToObject(record, "has_fix", false);
    }
}

static cJSON *new_record(const packet_t *packet, unsigned part,
                         const tbx_j1939_tp_message_t *transport)
{
    cJSON *record = NULL;
    if (transport) {
        record = cJSON_CreateObject();
        if (!record) return NULL;
        cJSON_AddStringToObject(record, "source", "j1939_tp");
        cJSON_AddStringToObject(record, "transport", "TP_BAM");
        cJSON_AddNumberToObject(record, "pgn", transport->pgn);
        cJSON_AddNumberToObject(record, "sa", transport->sa);
        cJSON_AddNumberToObject(record, "tp_start_capture_id", (double)transport->start_capture_id);
        cJSON_AddNumberToObject(record, "tp_end_capture_id", (double)transport->completed_capture_id);
        if (part == 1) {
            char raw[TBX_J1939_TP_MAX_PAYLOAD * 2 + 1];
            hex(raw, transport->data, transport->length);
            cJSON_AddStringToObject(record, "raw_payload", raw);
            cJSON_AddNumberToObject(record, "payload_length", transport->length);
            cJSON_AddNumberToObject(record, "tp_start_uptime_ms", (double)transport->start_ms);
            cJSON_AddNumberToObject(record, "tp_packets", transport->packets);
        } else cJSON_AddNumberToObject(record, "payload_ref_part", 1);
    } else if (packet->can && !part && !packet->oversize) {
        char raw[TB_RECORD_MAX];
        if (tb_protocol_can_json(raw, sizeof(raw), packet->captured_us,
                packet->id, packet->extended, packet->rtr,
                packet->length, packet->bytes)) record = cJSON_Parse(raw);
    }
    if (!record) {
        record = cJSON_CreateObject();
        if (!record) return NULL;
        cJSON_AddStringToObject(record, "source", packet->can ? "can" : "j1708");
        if (packet->can) {
            cJSON_AddNumberToObject(record, "id", packet->id);
            cJSON_AddBoolToObject(record, "extended", packet->extended);
            cJSON_AddBoolToObject(record, "rtr", packet->rtr);
        } else if (packet->length) cJSON_AddNumberToObject(record, "mid", packet->bytes[0]);
        if (!part) {
            char bytes[TELEMETRY_FRAME_MAX * 2 + 1];
            hex(bytes, packet->bytes, packet->length);
            cJSON_AddStringToObject(record, "raw", bytes);
            cJSON_AddNumberToObject(record, "length", packet->supplied_length);
            if (!packet->can) cJSON_AddBoolToObject(record, "checksum_valid",
                !packet->oversize && tb_protocol_j1708_checksum_valid(packet->bytes, packet->length));
            if (packet->oversize) cJSON_AddBoolToObject(record, "raw_truncated_or_oversize", true);
        }
    }
    /* Raw CAN formatter already supplied this exact captured timestamp. */
    cJSON_DeleteItemFromObjectCaseSensitive(record, "uptime_us");
    add_identity(record, packet, part);
    return record;
}

static void flush(worker_t *worker)
{
    if (!worker->record) return;
    char json[TB_RECORD_MAX];
    if (!cJSON_PrintPreallocated(worker->record, json, sizeof(json), false)) {
        bump(&s.serialization_errors);
    } else if (tb_store_submit(json) == ESP_OK) {
        bump(&s.store_accepted);
    } else bump(&s.store_rejected);
    cJSON_Delete(worker->record);
    worker->record = worker->values = NULL;
}

static void append(worker_t *worker, cJSON *value)
{
    if (!value) { bump(&s.serialization_errors); return; }
    if (!worker->record) worker->record = new_record(worker->packet, worker->part, worker->transport);
    if (!worker->record) { cJSON_Delete(value); bump(&s.serialization_errors); return; }
    if (!worker->values) worker->values = cJSON_AddArrayToObject(worker->record, "signals");
    if (!worker->values || !cJSON_AddItemToArray(worker->values, value)) {
        cJSON_Delete(value); bump(&s.serialization_errors); return;
    }
    char scratch[TB_RECORD_MAX];
    if (cJSON_PrintPreallocated(worker->record, scratch, sizeof(scratch), false)) return;
    cJSON_DetachItemViaPointer(worker->values, value);
    flush(worker);
    ++worker->part;
    worker->record = new_record(worker->packet, worker->part, worker->transport);
    if (!worker->record) { cJSON_Delete(value); bump(&s.serialization_errors); return; }
    worker->values = cJSON_AddArrayToObject(worker->record, "signals");
    if (!worker->values || !cJSON_AddItemToArray(worker->values, value)) {
        cJSON_Delete(value); bump(&s.serialization_errors); return;
    }
    /* A single complex observation is bounded by its received frame size.
     * Still report overflow explicitly rather than ever submitting truncation. */
    if (!cJSON_PrintPreallocated(worker->record, scratch, sizeof(scratch), false)) {
        cJSON_DetachItemViaPointer(worker->values, value);
        cJSON_Delete(value);
        bump(&s.serialization_errors);
    }
}

static void update_latest(const latest_t *reading)
{
    portENTER_CRITICAL(&s_mux);
    size_t slot = TELEMETRY_LATEST;
    size_t oldest = 0;
    for (size_t i = 0; i < s.latest_count; ++i) {
        if (s.latest[i].can == reading->can && s.latest[i].signal == reading->signal &&
            s.latest[i].address == reading->address &&
            s.latest[i].component_index == reading->component_index) { slot = i; break; }
        if (s.latest[i].capture_id < s.latest[oldest].capture_id) oldest = i;
    }
    latest_t next = *reading;
    if (slot != TELEMETRY_LATEST) {
        next.has_last_valid = s.latest[slot].has_last_valid;
        next.last_valid_value = s.latest[slot].last_valid_value;
        next.last_valid_us = s.latest[slot].last_valid_us;
        next.last_valid_quality = s.latest[slot].last_valid_quality;
        next.last_valid_validation = s.latest[slot].last_valid_validation;
    } else {
        if (s.latest_count < TELEMETRY_LATEST) slot = s.latest_count++;
        else { slot = oldest; ++s.latest_evictions; }
    }
    if (reading->has_value && isfinite(reading->value) && reading->quality &&
        (!strcmp(reading->quality, "VALIDO") || !strcmp(reading->quality, "EM_VALIDACAO"))) {
        next.has_last_valid = true;
        next.last_valid_value = reading->value;
        next.last_valid_us = reading->captured_us;
        next.last_valid_quality = reading->quality;
        next.last_valid_validation = reading->validation;
    }
    s.latest[slot] = next;
    portEXIT_CRITICAL(&s_mux);
}

static cJSON *j1939_value(const tbx_j1939_observation_t *observation,
                           const char *unit, const char *validation,
                           const char *quality, bool has_value, double number)
{
    cJSON *value = cJSON_CreateObject();
    if (!value) return NULL;
    cJSON_AddNumberToObject(value, "sid", observation->signal->id);
    cJSON_AddNumberToObject(value, "sa", observation->sa);
    cJSON_AddNumberToObject(value, "pgn", observation->pgn);
    cJSON_AddStringToObject(value, "unit", unit ? unit : "raw");
    cJSON_AddStringToObject(value, "validation", validation);
    cJSON_AddStringToObject(value, "quality", quality);
    cJSON_AddStringToObject(value, "decoder", TBX_J1939_DECODER_VERSION);
    if (has_value && isfinite(number)) cJSON_AddNumberToObject(value, "value", number);
    else cJSON_AddNullToObject(value, "value");
    return value;
}

static void j1939_observation(const tbx_j1939_observation_t *observation, void *context)
{
    worker_t *worker = context;
    const tbx_j1939_signal_t *signal = observation->signal;
    const char *quality = tbx_j1939_quality_name(observation->quality);
    const char *validation = tbx_j1939_validation_name(observation->validation);
    latest_t latest = { .used = true, .can = true, .signal = signal->id,
        .address = observation->sa, .pgn = observation->pgn,
        .captured_us = worker->packet->captured_us, .capture_id = worker->packet->capture_id,
        .name = signal->name, .unit = signal->unit, .validation = validation,
        .quality = quality, .has_value = observation->has_value,
        .raw_only = observation->raw_only,
        .value = observation->value, .raw_length = observation->dlc > 32 ? 32 : observation->dlc,
        .full_raw_length = observation->dlc, .component_index = UINT8_MAX,
        .component_count = observation->component_count };
    memcpy(latest.raw, observation->raw, latest.raw_length);
    snprintf(latest.text, sizeof(latest.text), "%s", observation->text);
    update_latest(&latest);
    const bool persist = tb_config()->full_decode_enabled;
    if (persist) {
    cJSON *value = j1939_value(observation, signal->unit, validation, quality,
        observation->has_value, observation->value);
    if (!value) { bump(&s.serialization_errors); return; }
    if (observation->text[0]) cJSON_AddStringToObject(value, "text", observation->text);
    if (signal->derived) cJSON_AddBoolToObject(value, "derived", true);
    if (observation->raw_only) cJSON_AddBoolToObject(value, "raw_only", true);
    if (observation->component_count)
        cJSON_AddNumberToObject(value, "component_count", observation->component_count);
    if (observation->reference_torque_nm > 0)
        cJSON_AddNumberToObject(value, "reference_torque_nm", observation->reference_torque_nm);
    append(worker, value);
    }
    /* Components are separate bounded entries. A 30-byte torque curve must not
     * be dropped merely because all named components do not fit one SD row. */
    unsigned count = observation->component_count;
    if (count > TBX_J1939_COMPONENT_MAX) count = TBX_J1939_COMPONENT_MAX;
    for (unsigned i = 0; i < count; ++i) {
        const tbx_j1939_component_t *component = &observation->components[i];
        const char *component_quality = tbx_j1939_quality_name(component->quality);
        const char *component_validation = component->quality == TBX_J1939_PARTIAL ||
            component->quality == TBX_J1939_PROFILE ? "EM_VALIDACAO" : validation;
        latest_t member = latest;
        member.component_index = (uint8_t)i;
        member.component_count = 0;
        member.raw_only = observation->raw_only;
        member.component = component->name;
        member.unit = component->unit;
        member.quality = component_quality;
        member.validation = component_validation;
        member.has_value = component->has_value;
        member.value = component->value;
        member.text[0] = 0;
        update_latest(&member);
        if (!persist) continue;
        cJSON *entry = j1939_value(observation, component->unit, component_validation,
            component_quality, component->has_value, component->value);
        if (!entry) { bump(&s.serialization_errors); continue; }
        cJSON_AddNumberToObject(entry, "component_index", i);
        cJSON_AddStringToObject(entry, "component", component->name);
        if (signal->derived) cJSON_AddBoolToObject(entry, "derived", true);
        append(worker, entry);
    }
}

static void number_array(cJSON *array, double number)
{
    cJSON_AddItemToArray(array, cJSON_CreateNumber(number));
}

static void j1587_observation(const tbx_j1587_observation_t *observation, void *context)
{
    worker_t *worker = context;
    const char *quality = tbx_j1587_observation_quality_name(observation->quality);
    latest_t latest = { .used = true, .can = false, .signal = observation->pid,
        .address = observation->mid, .captured_us = worker->packet->captured_us,
        .capture_id = worker->packet->capture_id, .name = observation->name,
        .unit = observation->unit, .validation = "EM_VALIDACAO", .quality = quality,
        .has_value = observation->has_numeric, .value = observation->value,
        .raw_length = observation->raw_len > 32 ? 32 : observation->raw_len,
        .full_raw_length = observation->raw_len, .component_index = UINT8_MAX };
    memcpy(latest.raw, observation->raw, latest.raw_length);
    snprintf(latest.text, sizeof(latest.text), "%s", observation->passive.text);
    update_latest(&latest);
    if (!tb_config()->full_decode_enabled) return; /* update PID cache without per-frame JSON */
    cJSON *value = cJSON_CreateObject();
    if (!value) { bump(&s.serialization_errors); return; }
    char raw[65];
    hex(raw, latest.raw, latest.raw_length);
    cJSON_AddNumberToObject(value, "pid", observation->pid);
    cJSON_AddNumberToObject(value, "mid", observation->mid);
    cJSON_AddStringToObject(value, "raw", raw);
    cJSON_AddStringToObject(value, "unit", observation->unit ? observation->unit : "raw");
    cJSON_AddStringToObject(value, "validation", "EM_VALIDACAO");
    cJSON_AddStringToObject(value, "quality", quality);
    cJSON_AddStringToObject(value, "decoder", TBX_J1587_DECODER_VERSION);
    if (observation->has_numeric && isfinite(observation->value))
        cJSON_AddNumberToObject(value, "value", observation->value);
    else cJSON_AddNullToObject(value, "value");
    if (observation->passive.text[0]) cJSON_AddStringToObject(value, "text", observation->passive.text);
    if (observation->passive.field_count) {
        cJSON *fields = cJSON_AddArrayToObject(value, "states");
        for (unsigned i = 0; i < observation->passive.field_count; ++i) {
            const tbx_j1587_state_field_t *field = &observation->passive.fields[i];
            cJSON *entry = cJSON_CreateArray();
            cJSON_AddItemToArray(fields, entry);
            number_array(entry, field->state); number_array(entry, field->width);
        }
    }
    if (observation->requested.bitmapped) {
        cJSON *states = cJSON_AddArrayToObject(value, "state_pairs_lsb_first");
        for (unsigned i = 0; i < 4; ++i) number_array(states, observation->requested.status_fields[i]);
    }
    const tbx_j1587_full_reading_t *full = &observation->passive;
    if (full->interpreted && full->kind == TBX_J1587_KIND_DTCS) {
        cJSON *dtcs = cJSON_AddArrayToObject(value, "dtcs");
        for (unsigned i = 0; i < full->dtc_count; ++i) {
            const tbx_j1587_dtc_t *dtc = &full->dtcs[i];
            cJSON *entry = cJSON_CreateArray();
            cJSON_AddItemToArray(dtcs, entry);
            number_array(entry, dtc->identifier); number_array(entry, dtc->fmi);
            number_array(entry, dtc->sid); number_array(entry, dtc->page2);
            number_array(entry, dtc->active); number_array(entry, dtc->has_occurrences);
            number_array(entry, dtc->occurrences);
        }
    } else if (full->interpreted && full->kind == TBX_J1587_KIND_CLOCK) {
        cJSON *clock = cJSON_AddArrayToObject(value, "clock_hms");
        number_array(clock, full->hour); number_array(clock, full->minute); number_array(clock, full->second);
    } else if (full->interpreted && full->kind == TBX_J1587_KIND_DATE) {
        cJSON *date = cJSON_AddArrayToObject(value, "date_ymd");
        number_array(date, full->year); number_array(date, full->month); number_array(date, full->day);
    } else if (full->interpreted && full->kind == TBX_J1587_KIND_REQUEST) {
        cJSON_AddNumberToObject(value, "requested_pid", full->requested_pid);
        cJSON_AddNumberToObject(value, "target_mid", full->target_mid);
    }
    append(worker, value);
}

static void transport_snapshot(void)
{
    const tbx_j1939_tp_t *transport = &s.transport;
    const uint32_t snapshot[] = {
        transport->announcements, transport->completed, transport->timeouts,
        transport->invalid_length, transport->invalid_sequence, transport->orphan_packets,
        transport->unsupported_control, transport->unsupported_pgn,
        transport->invalid_destination, transport->capacity_rejected, transport->restarted
    };
    portENTER_CRITICAL(&s_mux);
    memcpy(s.transport_stats, snapshot, sizeof(snapshot));
    portEXIT_CRITICAL(&s_mux);
}

static void transport_complete(const tbx_j1939_tp_message_t *message, void *context)
{
    const packet_t *packet = context;
    worker_t worker = { .packet = packet, .transport = message, .part = 1 };
    if (tb_config()->full_decode_enabled) {
        worker.record = new_record(packet, worker.part, message);
        if (!worker.record) { bump(&s.serialization_errors); return; }
    }
    tbx_j1939_decode_payload(&s.j1939, message->pgn, message->sa,
        message->data, message->length, message->completed_ms, j1939_observation, &worker);
    flush(&worker);
}

static void process_packet(const packet_t *packet)
{
    worker_t worker = { .packet = packet };
    if (tb_config()->full_decode_enabled) {
        worker.record = new_record(packet, 0, NULL);
        if (!worker.record) { bump(&s.serialization_errors); return; }
    }
    if (packet->can) {
        if (!packet->oversize) tbx_j1939_decode(&s.j1939, packet->id,
            packet->extended, packet->rtr, packet->bytes, packet->length,
            packet->captured_us / 1000, j1939_observation, &worker);
        /* Commit the unchanged physical CAN frame before handling a completed
         * BAM message. Reassembled payloads have their own PGN/SA/source and
         * exact CM/last-DT capture references; they are never a CAN DLC >8. */
        flush(&worker);
        if (!packet->oversize) tbx_j1939_tp_feed_capture(&s.transport, packet->id,
            packet->extended, packet->rtr, packet->bytes, packet->length,
            packet->captured_us / 1000, packet->capture_id, transport_complete, (void *)packet);
        transport_snapshot();
    } else {
        tbx_j1587_frame_result_t result = packet->oversize
            ? TBX_J1587_FRAME_LENGTH_INVALID
            : tbx_j1587_bridge_decode(packet->bytes, packet->length,
                packet->captured_us / 1000, NULL, NULL);
        if (worker.record) cJSON_AddStringToObject(worker.record, "j1587_result", packet->tx_echo
            ? "TX_ECHO" : tbx_j1587_frame_result_name(result));
        if (!packet->tx_echo && result == TBX_J1587_FRAME_OK) tbx_j1587_bridge_decode(packet->bytes,
            packet->length, packet->captured_us / 1000, j1587_observation, &worker);
    }
    flush(&worker);
    bump(&s.processed);
}

static void worker_task(void *unused)
{
    (void)unused;
    packet_t packet;
    tb_task_budget_t budget;
    tb_task_budget_reset(&budget, esp_timer_get_time());
    for (;;) {
        if (xQueueReceive(s.queue, &packet, pdMS_TO_TICKS(250)) == pdTRUE) {
            process_packet(&packet);
            if (tb_task_budget_due(&budget, esp_timer_get_time())) {
                bump(&s.scheduling_yields);
                vTaskDelay(TB_TASK_REST_TICKS);
                tb_task_budget_reset(&budget, esp_timer_get_time());
            }
        } else {
            tbx_j1939_tp_expire(&s.transport, esp_timer_get_time() / 1000);
            transport_snapshot();
            tb_task_budget_reset(&budget, esp_timer_get_time());
        }
    }
}

esp_err_t tb_telemetry_start(void)
{
    if (s.queue) return ESP_ERR_INVALID_STATE;
    tbx_j1939_init(&s.j1939);
    tbx_j1939_tp_init(&s.transport);
    s.queue = xQueueCreate(TELEMETRY_QUEUE, sizeof(packet_t));
    if (!s.queue) return ESP_ERR_NO_MEM;
    /* Below RX and storage priority. Logging cannot starve the SD writer. */
    if (xTaskCreate(worker_task, "tb_decode", 10240, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(s.queue); s.queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void tb_telemetry_add_json(cJSON *object)
{
    cJSON *telemetry = cJSON_AddObjectToObject(object, "telemetry_v2");
    if (!telemetry) return;
    portENTER_CRITICAL(&s_mux);
    uint64_t captured = s.captured, dropped = s.queue_dropped,
        accepted = s.store_accepted, rejected = s.store_rejected,
        errors = s.serialization_errors, processed = s.processed,
        evictions = s.latest_evictions, yields = s.scheduling_yields;
    size_t latest_count = s.latest_count;
    uint32_t transport_stats[11];
    memcpy(transport_stats, s.transport_stats, sizeof(transport_stats));
    portEXIT_CRITICAL(&s_mux);
    cJSON_AddNumberToObject(telemetry, "schema_version", TELEMETRY_SCHEMA);
    cJSON_AddBoolToObject(telemetry, "decoding_enabled", s.queue != NULL);
    cJSON_AddBoolToObject(telemetry, "persist_raw_decoded", tb_config()->full_decode_enabled);
    cJSON_AddNumberToObject(telemetry, "captured_frames", (double)captured);
    cJSON_AddNumberToObject(telemetry, "processed_frames", (double)processed);
    cJSON_AddNumberToObject(telemetry, "scheduling_yields", (double)yields);
    cJSON_AddNumberToObject(telemetry, "burst_max_frames", TB_TASK_BURST_MAX);
    cJSON_AddNumberToObject(telemetry, "burst_budget_us", TB_TASK_BURST_US);
    cJSON_AddNumberToObject(telemetry, "rest_ticks", TB_TASK_REST_TICKS);
    cJSON_AddNumberToObject(telemetry, "rx_queue_dropped", (double)dropped);
    cJSON_AddNumberToObject(telemetry, "store_queue_accepted", (double)accepted);
    cJSON_AddNumberToObject(telemetry, "store_rejected", (double)rejected);
    cJSON_AddNumberToObject(telemetry, "serialization_errors", (double)errors);
    cJSON_AddNumberToObject(telemetry, "queue_capacity_frames", TELEMETRY_QUEUE);
    cJSON_AddNumberToObject(telemetry, "latest_readings", latest_count);
    cJSON_AddNumberToObject(telemetry, "latest_capacity", TELEMETRY_LATEST);
    cJSON_AddNumberToObject(telemetry, "latest_evictions", (double)evictions);
    cJSON_AddStringToObject(telemetry, "latest_endpoint", "/api/sensors?offset=0&limit=16");
    cJSON_AddStringToObject(telemetry, "acquisition", "PASSIVE");
    cJSON *transport = cJSON_AddObjectToObject(telemetry, "j1939_tp_bam");
    static const char *const names[] = {
        "announcements", "completed", "timeouts", "invalid_length", "invalid_sequence",
        "orphan_packets", "unsupported_control", "unsupported_pgn", "invalid_destination",
        "capacity_rejected", "restarted"
    };
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        cJSON_AddNumberToObject(transport, names[i], transport_stats[i]);
    cJSON_AddNumberToObject(transport, "session_capacity", TBX_J1939_TP_SESSIONS);
    cJSON_AddNumberToObject(transport, "maximum_payload_bytes", TBX_J1939_TP_MAX_PAYLOAD);
}



/* Rev.B1 passive J1587 dual-lane export. Never enables TX.
 * Each record must fit TB_RECORD_MAX (768 bytes); no per-frame RAW flash writes.
 * Source ages and MIDs travel with the decoded values, and the original bytes
 * of the unvalidated economy readings 184/185 are preserved for comparison.
 */
static const uint16_t s_j1587_fast_pids[] = {
    190, 91, 92, 183, 184, 185, 110, 175, 105, 177,
    100, 102, 440, 46, 168, 84, 245, 122, 70, 85,
    86, 43, 44, 49, 151, 162, 163
};
enum { TB_J1587_FAST_COUNT = sizeof(s_j1587_fast_pids) / sizeof(s_j1587_fast_pids[0]) };
_Static_assert(TB_J1587_FAST_COUNT == 27, "27 fast J1587 PIDs required");

/* Twelve slow PIDs, then five backwards-compatible display fallbacks,
 * then the seventeen already catalogued request targets (passive RX only).
 * Request candidates rotate their priority to avoid permanent starvation. */
static const uint16_t s_j1587_health_pids[] = {
    36, 98, 111, 247, 89, 150, 187, 71, 83, 97, 194, 128,
    110, 175, 100, 102, 177,
    26, 33, 34, 37, 62, 68, 117, 118, 235, 236, 250, 31, 32, 63, 108, 158, 160
};
enum { TB_J1587_SLOW_COUNT = 12, TB_J1587_COMPAT_COUNT = 5,
       TB_J1587_REQUEST_COUNT = 17,
       TB_J1587_HEALTH_COUNT = sizeof(s_j1587_health_pids) / sizeof(s_j1587_health_pids[0]) };
_Static_assert(TB_J1587_HEALTH_COUNT == 34, "J1587 health catalog mismatch");

static bool j1587_cached(uint16_t pid, uint64_t now_us, uint64_t max_age_us, latest_t *out)
{
    latest_t match = {0};
    portENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < s.latest_count; ++i) {
        const latest_t *candidate = &s.latest[i];
        if (!candidate->used || candidate->can || candidate->component_index != UINT8_MAX ||
            candidate->signal != pid) continue;
        if (!match.used || candidate->captured_us > match.captured_us)
            match = *candidate;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!match.used || !match.captured_us || now_us < match.captured_us ||
        now_us - match.captured_us > max_age_us)
        return false;
    /* Do not present unsupported/invalid readings as valid sensor values. */
    if (!match.quality || strcmp(match.quality, "EM_VALIDACAO") != 0)
        return false;
    *out = match;
    return true;
}

static bool j1587_record_fits(cJSON *record)
{
    char json[TB_RECORD_MAX];
    return record && cJSON_PrintPreallocated(record, json, sizeof(json), false);
}

static bool j1587_add_observation(cJSON *record, uint16_t pid, const latest_t *latest,
                                  uint64_t now_us, bool preserve_raw)
{
    const bool numeric = latest->has_value && isfinite(latest->value);
    const char *field = numeric ? "j1587_sensors" : "j1587_raw";
    cJSON *bucket = cJSON_GetObjectItemCaseSensitive(record, field);
    if (!bucket) bucket = cJSON_AddObjectToObject(record, field);
    if (!bucket) return false;
    char key[6];
    snprintf(key, sizeof(key), "%u", (unsigned)pid);
    cJSON *entry = cJSON_AddArrayToObject(bucket, key);
    if (!entry) return false;
    char bytes[sizeof(latest->raw) * 2u + 1u];
    if (numeric) cJSON_AddItemToArray(entry, cJSON_CreateNumber(latest->value));
    else {
        hex(bytes, latest->raw, latest->raw_length);
        cJSON_AddItemToArray(entry, cJSON_CreateString(bytes));
    }
    /* The receiving Cloud can calculate the original observation time. */
    cJSON_AddItemToArray(entry, cJSON_CreateNumber(
        (double)((now_us - latest->captured_us) / UINT64_C(1000000))));
    cJSON_AddItemToArray(entry, cJSON_CreateNumber(latest->address));
    if (numeric && preserve_raw) {
        hex(bytes, latest->raw, latest->raw_length);
        cJSON_AddItemToArray(entry, cJSON_CreateString(bytes));
    }
    if (!j1587_record_fits(record)) {
        cJSON_DeleteItemFromObjectCaseSensitive(bucket, key);
        if (!bucket->child) cJSON_DeleteItemFromObjectCaseSensitive(record, field);
        return false;
    }
    return true;
}

/* Adds provisional scalar aliases only where absent, so J1939 remains primary.
 * This makes the already-supported SQL telemetry columns useful even before
 * dashboard consumers are upgraded to the richer j1587_fast format. */
bool tb_telemetry_append_j1587_operational(cJSON *sample)
{
    if (!sample) return false;
    static const struct { uint16_t pid; const char *key; } fields[] = {
        {110,"coolant_temp_c"}, {175,"oil_temp_c"}, {100,"oil_pressure_bar"},
        {102,"boost_bar"}, {440,"boost_bar"}, {177,"gearbox_temp_c"},
        {105,"intake_temp_c"}, {92,"load_pct"}, {91,"pedal_pct"},
        {168,"battery_voltage_v"}, {46,"air_pressure_bar"},
        {122,"veb_pct"}, {184,"j1587_instant_kml"}, {185,"j1587_average_kml"}
    };
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    bool added = false;
    for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); ++i) {
        if (cJSON_GetObjectItemCaseSensitive(sample, fields[i].key)) continue;
        latest_t latest = {0};
        const uint64_t max_age = fields[i].pid == 184 || fields[i].pid == 185
            ? UINT64_C(90000000) : UINT64_C(40000000);
        if (!j1587_cached(fields[i].pid, now_us, max_age, &latest) ||
            !latest.has_value || !isfinite(latest.value)) continue;
        if (!cJSON_AddNumberToObject(sample, fields[i].key, latest.value)) continue;
        if (!j1587_record_fits(sample)) {
            cJSON_DeleteItemFromObjectCaseSensitive(sample, fields[i].key);
            continue;
        }
        added = true;
    }
    return added;
}

/* Cursor-based packing permits every currently observed fast PID to go out
 * in the same 10-second cycle, split into bounded records if necessary.
 * The caller owns the record identity and persists only nonempty chunks. */
size_t tb_telemetry_append_j1587_fast(cJSON *fast, size_t *cursor)
{
    if (!fast || !cursor || *cursor >= TB_J1587_FAST_COUNT) return 0;
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    size_t count = 0;
    while (*cursor < TB_J1587_FAST_COUNT) {
        uint16_t pid = s_j1587_fast_pids[*cursor];
        latest_t latest = {0};
        const uint64_t max_age = (pid == 184 || pid == 185 || pid == 245)
            ? UINT64_C(90000000) : UINT64_C(40000000);
        if (!j1587_cached(pid, now_us, max_age, &latest)) {
            ++*cursor;
            continue;
        }
        if (!j1587_add_observation(fast, pid, &latest, now_us,
                                   pid == 184 || pid == 185)) {
            /* When a chunk is full retry this PID in the next chunk.
             * Skip a single impossible observation to guarantee progress. */
            if (count) break;
            ++*cursor;
            continue;
        }
        ++*cursor;
        ++count;
    }
    return count;
}

/* HEALTH stays at 60 seconds. Slow health readings have first priority;
 * the legacy display fallbacks and parked-only request targets come after.
 * No additional J1708 requests and no raw traffic dumps are produced here. */
void tb_telemetry_append_j1587_health(cJSON *health)
{
    if (!health) return;
    cJSON *metrics = cJSON_GetObjectItemCaseSensitive(health, "metrics");
    cJSON *context = cJSON_GetObjectItemCaseSensitive(health, "context");
    if (!cJSON_IsObject(metrics) || !cJSON_IsObject(context)) return;
    const uint64_t now_us = (uint64_t)esp_timer_get_time();
    static uint32_t rotation;
    const unsigned rotate = rotation++ % TB_J1587_REQUEST_COUNT;
    for (size_t i = 0; i < TB_J1587_HEALTH_COUNT; ++i) {
        size_t index = i;
        if (i >= TB_J1587_SLOW_COUNT + TB_J1587_COMPAT_COUNT)
            index = TB_J1587_SLOW_COUNT + TB_J1587_COMPAT_COUNT +
                    ((i - TB_J1587_SLOW_COUNT - TB_J1587_COMPAT_COUNT + rotate)
                     % TB_J1587_REQUEST_COUNT);
        uint16_t pid = s_j1587_health_pids[index];
        latest_t latest = {0};
        if (!j1587_cached(pid, now_us, UINT64_C(300000000), &latest)) continue;
        /* Use the existing Cloud HEALTH schema; keep room for the other
         * diagnostic counters and prevent oversized flash writes. */
        const bool numeric = latest.has_value && isfinite(latest.value);
        const char *field = numeric ? "j1587_sensors" : "j1587_raw";
        cJSON *target = cJSON_GetObjectItemCaseSensitive(metrics, field);
        if (!target) target = cJSON_AddObjectToObject(metrics, field);
        if (!target) continue;
        char key[6], bytes[sizeof(latest.raw)*2u+1u];
        snprintf(key, sizeof(key), "%u", (unsigned)pid);
        cJSON *entry = cJSON_AddArrayToObject(target, key);
        if (!entry) continue;
        if (numeric) cJSON_AddItemToArray(entry, cJSON_CreateNumber(latest.value));
        else {
            hex(bytes, latest.raw, latest.raw_length);
            cJSON_AddItemToArray(entry, cJSON_CreateString(bytes));
        }
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(
            (double)((now_us-latest.captured_us)/UINT64_C(1000000))));
        cJSON_AddItemToArray(entry, cJSON_CreateNumber(latest.address));
        if (!j1587_record_fits(health)) {
            cJSON_DeleteItemFromObjectCaseSensitive(target, key);
            if (!target->child) cJSON_DeleteItemFromObjectCaseSensitive(metrics, field);
        }
    }
    cJSON_Delete(sensor_data);
    if (cJSON_GetObjectItemCaseSensitive(metrics, "j1587_sensors") ||
        cJSON_GetObjectItemCaseSensitive(metrics, "j1587_raw"))
        cJSON_AddStringToObject(context, "j1587_sensor_quality", "EM_VALIDACAO");
    /* If quality metadata itself pushes the record over the storage cap,
     * remove metadata, never the window counters or entire HEALTH record. */
    if (!j1587_record_fits(health))
        cJSON_DeleteItemFromObjectCaseSensitive(context, "j1587_sensor_quality");
}

cJSON *tb_telemetry_sensors_json(size_t offset, size_t limit)
{
    if (limit < 1) limit = 1;
    if (limit > 32) limit = 32;
    latest_t *snapshot = calloc(limit, sizeof(*snapshot));
    if (!snapshot) return NULL;
    size_t copied = 0, total;
    portENTER_CRITICAL(&s_mux);
    total = s.latest_count;
    while (offset + copied < total && copied < limit) {
        snapshot[copied] = s.latest[offset + copied]; ++copied;
    }
    portEXIT_CRITICAL(&s_mux);
    cJSON *result = cJSON_CreateObject();
    if (!result) { free(snapshot); return NULL; }
    cJSON_AddBoolToObject(result, "ok", true);
    cJSON_AddNumberToObject(result, "schema_version", TELEMETRY_SCHEMA);
    cJSON_AddNumberToObject(result, "offset", offset);
    cJSON_AddNumberToObject(result, "count", copied);
    cJSON_AddNumberToObject(result, "observed_count", total);
    cJSON_AddNumberToObject(result, "next_offset", offset + copied < total ? offset + copied : total);
    cJSON *readings = cJSON_AddArrayToObject(result, "readings");
    uint64_t now = esp_timer_get_time();
    for (size_t i = 0; i < copied; ++i) {
        const latest_t *reading = &snapshot[i];
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddItemToArray(readings, entry);
        cJSON_AddStringToObject(entry, "source", reading->can ? "j1939" : "j1587");
        cJSON_AddNumberToObject(entry, reading->can ? "sid" : "pid", reading->signal);
        cJSON_AddNumberToObject(entry, reading->can ? "sa" : "mid", reading->address);
        if (reading->can) cJSON_AddNumberToObject(entry, "pgn", reading->pgn);
        if (reading->component_index != UINT8_MAX) {
            cJSON_AddNumberToObject(entry, "component_index", reading->component_index);
            cJSON_AddStringToObject(entry, "component", reading->component);
        }
        if (reading->component_count) cJSON_AddNumberToObject(entry, "component_count", reading->component_count);
        if (reading->raw_only) cJSON_AddBoolToObject(entry, "raw_only", true);
        cJSON_AddStringToObject(entry, "name", reading->name ? reading->name : "unknown");
        cJSON_AddStringToObject(entry, "unit", reading->unit ? reading->unit : "raw");
        cJSON_AddStringToObject(entry, "validation", reading->validation);
        cJSON_AddStringToObject(entry, "quality", reading->quality);
        cJSON_AddNumberToObject(entry, "capture_id", (double)reading->capture_id);
        cJSON_AddNumberToObject(entry, "boot_id", tb_boot_id());
        cJSON_AddNumberToObject(entry, "uptime_us", (double)reading->captured_us);
        uint64_t age_ms = now >= reading->captured_us ? (now - reading->captured_us) / 1000 : 0;
        cJSON_AddNumberToObject(entry, "age_ms", (double)age_ms);
        cJSON_AddBoolToObject(entry, "stale", age_ms > TELEMETRY_STALE_MS);
        if (reading->has_value && isfinite(reading->value)) cJSON_AddNumberToObject(entry, "value", reading->value);
        else cJSON_AddNullToObject(entry, "value");
        if (reading->text[0]) cJSON_AddStringToObject(entry, "text", reading->text);
        if (reading->has_last_valid) {
            uint64_t age = now >= reading->last_valid_us ? (now - reading->last_valid_us) / 1000 : 0;
            cJSON_AddNumberToObject(entry, "last_valid_value", reading->last_valid_value);
            cJSON_AddNumberToObject(entry, "last_valid_age_ms", (double)age);
            cJSON_AddBoolToObject(entry, "last_valid_stale", age > TELEMETRY_STALE_MS);
            cJSON_AddStringToObject(entry, "last_valid_quality", reading->last_valid_quality);
            cJSON_AddStringToObject(entry, "last_valid_validation", reading->last_valid_validation);
        }
        char raw[65]; hex(raw, reading->raw, reading->raw_length);
        cJSON_AddStringToObject(entry, "raw", raw);
        cJSON_AddNumberToObject(entry, "raw_length", reading->full_raw_length);
        if (reading->full_raw_length > reading->raw_length)
            cJSON_AddBoolToObject(entry, "raw_truncated_in_latest", true);
    }
    free(snapshot);
    return result;
}
