/* The runner extracts these exact production functions into the included header;
 * no duplicated signal formulas or reimplemented migration path is tested. */
#include "capture_host_stubs.h"
#include <assert.h>
#include <stdlib.h>
static int64_t host_now_us;
static uint8_t host_nvs[256];
static size_t host_nvs_size;
static unsigned host_commits, host_mutexes;
static unsigned host_held;
#include "capture_under_test.h"

static SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ assert(host_mutexes < 8); return (void *)(uintptr_t)(1u << host_mutexes++); }
static bool xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout)
{
    (void)timeout;
    unsigned bit = (unsigned)(uintptr_t)mutex;
    assert(bit && !(host_held & bit));
    host_held |= bit;
    return true;
}
static void xSemaphoreGive(SemaphoreHandle_t mutex)
{
    unsigned bit = (unsigned)(uintptr_t)mutex;
    assert(host_held & bit);
    host_held &= ~bit;
}
static int64_t esp_timer_get_time(void) { return host_now_us; }
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ (void)mode; assert(!strcmp(name, "tb_trip")); *handle = 1; return ESP_OK; }
static esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *buffer, size_t *size)
{
    (void)handle; assert(!strcmp(key, "trip"));
    if (!host_nvs_size) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < host_nvs_size) { *size = host_nvs_size; return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = host_nvs_size;
    memcpy(buffer, host_nvs, *size);
    return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *buffer, size_t size)
{
    (void)handle; assert(!strcmp(key, "trip") && size <= sizeof(host_nvs));
    /* NVS/flash work must not hold the acquisition mutex. */
    assert(!(host_held & (unsigned)(uintptr_t)s.lock));
    memcpy(host_nvs, buffer, size); host_nvs_size = size;
    return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    assert(!(host_held & (unsigned)(uintptr_t)s.lock));
    ++host_commits;
    return ESP_OK;
}

static void boot_with_checkpoint(const void *blob, size_t size)
{
    assert(!host_held);
    memset(&s, 0, sizeof(s));
    host_mutexes = host_commits = 0;
    host_now_us = 0;
    host_nvs_size = size;
    if (size) memcpy(host_nvs, blob, size);
    assert(host_start_restore() == ESP_OK);
    assert(!host_held);
}
static void close_to(double value, double expected)
{ assert(isfinite(value) && fabs(value - expected) < 0.00000001); }
static void can_u32(uint32_t pgn, uint8_t sa, unsigned offset, uint32_t raw, uint64_t now)
{
    assert(offset + 4 <= 8);
    twai_message_t f = {.extd = true, .identifier = UINT32_C(0x18000000) | (pgn << 8) | sa, .data_length_code = 8};
    for (unsigned i = 0; i < 4; ++i) f.data[offset + i] = (uint8_t)(raw >> (i * 8));
    host_now_us = (int64_t)now * 1000;
    update_signals(&f);
    assert(!host_held);
}
static void j1587_numeric(uint16_t pid, uint8_t mid, double number, uint64_t now)
{
    tbx_j1587_observation_t value = {.pid = pid, .mid = mid,
        .quality = TBX_J1587_OBS_EM_VALIDACAO, .has_numeric = true,
        .value = number, .uptime_ms = now};
    host_now_us = (int64_t)now * 1000;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    update_j1587_signal_locked(&value, NULL);
    xSemaphoreGive(s.lock);
}

