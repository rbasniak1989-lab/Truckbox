/* The runner extracts these exact production functions into the included header;
 * no duplicated signal formulas or reimplemented migration path is tested. */
#include "capture_host_stubs.h"
#include <assert.h>
#include <stdlib.h>
#include <setjmp.h>
static int64_t host_now_us;
static uint8_t host_nvs[256];
static size_t host_nvs_size;
static unsigned char slot_a[512],slot_b[512];
static size_t slot_sizes[2];
static unsigned host_commits, host_mutexes;
static unsigned host_held,host_audits;
typedef struct {int type;const uint8_t *bytes;size_t length;bool timeout;uint32_t delay_us;}host_rx_event_t;
static host_rx_event_t host_rx_events[16];
static unsigned host_rx_count,host_rx_cursor,host_rx_offset,host_rx_flushes;
static jmp_buf host_rx_done;
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
static bool xQueueReceive(QueueHandle_t queue,void *item,unsigned ticks)
{
    (void)queue;(void)ticks;
    if(host_rx_cursor==host_rx_count)longjmp(host_rx_done,1);
    const host_rx_event_t *e=&host_rx_events[host_rx_cursor++];host_now_us+=e->delay_us;
    host_rx_offset=0;if(!e->type)return false;
    *(uart_event_t *)item=(uart_event_t){e->type,e->length,e->timeout};
    if(e->type==UART_DATA)s.rx_edge_us=(uint32_t)host_now_us;
    return true;
}
static bool xQueueSend(QueueHandle_t q,const void *v,unsigned t){(void)q;(void)v;(void)t;return true;}
static void xQueueReset(QueueHandle_t q){(void)q;}
static int uart_read_bytes(int port,void *out,size_t count,unsigned ticks)
{
    (void)ticks;assert(port==1);const host_rx_event_t *e=&host_rx_events[host_rx_cursor-1];
    assert(host_rx_offset+count<=e->length);memcpy(out,e->bytes+host_rx_offset,count);host_rx_offset+=(unsigned)count;return (int)count;
}
static void uart_flush_input(int p){assert(p==1);++host_rx_flushes;}
static int gpio_get_level(int p){assert(p==20);return 1;}
static unsigned uart_ll_get_rxfifo_len(int p){assert(p==1);return 0;}
static int uart_get_buffered_data_len(int p,size_t *n){assert(p==1);*n=0;return 0;}
static bool tb_telemetry_j1708(const uint8_t *b,size_t n,uint64_t t,bool o){(void)b;(void)n;(void)t;(void)o;return true;}
static bool tb_telemetry_j1708_echo(const uint8_t *b,size_t n,uint64_t t,bool o){(void)b;(void)n;(void)t;(void)o;return true;}
static void run_rx(void)
{
    host_rx_cursor=host_rx_offset=0;if(!setjmp(host_rx_done))j1708_task(NULL);assert(!host_held);
}
static esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ (void)mode; assert(!strcmp(name, "tb_trip")); *handle = 1; return ESP_OK; }
static esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *buffer, size_t *size)
{
    (void)handle;
    if(strcmp(key,"trip")){
        unsigned i=!strcmp(key,"trip3a")?0:1;assert(!strcmp(key,i?"trip3b":"trip3a"));
        if(!slot_sizes[i])return ESP_ERR_NVS_NOT_FOUND;
        assert(*size>=slot_sizes[i]);*size=slot_sizes[i];memcpy(buffer,i?slot_b:slot_a,*size);return ESP_OK;
    }
    if (!host_nvs_size) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < host_nvs_size) { *size = host_nvs_size; return ESP_ERR_NVS_INVALID_LENGTH; }
    *size = host_nvs_size;
    memcpy(buffer, host_nvs, *size);
    return ESP_OK;
}
static esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *buffer, size_t size)
{
    (void)handle;
    if(strcmp(key,"trip")){
        unsigned i=!strcmp(key,"trip3a")?0:1;assert(!strcmp(key,i?"trip3b":"trip3a") && size<=512);
        assert(!(host_held & (unsigned)(uintptr_t)s.lock));
        memcpy(i?slot_b:slot_a,buffer,size);slot_sizes[i]=size;return ESP_OK;
    }
    assert(size <= sizeof(host_nvs));
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
    memset(slot_sizes,0,sizeof(slot_sizes));
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
    assert(host_commits == 2 && host_nvs_size == sizeof(trip_blob_t));
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
    assert(!s.trip.counter_valid && s.speed_at == 0 && s.fuel_at == 0 && host_commits == 2);
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
static void test_movement_stops_parked_trial_but_keeps_authorized_work(void)
{
    trip_blob_v1_t initial={.version=1};
    boot_with_checkpoint(&initial,sizeof(initial));
    tbx_j1708_active_init(&s.active,&s.requests,NULL,NULL);
    s.requests.armed=true;
    j1587_numeric(84,128,40,1000);
    assert(!s.requests.armed);
    s.requests.armed=true;s.active.work_mode=true;
    j1587_numeric(84,128,42,2000);
    assert(s.requests.armed && s.active.work_mode);
    twai_message_t frame={.extd=true,.identifier=0x18FEF111,.data_length_code=8};
    frame.data[1]=0;frame.data[2]=50; /* 50km/h, canonical little-endian speed. */
    update_signals(&frame);
    assert(s.requests.armed && s.active.work_mode);
}

static tbx_j1708_tx_result_t proof_begin(const uint8_t *f,size_t n,void *unused){(void)f;(void)n;(void)unused;return TBX_J1708_TX_STARTED;}
static void proof_abort(void *unused){(void)unused;}
static void test_external_evidence_owner(void)
{
    boot_with_checkpoint(NULL,0);
    tbx_j1708_active_ops_t ops={.begin_tx=proof_begin,.abort_tx=proof_abort};
    tbx_j1708_active_init(&s.active,&s.requests,&ops,NULL);
    tbx_j1708_active_config_t config={.local_mid=242,.priority=8,.inverted_de21_attested=true,.single_shot=true};
    for(unsigned i=0;i<17;i++)config.target_mid[i]=128;
    assert(tbx_j1708_active_configure(&s.active,&config));
    assert(tbx_j1708_active_arm(&s.active,true,true,1000000));
    tbx_j1708_active_tick(&s.active,1005000);
    tbx_j1708_active_note_rx(&s.active,s.active.tx_frame,5,1011000);
    assert(tbx_j1708_active_note_response(&s.active,128,26,1205000));
    tbx_j1708_active_tick(&s.active,1205001);assert(!s.requests.armed);
    uint8_t reply[]={128,26,1,101};memcpy(s.trial_reply,reply,sizeof(reply));s.trial_reply_size=4;
    host_now_us=1210000;s.speed_at=1200;s.speed=0;
    cJSON *body=cJSON_Parse("{\"stationary\":true,\"external_capture_verified\":true,\"local_mid_free_verified\":true,\"trial_id\":\"42:1005000\",\"tx_observed_hex\":\"F2 80 1A 80 F4\",\"response_observed_hex\":\"80 1A 01 65\"}");assert(body);
    /* TX checksum of F2 80 1A 80 isF4; no proof from echo alone. */
    assert(!tbx_j1708_active_work_ready(&s.active,host_now_us));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"trial_id",cJSON_CreateString("41:1005000"));assert(!tb_capture_confirm_physical(body));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"trial_id",cJSON_CreateString("42:1005000"));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"external_capture_verified",cJSON_CreateFalse());assert(!tb_capture_confirm_physical(body));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"external_capture_verified",cJSON_CreateTrue());
    cJSON_ReplaceItemInObjectCaseSensitive(body,"response_observed_hex",cJSON_CreateString("81 1A 01 64"));assert(!tb_capture_confirm_physical(body));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"response_observed_hex",cJSON_CreateString("80 1A 01 64"));assert(!tb_capture_confirm_physical(body));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"response_observed_hex",cJSON_CreateString("80 1A 01 65"));
    s.speed=30;assert(!tb_capture_confirm_physical(body));s.speed=0;
    assert(tb_capture_confirm_physical(body) && host_audits==1 && s.active.physical_validated);
    s.edge_ready=s.j1708_ready=true;
    s.observed_mid[242>>3]|=(uint8_t)(1u<<(242&7));
    assert(!tb_capture_request_enable_work(true,true)); /* Newly observed conflict after trial. */
    s.observed_mid[242>>3]=0;
    assert(tb_capture_request_enable_work(true,true));
    cJSON_Delete(body);assert(!host_held);
}

