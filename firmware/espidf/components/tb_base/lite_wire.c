#include "tb_lite_wire.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const tb_lite_signal_t tb_lite_signals[TB_LITE_FIELDS] = {
    [TB_LITE_SPEED] = {84, 0, 5000, "speed_kmh"},
    [TB_LITE_RPM] = {190, 128, 5000, "rpm"},
    [TB_LITE_PEDAL] = {91, 128, 5000, "pedal_pct"},
    [TB_LITE_GEAR] = {163, 130, 5000, "gear"},
    [TB_LITE_SELECTED_GEAR] = {162, 130, 5000, "selected_gear"},
    [TB_LITE_ODOMETER] = {245, 0, 30000, "odometer_km"},
    [TB_LITE_PID250] = {250, 0, 30000, "ecu_fuel_total_l"},
    [TB_LITE_FUEL_RATE] = {183, 128, 10000, "fuel_rate_lph"},
    [TB_LITE_TORQUE] = {93, 128, 65000, "torque_pct"},
    [TB_LITE_OIL_PRESSURE] = {100, 128, 40000, "oil_pressure_bar"},
    [TB_LITE_FUEL_PRESSURE] = {94, 128, 15000, "fuel_pressure_bar"},
    [TB_LITE_BOOST] = {102, 128, 40000, "boost_bar"},
    [TB_LITE_AIR1] = {117, 140, 30000, "brake_air_primary_bar"},
    [TB_LITE_AIR2] = {118, 140, 30000, "brake_air_secondary_bar"},
    [TB_LITE_COOLANT] = {110, 128, 40000, "coolant_temp_c"},
    [TB_LITE_OIL_TEMP] = {175, 128, 40000, "oil_temp_c"},
    [TB_LITE_INTAKE] = {105, 128, 40000, "intake_temp_c"},
    [TB_LITE_GEARBOX] = {177, 130, 40000, "gearbox_temp_c"},
    [TB_LITE_LOAD] = {92, 128, 40000, "load_pct"},
};

void tb_lite_fuel_observe(tb_lite_fuel_meter_t *meter, uint64_t at_ms, double lph)
{
    if (!at_ms || at_ms < meter->at_ms) return;
    const bool valid = isfinite(lph) && lph >= 0 && lph <= TB_LITE_FUEL_MAX_LPH;
    const uint64_t elapsed = at_ms - meter->at_ms;
    if (meter->last_valid && valid && elapsed <= TB_LITE_FUEL_GAP_MS) {
        meter->litres += meter->last_lph * (double)elapsed / 3600000.0;
        meter->covered_ms += elapsed;
    }
    meter->at_ms = at_ms;
    meter->last_lph = lph;
    meter->last_valid = valid;
}

bool tb_lite_fuel_delta(const tb_lite_fuel_counter_t *previous, const tb_lite_fuel_counter_t *current,
                        tb_lite_fuel_window_t *window)
{
    if (!previous->at_ms || current->at_ms <= previous->at_ms ||
        current->covered_ms < previous->covered_ms || current->microlitres < previous->microlitres) return false;
    const tb_lite_fuel_window_t result = {.span_ms = current->at_ms - previous->at_ms,
        .covered_ms = current->covered_ms - previous->covered_ms,
        .microlitres = current->microlitres - previous->microlitres};
    if (result.covered_ms > result.span_ms ||
        (double)result.microlitres > TB_LITE_FUEL_MAX_LPH * (double)result.covered_ms / 3.6 + 1) return false;
    *window = result;
    return true;
}

bool tb_lite_uid_valid(const char *uid)
{
    if (!uid || !uid[0] || strlen(uid) > 64) return false;
    for (; *uid; ++uid) {
        if (!((*uid >= 'a' && *uid <= 'z') || (*uid >= 'A' && *uid <= 'Z') ||
              (*uid >= '0' && *uid <= '9') || *uid == '-' || *uid == '_')) return false;
    }
    return true;
}

