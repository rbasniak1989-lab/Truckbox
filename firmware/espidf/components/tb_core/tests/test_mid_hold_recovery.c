#include "../mid_hold_recovery.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    tb_mid_hold_recovery_t p = {
        .authorized=true,.stationary_confirmed=true,.vehicle_parked=true,
        .hold_valid=true,.hold_persisted=true,.rx_fresh=true,.rx_healthy=true,
        .configured_mid=172,.held_mid=172,.held_boot=918,.current_boot=920,
        .valid_frames=3600,.uptime_us=UINT64_C(90000000)
    };
    assert(tb_mid_hold_can_recover(&p));

#define DENY(field,value) do { tb_mid_hold_recovery_t x=p; x.field=value; assert(!tb_mid_hold_can_recover(&x)); } while(0)
    DENY(authorized,false);
    DENY(stationary_confirmed,false);
    DENY(vehicle_parked,false);
    DENY(ota_busy,true);
    DENY(hold_valid,false);
    DENY(hold_persisted,false);
    DENY(hold_invalid,true);
    DENY(hold_dirty,true);
    DENY(tx_active,true);
    DENY(polling_armed,true);
    DENY(tx_seen_this_boot,true);
    DENY(mid_observed_this_boot,true);
    DENY(rx_fresh,false);
    DENY(rx_healthy,false);
    DENY(configured_mid,0);
    DENY(configured_mid,128);
    DENY(held_mid,128);
    DENY(held_boot,920);
    DENY(held_boot,921);
    DENY(held_boot,0);
    DENY(valid_frames,2999);
    DENY(uptime_us,UINT64_C(59999999));
#undef DENY
    puts("Stale MID hold recovery: old evidence, idle/parked, clean/passive RX, no current MID or TX PASS");
    return 0;
}
