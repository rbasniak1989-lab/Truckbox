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
static tbx_fuel_stamp_t stamp(uint32_t boot,uint64_t ms,uint64_t unix_ms)
{return (tbx_fuel_stamp_t){.boot=boot,.uptime_ms=ms,.unix_ms=unix_ms};}
static void constant_rate(tbx_fuel_account_t *a,double lph,uint64_t start,unsigned seconds)
{
    for(unsigned i=0;i<=seconds;i++)tbx_fuel_account_rate_at(a,128,lph,40,stamp(1,start+1000*i,0));
}
static void fuel_reconciliation_examples(void)
{
    tbx_fuel_account_t a;tbx_fuel_account_init(&a,0,false);
    assert(!tbx_fuel_account_counter_litres(&a,128,1000,stamp(1,1000,1794600001000),0));
    constant_rate(&a,108,1000,300);near(a.observed_l,9);near(tbx_fuel_pending(&a),9);
    assert(tbx_fuel_account_counter_litres(&a,128,1010,stamp(1,301000,1794600301000),10));
    near(a.observed_l,10);near(a.last_correction_l,1);near(a.direct_l,10);near(tbx_fuel_pending(&a),0);
    constant_rate(&a,60,301000,300);near(a.observed_l,15);
    assert(tbx_fuel_account_counter_litres(&a,128,1014,stamp(1,601000,1794600601000),20));
    near(a.last_correction_l,-1);near(a.observed_l,14);near(a.direct_l,14);near(tbx_fuel_pending(&a),0);
    assert(!tbx_fuel_account_counter_litres(&a,128,1014,stamp(1,601000,1794600601000),20));near(a.observed_l,14);
    assert(tbx_fuel_account_counter_litres(&a,128,1014,stamp(1,901000,1794600901000),20));near(a.observed_l,14); /* unchanged is valid */
    tbx_fuel_account_init(&a,0,false);
    tbx_fuel_account_counter_litres(&a,128,30400,stamp(1,1000,0),0);constant_rate(&a,144,1000,300);
    assert(tbx_fuel_account_counter_litres(&a,128,30413,stamp(1,301000,0),10));near(a.observed_l,13);near(a.last_correction_l,1);
    tbx_fuel_account_init(&a,0,false);constant_rate(&a,36,1000,100);near(a.observed_l,1);
    tbx_fuel_account_counter_litres(&a,128,1000,stamp(1,101000,0),0);near(tbx_fuel_unanchored(&a),1);
    assert(tbx_fuel_account_counter_litres(&a,128,1010,stamp(1,401000,0),10));
    near(a.observed_l,11);near(a.direct_l,10);near(tbx_fuel_unanchored(&a),1); /* pre-anchor1 remains provisional */
    puts("Required SI examples: 9->10 (+1), 5->4 (-1) =14 confirmed; 12->13 =13, never25 PASS");
}
static void wire_counter_validation(void)
{
    tbx_fuel_account_t a;tbx_fuel_account_init(&a,0,false);
    assert(!tbx_fuel_account_counter_at(&a,128,1000,stamp(1,1000,0),0));near(a.counter_l,473.176473);
    constant_rate(&a,36,1000,60);near(a.observed_l,.6);
    assert(tbx_fuel_account_counter_at(&a,128,1002,stamp(1,61000,0),1));near(a.observed_l,2*.473176473);
    double canonical=a.observed_l;tbx_fuel_stamp_t anchor=a.anchor;
    assert(!tbx_fuel_account_counter_at(&a,130,1003,stamp(1,62000,0),1));assert(a.result==TBX_FUEL_WRONG_MID);
    assert(!tbx_fuel_account_counter_at(&a,128,1,stamp(1,63000,0),1));assert(a.result==TBX_FUEL_COUNTER_DECREASE);
    assert(!tbx_fuel_account_counter_at(&a,128,100000,stamp(1,64000,0),1));assert(a.result==TBX_FUEL_COUNTER_JUMP);
    assert(!tbx_fuel_account_counter_at(&a,128,1003,stamp(1,60000,0),1));assert(a.result==TBX_FUEL_TIME_INVALID);
    assert(!tbx_fuel_account_counter_at(&a,128,UINT32_MAX,stamp(1,65000,0),1));
    assert(a.direct_raw==1002 && a.anchor.uptime_ms==anchor.uptime_ms);near(a.observed_l,canonical);
    assert(tbx_fuel_account_counter_at(&a,128,1003,stamp(1,121000,0),2));near(a.observed_l,3*.473176473);
    /* Negative/out-of-order or different-MID rates cannot alter the integral. */
    uint64_t before=a.rate_at_ms;tbx_fuel_account_rate_at(&a,128,999,40,stamp(1,100,0));assert(a.rate_at_ms==before);
}
static void restart_and_crc(void)
{
    tbx_fuel_account_t a;tbx_fuel_account_init(&a,0,false);tbx_trip_t t;tbx_trip_init(&t,100,true);
    tbx_fuel_account_counter_at(&a,128,1000,stamp(1,1000,1794600001000),100);
    constant_rate(&a,36,1000,60);near(a.observed_l,.6);
    tb_trip_checkpoint_t b;tb_trip_checkpoint_make(&b,2,&t,&a);
    tb_trip_checkpoint_v4_t v;tb_trip_checkpoint_v4_make(&v,&b,&a);assert(tb_trip_checkpoint_v4_valid(&v));
    assert(tbx_fuel_account_restore(&a,&v.fuel));assert(a.unknown_gap && !a.rate_valid);
    assert(!tbx_fuel_account_counter_at(&a,128,1002,stamp(2,1000,0),101));assert(a.result==TBX_FUEL_TIME_INVALID && a.direct_raw==1000);
    assert(tbx_fuel_account_counter_at(&a,128,1002,stamp(2,1000,1794600121000),101));near(a.observed_l,2*.473176473);
    assert(!a.interval_unknown);double once=a.observed_l;
    tb_trip_checkpoint_make(&b,3,&t,&a);tb_trip_checkpoint_v4_make(&v,&b,&a);assert(tbx_fuel_account_restore(&a,&v.fuel));
    assert(tbx_fuel_account_counter_at(&a,128,1002,stamp(3,1000,1794600181000),101));near(a.observed_l,once);
    for(size_t i=0;i<offsetof(tb_trip_checkpoint_v4_t,crc)+sizeof(v.crc);i++){
        tb_trip_checkpoint_v4_t corrupt=v;((uint8_t *)&corrupt)[i]^=1;assert(!tb_trip_checkpoint_v4_valid(&corrupt));
    }
    tb_trip_checkpoint_restore(&b,&t,&a);near(a.observed_l,once);assert(!a.direct_valid); /* v3 has no time reference */
    puts("A/B scalar checkpoint: restart inside pending interval, no duplicate, UTC gap recovery, missing-clock hold, CRC corruption and v3 import PASS");
}
static void gaps_and_operation_boundaries(void)
{
    tbx_fuel_account_t a;tbx_fuel_account_init(&a,0,false);
    tbx_fuel_account_rate(&a,128,36,36,1000);tbx_fuel_account_rate(&a,128,36,36,2000);near(a.observed_l,.01);
    tbx_fuel_account_rate(&a,128,36,36,7000);near(a.observed_l,.06);near(a.estimated_l,.05);
    tbx_fuel_account_rate(&a,128,36,36,400000);assert(a.unknown_gap);
    tbx_fuel_account_rate(&a,128,36,36,405000);assert(a.unknown_gap); /* short later estimate never hides previous unknown */
    tbx_fuel_account_init(&a,0,false);tbx_fuel_account_counter_litres(&a,128,1000,stamp(1,1000,0),100);
    constant_rate(&a,72,1000,100);near(a.observed_l,2);
    assert(tbx_fuel_account_boundary(&a,true,"trip-A",stamp(1,101000,0),102));
    constant_rate(&a,72,101000,100);near(a.observed_l,4);
    assert(tbx_fuel_account_boundary(&a,false,"trip-A",stamp(1,201000,0),104));
    constant_rate(&a,72,201000,100);near(a.observed_l,6);
    assert(tbx_fuel_account_counter_litres(&a,128,1009,stamp(1,301000,0),106));
    near(a.observed_l,9);near(tbx_fuel_operation_litres(&a),3);
    assert(a.interval_crosses_operation && a.operation_estimated); /* 3L allocation estimated; ECU only confirms entire9 */
    assert(!tbx_fuel_account_boundary(&a,true,"trip-A",stamp(1,302000,0),106));
    constant_rate(&a,72,301000,10);near(tbx_fuel_operation_litres(&a),3); /* Empty-mode fuel never added to closed trip. */
    assert(!tbx_fuel_account_boundary(&a,false,"another-trip",stamp(1,311000,0),106));
    assert(tbx_fuel_account_boundary(&a,true,"trip-B",stamp(1,311000,0),106));near(tbx_fuel_operation_litres(&a),0);
    puts("Offline integration, unknown vs bounded estimated gaps, trip/empty/trip boundaries and explicit estimated allocation PASS");
}
int main(void){distance();fuel_reconciliation_examples();wire_counter_validation();restart_and_crc();gaps_and_operation_boundaries();
    printf("Fuel state %zuB; v4 checkpoint %zuB; all accounting tests PASS\n",sizeof(tbx_fuel_account_t),sizeof(tb_trip_checkpoint_v4_t));return 0;}