static void test_v1_migration_and_v2_reboot(void)
{
    trip_blob_v1_t v1 = {.version = 1, .distance = 123.5, .fuel = 45.25,
        .distance_valid = true, .fuel_valid = true};
    boot_with_checkpoint(&v1, sizeof(v1));
    close_to(s.distance_trip, 123.5); close_to(s.fuel_trip, 45.25);
    assert(s.distance_trip_valid && s.fuel_trip_valid && !s.trip.counter_valid);
    persist_trip();
    assert(host_commits == 1 && host_nvs_size == sizeof(trip_blob_t));
    trip_blob_t migrated;
    memcpy(&migrated, host_nvs, sizeof(migrated));
    assert(migrated.version == 2);
    close_to(migrated.distance, 123.5); close_to(migrated.fuel, 45.25);
    migrated.counter_valid = true; migrated.counter_sa = 0; migrated.counter_raw = 100000;
    boot_with_checkpoint(&migrated, sizeof(migrated));
    /* Unknown powered-off interval is rebased, never a fabricated giant delta. */
    can_u32(65217, 0, 0, 200000, 1000);
    close_to(s.distance_trip, 123.5);
    can_u32(65217, 0, 0, 200002, 2000);
    close_to(s.distance_trip, 123.51);
}
static void test_counter_gap_and_reset_no_double_count(void)
{
    boot_with_checkpoint(NULL, 0);
    can_u32(65217, 0, 0, 100000, 1000);
    can_u32(65217, 0, 0, 100002, 2000);
    close_to(s.distance_trip, .01);
    can_u32(65265, 0, 1, 36u * 256u, 35000);
    can_u32(65265, 0, 1, 36u * 256u, 36000);
    close_to(s.distance_trip, .02);
    can_u32(65217, 0, 0, 100010, 37000);
    close_to(s.distance_trip, .02); /* counter return establishes a new baseline */
    can_u32(65217, 0, 0, 100012, 38000);
    close_to(s.distance_trip, .03);
    can_u32(65217, 0, 0, 10, 39000);
    close_to(s.distance_trip, .03);
    can_u32(65217, 0, 0, 11, 40000);
    close_to(s.distance_trip, .035);
    tb_capture_reset_trip();
    close_to(s.distance_trip, 0); close_to(s.fuel_trip, 0);
    assert(!s.trip.counter_valid && s.speed_at == 0 && s.fuel_at == 0 && host_commits == 1);
    can_u32(65217, 0, 0, 123456, 41000);
    close_to(s.distance_trip, 0);
}
static void test_j1939_preferred_then_passive_fallback(void)
{
    boot_with_checkpoint(NULL, 0);
    can_u32(65265, 0, 1, 36u * 256u, 1000);
    j1587_numeric(84, 128, 72, 1500);
    close_to(s.speed, 36); assert(!s.speed_j1587);
    j1587_numeric(84, 128, 72, 6001);
    close_to(s.speed, 72); assert(s.speed_j1587);
    can_u32(61444, 0, 3, 8000, 7000);
    j1587_numeric(190, 128, 2000, 7500);
    close_to(s.rpm, 1000); assert(!s.rpm_j1587);
    j1587_numeric(190, 128, 2000, 12001);
    close_to(s.rpm, 2000); assert(s.rpm_j1587);
    /* All-FF unavailable CAN bytes cannot replace the last valid RPM. */
    can_u32(61444, 0, 3, UINT32_MAX, 13000);
    close_to(s.rpm, 2000);
    tbx_j1587_observation_t unavailable = {.pid = 84, .mid = 128,
        .quality = TBX_J1587_OBS_INDISPONIVEL, .has_numeric = false, .value = 0, .uptime_ms = 13000};
    xSemaphoreTake(s.lock, portMAX_DELAY);
    update_j1587_signal_locked(&unavailable, NULL);
    xSemaphoreGive(s.lock);
    close_to(s.speed, 72);
}
static void can_eec1(uint8_t sa, uint16_t rpm_raw, uint8_t torque_raw, uint64_t now)
{
    twai_message_t frame = {.extd=true,
        .identifier=UINT32_C(0x18f00400)|sa, .data_length_code=8};
    frame.data[2]=torque_raw; frame.data[3]=(uint8_t)rpm_raw;
    frame.data[4]=(uint8_t)(rpm_raw>>8);
    host_now_us=(int64_t)now*1000;
    update_signals(&frame);
    assert(!host_held);
}
static void test_canonical_derived_fuel_and_fallback_boundaries(void)
{
    boot_with_checkpoint(NULL,0);
    /* Independent Document Master formula, matching the established Android C0.
     * This is an estimated rate, never presented as a measured flowmeter. */
    const double c0_rate=2643.0*.5*1000.0/9550.0*.25*.9720031611625598;
    can_eec1(0,8000,175,1000);
    assert(s.fuel_derived && !s.fuel_j1587 && s.fuel_trip_valid);
    close_to(s.fuel_rate,c0_rate); close_to(s.fuel_trip,0);
    can_eec1(0,8000,225,1500);
    double expected=2*c0_rate*.5/3600.0; /* current rectangle, not trapezoid */
    close_to(s.fuel_trip,expected);
    can_eec1(0,8000,175,2500); expected+=c0_rate/3600.0;
    close_to(s.fuel_trip,expected); /* exactly 1s is permitted */
    can_eec1(0,8000,175,3501); close_to(s.fuel_trip,expected); /* 1001ms skipped */
    can_eec1(0,UINT16_MAX,UINT8_MAX,3600); close_to(s.fuel_trip,expected);
    assert(!s.fuel_model.baseline);
    can_eec1(0,8000,175,4000); close_to(s.fuel_trip,expected); /* NA breaks baseline */
    can_eec1(0,8000,175,5000); expected+=c0_rate/3600.0;
    close_to(s.fuel_trip,expected);
    can_eec1(1,8000,175,5100); close_to(s.fuel_trip,expected); /* new ECU */
    can_eec1(1,8000,175,5500); expected+=c0_rate*.4/3600.0;
    close_to(s.fuel_trip,expected);
    can_u32(65266,1,0,2000,10500); /* model exactly 5s old: suppress fallback */
    assert(s.fuel_derived); close_to(s.fuel_rate,c0_rate); close_to(s.fuel_trip,expected);
    can_u32(65266,1,0,2000,10501); /* stale model -> measured legacy rate */
    assert(!s.fuel_derived && !s.fuel_j1587); close_to(s.fuel_rate,100); close_to(s.fuel_trip,expected);
    can_u32(65266,1,0,4000,10601); expected+=150.0*.1/3600.0;
    close_to(s.fuel_trip,expected);
    can_eec1(1,8000,175,10700); /* switching back cannot blend sources */
    assert(s.fuel_derived); close_to(s.fuel_rate,c0_rate); close_to(s.fuel_trip,expected);
    uint8_t raw183[]={0x34,0x12};
    tbx_j1587_observation_t comparison={.pid=183,.mid=128,.raw=raw183,.raw_len=2,
        .quality=TBX_J1587_OBS_EM_VALIDACAO,.has_numeric=true,.value=200,.uptime_ms=10710};
    xSemaphoreTake(s.lock,portMAX_DELAY);
    update_j1587_signal_locked(&comparison,NULL);
    xSemaphoreGive(s.lock);
    assert(comparison.raw==raw183 && comparison.raw_len==2 && raw183[0]==0x34 && raw183[1]==0x12);
    close_to(comparison.value,200); /* selection must not alter comparison observation */
    assert(s.fuel_derived && !s.fuel_j1587); close_to(s.fuel_rate,c0_rate); close_to(s.fuel_trip,expected);
    j1587_numeric(183,128,200,15701); /* passive fallback after model expires */
    assert(!s.fuel_derived && s.fuel_j1587); close_to(s.fuel_trip,expected);
    j1587_numeric(183,128,200,15801); expected+=200.0*.1/3600.0;
    close_to(s.fuel_trip,expected);
    can_u32(65266,1,0,2000,15850); close_to(s.fuel_trip,expected); /* no mixed network trapezoid */
    can_u32(65266,1,0,2000,15950); expected+=100.0*.1/3600.0;
    close_to(s.fuel_trip,expected);
    can_eec1(1,8000,175,16000); close_to(s.fuel_trip,expected);
    tb_capture_reset_trip(); close_to(s.fuel_trip,0); assert(!s.fuel_model.baseline);
    can_eec1(1,8000,175,16100); close_to(s.fuel_trip,0);
}
static void test_invalid_checkpoint_rejected(void)
{
    trip_blob_v1_t invalid = {.version = 1, .distance = NAN, .fuel = 1,
        .distance_valid = true, .fuel_valid = true};
    boot_with_checkpoint(&invalid, sizeof(invalid));
    assert(!s.distance_trip_valid && !s.fuel_trip_valid);
    invalid.distance = 12; invalid.version = 99;
    boot_with_checkpoint(&invalid, sizeof(invalid));
    assert(!s.distance_trip_valid && !s.fuel_trip_valid);
}
int main(void)
{
    test_v1_migration_and_v2_reboot();
    test_counter_gap_and_reset_no_double_count();
    test_j1939_preferred_then_passive_fallback();
    test_invalid_checkpoint_rejected();
    test_canonical_derived_fuel_and_fallback_boundaries();
    puts("Actual capture functions passed: v1 migration/v2 reboot, GPS-independent trip/source selection, gap/reset rebasing, unavailable readings, NVS outside RX lock, canonical C0 derived fuel/fallback boundaries");
    return 0;
}
