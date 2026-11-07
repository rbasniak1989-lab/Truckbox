#ifndef TBX_TRIP_H
#define TBX_TRIP_H
#include <stdbool.h>
#include <stdint.h>
/* Pure distance accounting: GNSS, wall clock, storage and network are optional.
 * Caller serializes access and persists total_km/has_distance. Restored vehicle
 * counter checkpoints never authorize an unbounded delta across unknown downtime. */
#define TBX_TRIP_COUNTER_FRESH_MS UINT64_C(30000)
#define TBX_TRIP_SPEED_WINDOW_MS UINT64_C(3000)
#define TBX_TRIP_CAN_PREFERENCE_MS UINT64_C(5000)
typedef enum { TBX_TRIP_J1939 = 1, TBX_TRIP_J1587 = 2 } tbx_trip_network_t;
typedef enum {
    TBX_TRIP_INVALID, TBX_TRIP_BASELINE, TBX_TRIP_COUNTER_ADDED,
    TBX_TRIP_ESTIMATE_ADDED, TBX_TRIP_COUNTER_FRESH, TBX_TRIP_SOURCE_IGNORED,
    TBX_TRIP_GAP_REBASELINE, TBX_TRIP_RESET_REBASELINE, TBX_TRIP_RANGE_REBASELINE,
    TBX_TRIP_SOURCE_REBASELINE, TBX_TRIP_BOOT_REBASELINE
} tbx_trip_result_t;
typedef struct {
    double total_km;
    bool has_distance;
    /* True after an estimated increment until a counter baseline is established. */
    bool fallback_since_counter;
    bool counter_valid, restored_counter;
    uint8_t counter_sa;
    uint32_t counter_raw;
    uint64_t counter_at_ms;
    bool speed_valid, can_speed_valid;
    tbx_trip_network_t speed_network;
    uint8_t speed_address;
    double speed_kmh;
    uint64_t speed_at_ms, can_speed_at_ms;
    uint32_t gaps, resets, implausible, source_changes;
    const char *quality;
} tbx_trip_t;
void tbx_trip_init(tbx_trip_t *trip, double restored_km, bool restored_valid);
void tbx_trip_restore_counter(tbx_trip_t *trip, uint8_t source_address, uint32_t raw);
void tbx_trip_reset(tbx_trip_t *trip);
tbx_trip_result_t tbx_trip_counter(tbx_trip_t *trip, uint8_t source_address,
                                   uint32_t raw, uint64_t now_ms);
tbx_trip_result_t tbx_trip_speed(tbx_trip_t *trip, tbx_trip_network_t network,
                                 uint8_t source_address, double kmh, uint64_t now_ms);
const char *tbx_trip_result_name(tbx_trip_result_t result);
#endif
