#include "tbx_trip.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void near(double a,double b) {assert(fabs(a-b)<1e-10);}
int main(void)
{
    tbx_trip_t t;
    tbx_trip_init(&t,0,false);
    assert(!t.has_distance);
    assert(tbx_trip_counter(&t,0,100000,0)==TBX_TRIP_BASELINE);
    assert(!t.has_distance);
    assert(tbx_trip_counter(&t,0,100002,1000)==TBX_TRIP_COUNTER_ADDED);
    assert(t.has_distance);near(t.total_km,0.010);
    assert(tbx_trip_counter(&t,0,100002,2000)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,0.010);
    /* Alternate ECUs cannot blend counter domains or steal a fresh primary. */
    assert(tbx_trip_counter(&t,1,2000000,2500)==TBX_TRIP_SOURCE_IGNORED);
    assert(t.counter_sa==0);near(t.total_km,0.010);
    assert(tbx_trip_counter(&t,0,100004,3000)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,0.020);
    /* Reset is a baseline, never a negative trip or assumed 32bit rollover. */
    assert(tbx_trip_counter(&t,0,1,4000)==TBX_TRIP_RESET_REBASELINE);
    near(t.total_km,0.020);assert(t.resets==1);
    assert(tbx_trip_counter(&t,0,3,5000)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,0.030);
    assert(tbx_trip_counter(&t,0,100000,6000)==TBX_TRIP_RANGE_REBASELINE);
    assert(t.implausible==1);near(t.total_km,0.030);
    assert(tbx_trip_counter(&t,0,100001,7000)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,0.035);
    assert(tbx_trip_counter(&t,0,UINT32_MAX,8000)==TBX_TRIP_INVALID);
    assert(t.counter_raw==100001);
    assert(tbx_trip_counter(&t,1,2000000,40000)==TBX_TRIP_SOURCE_REBASELINE);
    assert(t.counter_sa==1 && t.source_changes==1);near(t.total_km,0.035);
    /* Restoring after offline reboot recovers confirmed total but cannot trust
     * the elapsed time to a persisted vehicle counter. No huge delta is added. */
    tbx_trip_init(&t,1234.5,true);
    tbx_trip_restore_counter(&t,0,100000);
    assert(!t.counter_valid && t.restored_counter);
    assert(tbx_trip_counter(&t,0,2000000,100)==TBX_TRIP_BOOT_REBASELINE);
    near(t.total_km,1234.5);assert(t.gaps==1);
    assert(tbx_trip_counter(&t,0,2000001,1100)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,1234.505);
    /* Without highres, two same-source speed samples give a labelled estimate. */
    tbx_trip_init(&t,0,false);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,36,0)==TBX_TRIP_BASELINE);
    assert(!t.has_distance);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,36,1000)==TBX_TRIP_ESTIMATE_ADDED);
    near(t.total_km,0.010);assert(t.has_distance && t.fallback_since_counter);
    assert(tbx_trip_counter(&t,0,100000,1100)==TBX_TRIP_BASELINE);
    near(t.total_km,0.010);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,36,2000)==TBX_TRIP_COUNTER_FRESH);
    near(t.total_km,0.010);
    assert(tbx_trip_counter(&t,0,100002,2100)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,0.020);
    /* Long counter silence switches to bounded speed integration. On return,
     * the total counter span overlaps fallback and therefore only rebaselines. */
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,36,31000)==TBX_TRIP_COUNTER_FRESH);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,36,32000)==TBX_TRIP_COUNTER_FRESH);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,36,33000)==TBX_TRIP_ESTIMATE_ADDED);
    near(t.total_km,0.030);
    assert(tbx_trip_counter(&t,0,100062,33100)==TBX_TRIP_GAP_REBASELINE);
    near(t.total_km,0.030);
    assert(tbx_trip_counter(&t,0,100064,34100)==TBX_TRIP_COUNTER_ADDED);
    near(t.total_km,0.040);
    /* Never bridge J1939/J1587 or different MID speed endpoints. */
    tbx_trip_init(&t,0,false);
    tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,0);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,129,200,1000)==TBX_TRIP_SOURCE_IGNORED);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,1000)==TBX_TRIP_ESTIMATE_ADDED);
    near(t.total_km,0.010);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,72,1500)==TBX_TRIP_SOURCE_REBASELINE);
    near(t.total_km,0.010);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,2000)==TBX_TRIP_SOURCE_IGNORED);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1939,0,72,2500)==TBX_TRIP_ESTIMATE_ADDED);
    near(t.total_km,0.030);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,8000)==TBX_TRIP_SOURCE_REBASELINE);
    near(t.total_km,0.030);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,9000)==TBX_TRIP_ESTIMATE_ADDED);
    near(t.total_km,0.040);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,10000)==TBX_TRIP_ESTIMATE_ADDED);
    near(t.total_km,0.050);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,20000)==TBX_TRIP_GAP_REBASELINE);
    near(t.total_km,0.050);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,NAN,21000)==TBX_TRIP_INVALID);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,-1,21000)==TBX_TRIP_INVALID);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,251,21000)==TBX_TRIP_INVALID);
    assert(tbx_trip_speed(&t,TBX_TRIP_J1587,128,36,100)==TBX_TRIP_GAP_REBASELINE);
    near(t.total_km,0.050);
    tbx_trip_reset(&t);assert(!t.has_distance && !t.counter_valid && !t.speed_valid);
    near(t.total_km,0);
    tbx_trip_init(&t,NAN,true);assert(!t.has_distance);
    puts("Trip: checkpoint/reboot, source priority, counter reset/jump/gap and fallback overlap PASS");
}