/* Boundary fixture for authorization/speed/nonce; complete measured rounds are
 * exercised by test_j1708_field with the production controller. */
static void test_field_owner_speed_nonce_and_no_external_evidence(void)
{
    unsigned before_audits=host_audits;
    boot_with_checkpoint(NULL,0);
    tbx_j1708_active_ops_t ops={.begin_tx=proof_begin,.abort_tx=proof_abort};
    tbx_j1708_active_init(&s.active,&s.requests,&ops,NULL);
    tbx_j1708_active_config_t config={.local_mid=172,.priority=8,.inverted_de21_attested=true};
    for(unsigned i=0;i<17;i++)config.target_mid[i]=128;
    assert(tbx_j1708_active_configure(&s.active,&config));
    host_now_us=1000000;s.j1708_ready=s.edge_ready=true;s.speed_at=1000;s.speed=0;
    assert(!tb_capture_field_start(false,true));assert(!tb_capture_field_start(true,false));
    s.speed=40;assert(!tb_capture_field_start(true,true));s.speed=0;
    s.observed_mid[172>>3]|=1u<<(172&7);assert(!tb_capture_field_start(true,true));s.observed_mid[172>>3]=0;
    assert(tb_capture_field_start(true,true));assert(!s.active.physical_validated && s.active.field.started_us==1000000);
    cJSON *body=cJSON_Parse("{\"authorized\":true,\"stationary\":true,\"local_mid_free_verified\":true,\"indirect_validation_acknowledged\":true,\"session_id\":\"42:1000000\"}");assert(body);
    assert(!tb_capture_field_confirm(body)); /* Measuring is not a satisfactory result. */
    s.requests.armed=false;s.active.field.phase=TBX_FIELD_SATISFACTORY;s.active.field.completed_us=1000000;
    cJSON_ReplaceItemInObjectCaseSensitive(body,"session_id",cJSON_CreateString("41:1000000"));assert(!tb_capture_field_confirm(body));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"session_id",cJSON_CreateString("42:1000000"));
    s.speed=20;assert(!tb_capture_field_confirm(body));s.speed=0;
    host_now_us=7000000;assert(!tb_capture_field_confirm(body)); /* stale speed */
    s.speed_at=7000;cJSON_ReplaceItemInObjectCaseSensitive(body,"indirect_validation_acknowledged",cJSON_CreateFalse());assert(!tb_capture_field_confirm(body));
    cJSON_ReplaceItemInObjectCaseSensitive(body,"indirect_validation_acknowledged",cJSON_CreateTrue());
    assert(tb_capture_field_confirm(body));assert(s.active.field.confirmed && !s.active.physical_validated && host_audits==before_audits);
    cJSON_Delete(body);assert(!host_held);
}

