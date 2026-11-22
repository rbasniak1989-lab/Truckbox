#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Permit clearing only an OLD, checksum-valid local MID hold. A current-boot
 * matching MID, recent TX, weak/passive RX or moving vehicle always wins.
 * This helper decides eligibility; it never writes NVS or enables TX. */
typedef struct {
    bool authorized, stationary_confirmed, vehicle_parked, ota_busy;
    bool hold_valid, hold_persisted, hold_invalid, hold_dirty;
    bool tx_active, polling_armed, tx_seen_this_boot, mid_observed_this_boot;
    bool rx_fresh, rx_healthy, opaque_boot_id;
    uint8_t configured_mid, held_mid;
    uint32_t held_boot, current_boot, valid_frames;
    uint64_t uptime_us;
} tb_mid_hold_recovery_t;

static inline bool tb_mid_hold_can_recover(const tb_mid_hold_recovery_t *p)
{
    return p && p->authorized && p->stationary_confirmed && p->vehicle_parked &&
           !p->ota_busy && p->hold_valid && p->hold_persisted &&
           !p->hold_invalid && !p->hold_dirty &&
           !p->tx_active && !p->polling_armed && !p->tx_seen_this_boot &&
           !p->mid_observed_this_boot && p->rx_fresh && p->rx_healthy &&
           p->configured_mid >= 128 && p->configured_mid == p->held_mid &&
           p->held_boot > 0 && p->current_boot > 0 &&
           (p->opaque_boot_id ? p->held_boot != p->current_boot : p->held_boot < p->current_boot) &&
           p->valid_frames >= 3000 && p->uptime_us >= UINT64_C(60000000);
}
