#include "tb_lite_wire.h"
#include <limits.h>
#include <math.h>

static uint64_t microlitres(uint64_t product)
{
    /* 1 mL/h × 1 ms is 1/3600 µL. Round the cumulative value only. */
    return (product + 1800) / 3600;
}

static uint64_t end_ms(const tb_fuel_meter_t *meter)
{
    return (uint64_t)meter->open.slot * TB_LITE_OPERATIONAL_MS;
}

static void init_open(tb_fuel_meter_t *meter)
{
    if (!meter->open.slot) meter->open.slot = 1;
}

static void seal(tb_fuel_meter_t *meter)
{
    tb_fuel_window_t *window = &meter->open;
    const uint64_t total_ul = microlitres(meter->product);
    if (window->counter.at_ms >= end_ms(meter)) window->counter = (tb_fuel_anchor_t){0};
    window->ul = (uint32_t)(total_ul - window->total_ul);
    window->total_ul = total_ul;
    meter->ring[(window->slot - 1) % TB_FUEL_HISTORY] = *window;
    *window = (tb_fuel_window_t){.slot = window->slot + 1, .total_ul = total_ul, .counter = meter->last_counter};
}

static void seal_through(tb_fuel_meter_t *meter, uint64_t cutoff_ms)
{
    uint64_t final_slot = cutoff_ms / TB_LITE_OPERATIONAL_MS;
    if (final_slot >= UINT32_MAX || final_slot < meter->open.slot) return;
    seal(meter);
    if (final_slot >= (uint64_t)meter->open.slot + TB_FUEL_HISTORY)
        meter->open.slot = (uint32_t)(final_slot - TB_FUEL_HISTORY + 1);
    while (end_ms(meter) <= cutoff_ms) seal(meter);
}

static void complete_anchor(tb_fuel_anchor_t *anchor, uint64_t from, uint64_t to, const tb_fuel_meter_t *meter)
{
    if (!anchor->at_ms || anchor->at_ms < from || anchor->at_ms > to) return;
    const uint64_t elapsed = anchor->at_ms - from;
    anchor->observed_ul = microlitres(meter->product + elapsed * meter->rate_mlph);
    anchor->covered_ms = meter->covered_ms + elapsed;
}

void tb_fuel_observe(tb_fuel_meter_t *meter, uint64_t at_ms, double lph)
{
    init_open(meter);
    if (at_ms < meter->last_at || at_ms < end_ms(meter) - TB_LITE_OPERATIONAL_MS) {
        ++meter->invalid_count;
        return;
    }
    const bool valid = isfinite(lph) && lph >= 0 && lph <= TB_LITE_FUEL_MAX_LPH;
    const uint64_t elapsed = at_ms - meter->last_at;
    if (meter->valid && valid && elapsed <= TB_LITE_FUEL_GAP_MS) {
        uint64_t from = meter->last_at;
        while (from < at_ms) {
            const uint64_t boundary = end_ms(meter);
            const uint64_t to = at_ms < boundary ? at_ms : boundary;
            complete_anchor(&meter->last_counter, from, to, meter);
            complete_anchor(&meter->open.counter, from, to, meter);
            complete_anchor(&meter->trip_reference, from, to, meter);
            meter->product += (to - from) * meter->rate_mlph;
            meter->covered_ms += to - from;
            meter->open.covered_ms += (uint32_t)(to - from);
            if (to == boundary) seal(meter);
            from = to;
        }
    }
    seal_through(meter, at_ms);
    meter->last_at = at_ms;
    meter->valid = valid;
    if (!valid) {
        ++meter->invalid_count;
        return;
    }
    if (meter->valid_count && at_ms > meter->valid_at) {
        const uint64_t gap = at_ms - meter->valid_at;
        const uint32_t spacing = gap > UINT32_MAX ? UINT32_MAX : (uint32_t)gap;
        meter->last_spacing_ms = spacing;
        if (!meter->spacing_min_ms || spacing < meter->spacing_min_ms) meter->spacing_min_ms = spacing;
        if (spacing > meter->spacing_max_ms) meter->spacing_max_ms = spacing;
        if (spacing > TB_LITE_FUEL_GAP_MS) ++meter->gap_count;
    }
    ++meter->valid_count;
    meter->valid_at = at_ms;
    meter->rate_mlph = (uint32_t)(lph * 1000 + 0.5);
    tb_fuel_window_t *window = &meter->open;
    if (!window->readings || meter->rate_mlph < window->min_mlph) window->min_mlph = meter->rate_mlph;
    if (meter->rate_mlph > window->max_mlph) window->max_mlph = meter->rate_mlph;
    ++window->readings;
}

