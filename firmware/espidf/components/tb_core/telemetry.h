#pragma once

#include "tb.h"

/* Acquisition only copies a bounded packet into RAM; decoding, JSON, file and
 * network work belongs to the worker. A false result is a reported RX gap. */
esp_err_t tb_telemetry_start(void);
bool tb_telemetry_can(uint32_t id, bool extended, bool rtr, uint8_t dlc,
                       const uint8_t *data, uint64_t captured_us);
bool tb_telemetry_j1708(const uint8_t *frame, size_t length,
                         uint64_t captured_us, bool oversize);
/* Exact matching against a locally transmitted request belongs to the active
 * controller. Echoes are logged but never treated as a vehicle sensor. */
bool tb_telemetry_j1708_echo(const uint8_t *frame, size_t length,
                             uint64_t captured_us, bool oversize);
void tb_telemetry_add_json(cJSON *object);
/* Observed readings only. Catalog coverage is a separate decoder concern.
 * Pagination bounds both HTTP heap use and time inside the snapshot lock. */
cJSON *tb_telemetry_sensors_json(size_t offset, size_t limit);

/* Provisional passive J1587 signals, never trigger an active J1708 request.
 * Both routines respect TB_RECORD_MAX and return only observed values. */
bool tb_telemetry_append_j1587_operational(cJSON *sample);
size_t tb_telemetry_append_j1587_fast(cJSON *fast, size_t *cursor);
void tb_telemetry_append_j1587_health(cJSON *health);

size_t tb_telemetry_append_j1587_slow(cJSON *record,size_t *cursor);

#include "tbx_transmission.h"
void tb_telemetry_transmission_can(tbx_transmission_t *monitor);
size_t tb_telemetry_transmission_snapshot(cJSON *record,size_t *cursor,uint64_t now_us);

cJSON *tb_telemetry_volvo_json(void);
