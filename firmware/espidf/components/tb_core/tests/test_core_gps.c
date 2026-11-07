/* Runner compiles the actual core.c GPS parsing/acceptance/attachment functions.
 * The only simulated boundaries are the monotonic clock, mutex and wall clock. */
#include "gps_host_stubs.h"
#include <assert.h>
#include <stdlib.h>
static uint64_t host_now_us;
static bool host_lock_available=true, host_locked;
static unsigned host_wall_clock_sets;
static struct timeval host_wall_clock;
#include "core_gps_under_test.h"

static bool xSemaphoreTake(SemaphoreHandle_t lock, uint32_t timeout)
{
    (void)timeout;
    if (!lock || !host_lock_available) return false;
    assert(!host_locked); host_locked=true; return true;
}
static void xSemaphoreGive(SemaphoreHandle_t lock)
{ assert(lock && host_locked); host_locked=false; }
static int64_t esp_timer_get_time(void) { return (int64_t)host_now_us; }
static int host_settimeofday(const struct timeval *value, const void *unused)
{ (void)unused; assert(!host_locked); host_wall_clock=*value; ++host_wall_clock_sets; return 0; }
static cJSON *sample(void)
{
    cJSON *object=cJSON_CreateObject();
    assert(object); cJSON_AddStringToObject(object,"sample_kind","RAW");
    return object;
}
static void assert_no_position(const cJSON *object)
{
    assert(!cJSON_GetObjectItemCaseSensitive(object,"lat"));
    assert(!cJSON_GetObjectItemCaseSensitive(object,"lon"));
    assert(!cJSON_GetObjectItemCaseSensitive(object,"altitude_m"));
    assert(!cJSON_GetObjectItemCaseSensitive(object,"gps_speed_kmh"));
}
static void accept_json(const char *json)
{
    cJSON *object=cJSON_Parse(json); assert(object);
    gps_fix_t fix; assert(gps_from_json(object,&fix,"link_gnss"));
    accept_gps(&fix); cJSON_Delete(object); assert(!host_locked);
}
static void test_no_gps_then_current_future_and_aged_fix(void)
{
    memset(&s,0,sizeof(s)); s.lock=(void *)1;
    host_now_us=5000000;
    cJSON *history=sample();
    assert(!tb_core_append_gps_at(history,host_now_us));
    assert_no_position(history);
    char *original=cJSON_PrintUnformatted(history); assert(original);
    host_now_us=10000000;
    accept_json("{\"valid\":true,\"lat\":-23.5,\"lon\":-46.6,\"age_ms\":6000}");
    assert(s.gps.accepted_us==10000000 && s.gps.received_us==4000000);
    cJSON *queued_before_report=sample();
    assert(!tb_core_append_gps_at(queued_before_report,6000000));
    assert_no_position(queued_before_report); cJSON_Delete(queued_before_report);
    cJSON *current=sample();
    assert(tb_core_append_gps_at(current,10000000));
    assert(cJSON_GetObjectItemCaseSensitive(current,"lat")->valuedouble==-23.5);
    assert(cJSON_GetObjectItemCaseSensitive(current,"lon")->valuedouble==-46.6);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(current,"has_fix")));
    assert(!cJSON_GetObjectItemCaseSensitive(current,"gps_speed_kmh"));
    cJSON_Delete(current);
    current=sample(); assert(tb_core_append_gps_at(current,19000000)); cJSON_Delete(current);
    current=sample(); assert(!tb_core_append_gps_at(current,19000001)); assert_no_position(current); cJSON_Delete(current);
    host_now_us=30000000;
    accept_json("{\"valid\":true,\"lat\":-23.6,\"lon\":-46.7,\"speed_kmh\":15,\"altitude_m\":700}");
    current=sample(); assert(!tb_core_append_gps_at(current,29000000)); assert_no_position(current); cJSON_Delete(current);
    current=sample(); cJSON_AddNumberToObject(current,"speed_kmh",50);
    assert(tb_core_append_gps_at(current,30000000));
    assert(cJSON_GetObjectItemCaseSensitive(current,"speed_kmh")->valuedouble==50);
    assert(cJSON_GetObjectItemCaseSensitive(current,"gps_speed_kmh")->valuedouble==15);
    assert(cJSON_GetObjectItemCaseSensitive(current,"altitude_m")->valuedouble==700);
    cJSON_Delete(current);
    /* Accepting new fixes cannot modify an earlier data object or record. */
    char *unchanged=cJSON_PrintUnformatted(history); assert(unchanged);
    assert(!strcmp(original,unchanged)); free(original); free(unchanged); cJSON_Delete(history);
}
static void test_unavailable_bad_fix_and_gps_restart(void)
{
    gps_fix_t fix;
    const char *bad[]={"{}","{\"valid\":false,\"lat\":0,\"lon\":0}",
        "{\"valid\":true,\"lat\":91,\"lon\":1}","{\"valid\":true,\"lat\":1}",
        "{\"valid\":true,\"lat\":1,\"lon\":181}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        cJSON *object=cJSON_Parse(bad[i]); assert(object);
        assert(!gps_from_json(object,&fix,"link_gnss")); cJSON_Delete(object);
    }
    s.gps.valid=false; /* Link's explicit loss-of-fix notification. */
    cJSON *object=sample(); assert(!tb_core_append_gps_at(object,31000000)); assert_no_position(object); cJSON_Delete(object);
    host_now_us=32000000;
    accept_json("{\"valid\":true,\"lat\":-24.5,\"lon\":-47.6}");
    object=sample(); assert(tb_core_append_gps_at(object,32000000)); cJSON_Delete(object);
    host_lock_available=false;
    object=sample(); assert(!tb_core_append_gps_at(object,32000000)); assert_no_position(object); cJSON_Delete(object);
    host_lock_available=true;
    assert(host_wall_clock_sets==0); /* No invented satellite/system time. */
}
static void test_real_time_and_age_preserved(void)
{
    host_now_us=40000000;
    accept_json("{\"valid\":true,\"lat\":-24.5,\"lon\":-47.6,\"age_ms\":1000,\"time_ms\":1794010000000}");
    assert(host_wall_clock_sets==1);
    assert((uint64_t)host_wall_clock.tv_sec*1000u+(uint64_t)host_wall_clock.tv_usec/1000u==UINT64_C(1794010001000));
    cJSON *object=sample(); assert(tb_core_append_gps_at(object,40000000));
    assert(cJSON_GetObjectItemCaseSensitive(object,"gps_time_ms")->valuedouble==1794010000000.0);
    cJSON_Delete(object);
}
int main(void)
{
    test_no_gps_then_current_future_and_aged_fix();
    test_unavailable_bad_fix_and_gps_restart();
    test_real_time_and_age_preserved();
    puts("Actual Core GPS functions passed: absent GPS, no invented coordinates/time, receipt-vs-aged fix guard, no retroactive queued/history fixes, expiry/restart, source speed preservation");
    return 0;
}