void tb_fuel_observe_counter(tb_fuel_meter_t *meter, uint64_t at_ms, uint32_t ticks, uint8_t mid, uint8_t flags)
{
    if (!at_ms || at_ms < meter->last_at || at_ms <= meter->last_counter.at_ms || ticks == UINT32_MAX || mid < 128) return;
    init_open(meter);
    meter->last_counter = (tb_fuel_anchor_t){.at_ms = at_ms, .ticks = ticks, .mid = mid, .flags = flags,
        .observed_ul = microlitres(meter->product), .covered_ms = meter->covered_ms};
    if (at_ms < end_ms(meter)) meter->open.counter = meter->last_counter;
}

void tb_fuel_tick(tb_fuel_meter_t *meter, uint64_t now_ms, uint64_t epoch_ms)
{
    init_open(meter);
    if (!meter->clock_offset_ms && epoch_ms >= TB_LITE_EPOCH_MS && epoch_ms > now_ms)
        meter->clock_offset_ms = epoch_ms - now_ms;
    if (now_ms >= TB_LITE_FUEL_GAP_MS) {
        const uint64_t cutoff = now_ms - TB_LITE_FUEL_GAP_MS;
        if (cutoff >= end_ms(meter) && meter->last_at < end_ms(meter)) meter->valid = false;
        seal_through(meter, cutoff);
    }
}

unsigned tb_fuel_page(const tb_fuel_meter_t *meter, uint32_t after, tb_fuel_window_t *out, unsigned capacity)
{
    if (!meter->open.slot || after >= meter->open.slot) return 0;
    uint32_t slot = after + 1;
    if (meter->open.slot - slot > TB_FUEL_HISTORY) slot = meter->open.slot - TB_FUEL_HISTORY;
    unsigned count = 0;
    for (; slot < meter->open.slot && count < capacity; ++slot) {
        const tb_fuel_window_t *window = &meter->ring[(slot - 1) % TB_FUEL_HISTORY];
        if (window->slot != slot) continue;
        out[count] = *window;
        const uint64_t end = (uint64_t)slot * TB_LITE_OPERATIONAL_MS;
        if (meter->clock_offset_ms && end + meter->clock_offset_ms >= TB_LITE_EPOCH_MS)
            out[count].end_utc_ms = end + meter->clock_offset_ms;
        ++count;
    }
    return count;
}

void tb_fuel_trip_mark(tb_fuel_meter_t *meter, uint64_t at_ms)
{
    meter->trip_reference = (tb_fuel_anchor_t){.at_ms = at_ms,
        .observed_ul = microlitres(meter->product), .covered_ms = meter->covered_ms};
}

bool tb_fuel_trip_total(const tb_fuel_meter_t *meter, tb_lite_fuel_counter_t *total)
{
    const tb_fuel_anchor_t *start = &meter->trip_reference;
    if (!start->at_ms || meter->last_at <= start->at_ms || meter->covered_ms <= start->covered_ms) return false;
    *total = (tb_lite_fuel_counter_t){.at_ms = start->at_ms, .covered_ms = meter->covered_ms - start->covered_ms,
        .microlitres = microlitres(meter->product) - start->observed_ul};
    return true;
}
