#ifndef TBX_FUEL_ACCOUNT_H
#define TBX_FUEL_ACCOUNT_H
#include <stdint.h>
#include <stdbool.h>
/* No wall clock or GPS required. Estimates are always separate from observed
 * integrated/direct totals. Persist scalar checkpoint, never a live pointer. */
typedef struct {
    double observed_l, integrated_l, estimated_l, direct_l;
    bool valid, rate_valid, direct_valid, restored, unknown_gap;
    uint8_t rate_mid,direct_mid;
    double last_rate_lph,last_speed_kmh,last_l_per_km;
    uint64_t rate_at_ms,direct_at_ms;
    uint32_t direct_raw,gaps,resets,range_errors;
    double direct_anchor_l, estimated_at_direct;
    bool distance_valid, reboot_gap_pending;
    double distance_km;
    const char *quality;
} tbx_fuel_account_t;
void tbx_fuel_account_init(tbx_fuel_account_t *,double restored_l,bool valid);
void tbx_fuel_account_rate(tbx_fuel_account_t *,uint8_t mid,double lph,double speed_kmh,uint64_t now_ms);
void tbx_fuel_account_distance(tbx_fuel_account_t *,double validated_trip_km);
bool tbx_fuel_account_counter(tbx_fuel_account_t *,uint8_t mid,uint32_t raw,uint64_t now_ms);
#endif