void tb_fuel_append_windows(tb_lite_buffer_t *out, const tb_fuel_window_t *windows, unsigned count)
{
    tb_lite_append(out, ",\"w183\":[");
    for (unsigned i = 0; i < count; ++i) {
        const tb_fuel_window_t *w = &windows[i];
        tb_lite_append(out, "%s[%" PRIu32 ",%" PRIu64 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
            ",%" PRIu32 ",%" PRIu32 ",%" PRIu64, i ? "," : "", w->slot, w->end_utc_ms,
            w->ul, w->covered_ms, w->readings, w->min_mlph, w->max_mlph, w->total_ul);
        const tb_fuel_anchor_t *a = &w->counter;
        if (a->at_ms) tb_lite_append(out, ",%" PRIu64 ",%" PRIu32 ",%u,%u,%" PRIu64 ",%" PRIu64,
            a->at_ms, a->ticks, a->mid, a->flags, a->observed_ul, a->covered_ms);
        tb_lite_append(out, "]");
    }
    tb_lite_append(out, "]");
}

bool tb_lite_append(tb_lite_buffer_t *buf, const char *format, ...)
{
    if (buf->failed || buf->used >= buf->capacity) return false;
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buf->data + buf->used, buf->capacity - buf->used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= buf->capacity - buf->used) {
        buf->failed = true;
        return false;
    }
    buf->used += (size_t)count;
    return true;
}

size_t tb_lite_snapshot_encode(const tb_lite_snapshot_t *s, char *buffer, size_t capacity)
{
    if (!s || !buffer || !capacity || !tb_lite_uid_valid(s->uid) || !s->boot_id ||
        s->local_seq > TB_LITE_SAFE_SEQ || (s->valid | s->stale) & ~TB_LITE_ALL ||
        (s->valid & s->stale) || (s->estimated & ~s->valid)) return 0;
    tb_lite_buffer_t out = {.data=buffer, .capacity=capacity};
    tb_lite_append(&out, "{\"v\":1,\"device_uid\":\"%s\",\"boot_id\":%" PRIu32
        ",\"local_seq\":%" PRIu64 ",\"uptime_ms\":%" PRIu64 ",\"unix_ms\":%" PRIu64
        ",\"age_ms\":%" PRIu32 ",\"valid\":%" PRIu32 ",\"stale\":%" PRIu32
        ",\"estimated\":%" PRIu32 ",\"frames\":%" PRIu32 ",\"sent\":%" PRIu32
        ",\"answered\":%" PRIu32 ",\"timeouts\":%" PRIu32 ",\"guard\":%" PRIu32
        ",\"heap_minimum\":%" PRIu32, s->uid, s->boot_id, s->local_seq,
        s->uptime_ms, s->unix_ms, s->age_ms, s->valid, s->stale, s->estimated, s->frames,
        s->sent, s->answered, s->timeouts, s->guard, s->heap_minimum);
    if (s->reset_reason_valid) tb_lite_append(&out, ",\"reset_reason\":%" PRIu32, s->reset_reason);
    if (s->flow.at_ms) tb_lite_append(&out, ",\"f183\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]",
        s->flow.at_ms, s->flow.covered_ms, s->flow.microlitres);
    if (s->pid250_at_ms && s->odometer_at_ms) tb_lite_append(&out, ",\"a250\":[%u,%u,%" PRIu64 ",%" PRIu64 "]",
        s->pid250_mid, s->odometer_mid, s->pid250_at_ms, s->odometer_at_ms);
    if (s->fuel_v2) tb_fuel_append_windows(&out, s->fuel_windows, s->fuel_count);
    tb_lite_append(&out, ",\"values\":{");
    bool first = true;
    for (unsigned i = 0; i < TB_LITE_FIELDS; ++i) {
        if (!(s->valid & TB_LITE_BIT(i))) continue;
        if (!isfinite(s->values[i])) return 0;
        tb_lite_append(&out, "%s\"%s\":%.11g", first ? "" : ",", tb_lite_signals[i].key, s->values[i]);
        first = false;
    }
    tb_lite_append(&out, "}}");
    return out.failed ? 0 : out.used;
}
