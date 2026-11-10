#include "trip_checkpoint.h"
#include <assert.h>
#include <stdio.h>
static void near(double a,double b){assert(isfinite(a) && fabs(a-b)<1e-8);}
static void distance(void)
{
    tbx_trip_t t;tbx_trip_init(&t,100,true);
    assert(tbx_trip_odometer(&t,128,10000,1000)==TBX_TRIP_BASELINE);
    tbx_trip_speed(&t,TBX_TRIP_J1587,128,60,40000);
    tbx_trip_speed(&t,TBX_TRIP_J1587,128,60,41000);
    assert(t.total_km>100);
    assert(tbx_trip_odometer(&t,128,10002,70000)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,100.3218688); /* Replace preceding speed fallback, no overlap. */
    tbx_fuel_account_t a;tbx_fuel_account_init(&a,10,true);
    tb_trip_checkpoint_t b;tb_trip_checkpoint_make(&b,7,&t,&a);assert(tb_trip_checkpoint_valid(&b));
    tb_trip_checkpoint_restore(&b,&t,&a);
    assert(tbx_trip_odometer(&t,128,10010,1000)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,101.609344);assert(t.gaps==1);
    tbx_trip_odometer(&t,128,10010,2000);near(t.total_km,101.609344);
    assert(tbx_trip_odometer(&t,130,20000,3000)==TBX_TRIP_SOURCE_IGNORED);
    assert(tbx_trip_odometer(&t,128,1,4000)==TBX_TRIP_RESET_REBASELINE);near(t.total_km,101.609344);
    assert(tbx_trip_odometer(&t,128,1000000,5000)==TBX_TRIP_RANGE_REBASELINE);near(t.total_km,101.609344);
    tbx_trip_reset(&t);assert(!t.odo_valid && !t.has_distance);
}
static void fuel(void)
{
    tbx_fuel_account_t a;tbx_fuel_account_init(&a,0,false);
    tbx_fuel_account_rate(&a,128,36,36,1000);tbx_fuel_account_rate(&a,128,36,36,2000);
    near(a.integrated_l,.01);near(a.observed_l,.01);near(a.estimated_l,0);
    tbx_fuel_account_rate(&a,128,36,36,7000);near(a.observed_l,.01);near(a.estimated_l,.05);
    tbx_fuel_account_rate(&a,128,36,36,8000);near(a.observed_l,.02);
    assert(!tbx_fuel_account_counter(&a,128,1000,8000));
    tbx_fuel_account_rate(&a,128,36,36,9000);near(a.observed_l,.03);
    assert(tbx_fuel_account_counter(&a,128,1002,10000));near(a.observed_l,.02+2*.473176473);
    double once=a.observed_l;assert(tbx_fuel_account_counter(&a,128,1002,11000));near(a.observed_l,once);
    assert(!tbx_fuel_account_counter(&a,130,5000,12000));near(a.observed_l,once);
    assert(!tbx_fuel_account_counter(&a,128,1,13000));assert(a.resets==1);near(a.observed_l,once);
    tbx_fuel_account_counter(&a,128,100000,14000);assert(a.range_errors==1);near(a.observed_l,once);
    tbx_trip_t t;tbx_trip_init(&t,100,true);a.distance_valid=true;a.distance_km=100;
    a.last_l_per_km=.4;
    tb_trip_checkpoint_t b;tb_trip_checkpoint_make(&b,1,&t,&a);
    tb_trip_checkpoint_restore(&b,&t,&a);assert(a.unknown_gap && a.reboot_gap_pending);
    tbx_fuel_account_distance(&a,102);near(a.estimated_l,b.estimated_l+.8);near(a.observed_l,once);
    double estimate=a.estimated_l;tbx_fuel_account_distance(&a,102);near(a.estimated_l,estimate);
    tbx_fuel_account_distance(&a,103);near(a.estimated_l,estimate); /* No repeated estimation after restored gap. */
    assert(tbx_fuel_account_counter(&a,128,100003,2000));near(a.observed_l,b.direct_anchor_l+3*.473176473);
    near(a.estimated_l,b.estimated_at_direct); /* Valid ECU delta covers same estimated interval. */
    tb_trip_checkpoint_t newer=b;newer.generation=2;newer.crc=0;
    assert(!tb_trip_checkpoint_valid(&newer) && tb_trip_checkpoint_valid(&b));
    for(size_t i=0;i<sizeof(b);i++){tb_trip_checkpoint_t c=b;((unsigned char *)&c)[i]^=1;if(i<offsetof(tb_trip_checkpoint_t,crc)+sizeof(b.crc))assert(!tb_trip_checkpoint_valid(&c));}
    tbx_fuel_account_init(&a,10,true);a.last_l_per_km=0;a.distance_valid=true;a.distance_km=100;
    tbx_fuel_account_distance(&a,102);assert(a.unknown_gap);near(a.estimated_l,0);near(a.observed_l,10);
}
int main(void){distance();fuel();puts("Accounting: validated odometer recovery, speed overlap replacement, isolated estimates, PID250 reconciliation/no duplication, unknown gap and CRC fallback PASS");return 0;}