static void test_production_uart_partitioning_and_candidates(void)
{
    boot_with_checkpoint(NULL,0);host_rx_flushes=0;s.j1708_ready=true;
    const tbx_j1708_active_ops_t ops={.begin_tx=proof_begin,.abort_tx=proof_abort};
    tbx_j1708_active_init(&s.active,&s.requests,&ops,NULL);
    tbx_j1708_active_config_t cfg={.local_mid=172,.priority=8,.inverted_de21_attested=true,.single_shot=true};
    for(unsigned i=0;i<17;i++)cfg.target_mid[i]=128;
    assert(tbx_j1708_active_configure(&s.active,&cfg));
    host_now_us=1000000;assert(tbx_j1708_active_arm(&s.active,true,true,host_now_us));
    host_now_us+=5000;tbx_j1708_active_tick(&s.active,host_now_us);assert(s.active.tx_active);
    uint8_t echo[5];memcpy(echo,s.active.tx_frame,5);
    const uint8_t bad[]={128,26,1,100},wrong_mid[]={130,26,1,99},matching[]={128,26,1,101};
    const uint8_t oversized[22]={128};
    host_rx_event_t script[]={
        {UART_DATA,echo,1,false,1100},{UART_DATA,echo+1,4,false,4400},
        {UART_DATA,bad,4,true,6000},{UART_DATA,oversized,22,true,23000},
        {UART_DATA,wrong_mid,4,true,6000},{UART_DATA,matching,4,true,200000},
        {UART_DATA,matching,2,false,20000},{UART_DATA,matching+2,2,false,2200},
        {0,NULL,0,false,20000}};
    memcpy(host_rx_events,script,sizeof(script));host_rx_count=sizeof(script)/sizeof(*script);run_rx();
    assert(s.active.echo_ok==1 && s.requests.answered_total==1);
    assert(s.j1708_frames==3 && s.j1708_checksum_errors==1 && s.j1708_oversize==1);
    assert(s.active.rx_guard.total.valid==3 && s.active.rx_guard.total.checksum==1 && s.active.rx_guard.total.framing==1);
    assert(!(s.observed_mid[172>>3]&(1u<<(172&7))));
    assert(!s.active.physical_validated); /* Matching spontaneous data still only a candidate. */
    /* Actual receiver overflow path: stop blind TX, then keep passive RX alive. */
    tbx_j1708_active_stop(&s.active,"OPERATOR_STOP");
    assert(tbx_j1708_active_configure(&s.active,&cfg));host_now_us+=1000000;
    assert(tbx_j1708_active_arm(&s.active,true,true,host_now_us));host_now_us+=5000;
    tbx_j1708_active_tick(&s.active,host_now_us);assert(s.active.tx_active);
    host_rx_events[0]=(host_rx_event_t){UART_FIFO_OVF,NULL,0,false,1000};
    host_rx_events[1]=(host_rx_event_t){UART_DATA,matching,4,true,6000};host_rx_count=2;run_rx();
    assert(!s.requests.armed && !s.active.tx_active && s.j1708_ready && host_rx_flushes==1);
    assert(s.active.rx_guard.total.overruns==1 && !s.active.collisions);
    assert(s.j1708_frames==4 && s.requests.answered_total==1);
    puts("Actual UART task/finish replay: fragmented echo, split/idle frame, bad checksum, oversized/coalesced input, wrong MID, spontaneous candidates, RX overflow during TX and passive continuation PASS");
}

static void test_reupgrade_imports_legacy_trip_advanced_after_rollback(void)
{
    trip_blob_t base={.version=2,.distance=10,.fuel=2,.distance_valid=true,.fuel_valid=true};
    boot_with_checkpoint(&base,sizeof(base));persist_trip();assert(slot_sizes[0]);
    /* Simulate old firmware running after rollback: it writes only trip. */
    trip_blob_t advanced={.version=2,.distance=11.5,.fuel=2.7,.distance_valid=true,.fuel_valid=true};
    memcpy(host_nvs,&advanced,sizeof(advanced));host_nvs_size=sizeof(advanced);
    memset(&s,0,sizeof(s));host_mutexes=host_held=0;assert(host_start_restore()==ESP_OK);
    close_to(s.distance_trip,11.5);close_to(s.fuel_trip,2.7);
    assert(!s.trip.odo_valid && !s.fuel_account.direct_valid);
    assert(!strcmp(s.trip.quality,"RECUPERADO_CHECKPOINT_LEGADO_ALTERADO"));
}

static void verified_counter_observation(uint16_t pid,uint8_t mid,uint32_t raw,uint64_t ms)
{
    uint8_t data[4];for(unsigned i=0;i<4;i++)data[i]=(uint8_t)(raw>>(8*i));
    tbx_j1587_observation_t o={.pid=pid,.mid=mid,.uptime_ms=ms,.raw=data,.raw_len=4,.quality=TBX_J1587_OBS_EM_VALIDACAO,.has_numeric=true,
        .value=raw*(pid==245?.1609344:.473176473)};
    host_now_us=(int64_t)ms*1000;xSemaphoreTake(s.lock,portMAX_DELAY);update_j1587_signal_locked(&o,NULL);xSemaphoreGive(s.lock);
}
static void test_validated_counter_mids_and_real_checkpoint_restore(void)
{
    trip_blob_t b={.version=2,.distance=10,.fuel=2,.distance_valid=true,.fuel_valid=true};boot_with_checkpoint(&b,sizeof(b));
    host_capture_config.j1708_enabled=true;host_capture_config.j1587_odometer_validated=host_capture_config.j1587_fuel250_validated=true;
    host_capture_config.j1587_odometer_mid=host_capture_config.j1587_fuel250_mid=128;
    verified_counter_observation(245,130,1000,1000);assert(!s.trip.odo_valid);
    verified_counter_observation(245,128,1000,2000);verified_counter_observation(245,128,1001,5000);close_to(s.distance_trip,10.1609344);
    verified_counter_observation(250,130,1000,5001);assert(!s.fuel_account.direct_valid);
    verified_counter_observation(250,128,1000,5100);verified_counter_observation(250,128,1002,65000);close_to(s.fuel_trip,2+2*.473176473);
    persist_trip();memset(&s,0,sizeof(s));host_mutexes=host_held=0;assert(host_start_restore()==ESP_OK);
    assert(s.trip.odo_restored && s.fuel_account.direct_valid);
    verified_counter_observation(245,128,1002,1000);close_to(s.distance_trip,10.3218688);
    verified_counter_observation(250,128,1003,1100);close_to(s.fuel_trip,2+3*.473176473);
    verified_counter_observation(250,128,1003,1200);close_to(s.fuel_trip,2+3*.473176473);
    memset(&host_capture_config,0,sizeof(host_capture_config));
}

int main(void)
{
    test_validated_counter_mids_and_real_checkpoint_restore();
    test_production_uart_partitioning_and_candidates();
    test_external_evidence_owner();
    test_field_owner_speed_nonce_and_no_external_evidence();
    test_reupgrade_imports_legacy_trip_advanced_after_rollback();
    test_movement_stops_parked_trial_but_keeps_authorized_work();
    test_v1_migration_and_v2_reboot();
    test_counter_gap_and_reset_no_double_count();
    test_j1939_preferred_then_passive_fallback();
    test_invalid_checkpoint_rejected();
    test_canonical_derived_fuel_and_fallback_boundaries();
    puts("Actual capture functions passed: v1 migration/v2 reboot, GPS-independent trip/source selection, gap/reset rebasing, unavailable readings, NVS outside RX lock, canonical C0 derived fuel/fallback boundaries");
    return 0;
}
