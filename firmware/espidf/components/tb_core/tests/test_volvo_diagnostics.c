#include "diagnostics_host_stubs.h"
#include "../diagnostics.h"
#include "../ecu_counters.h"
#include "../tbx_volvo_operating.h"
#include "../store_internal.h"
#include "tbx_j1708_policy.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint8_t value[8192],pending[8192];size_t size,pending_size;}blob_t;
static blob_t blobs[2][2];static unsigned writes,records,notifications,peak_record;static uint64_t now_ms;static uint32_t boot=306;static bool write_error,store_error;
static size_t allocations,fail_allocation;static bool in_store;
static void *fault_malloc(size_t size){if(!in_store && ++allocations==fail_allocation)return NULL;return malloc(size);}
int64_t esp_timer_get_time(void){return (int64_t)(now_ms*1000);}
uint32_t tb_boot_id(void){return boot;}
const char *tb_device_uid(void){return "tb-c6-d40592b9190c";}
static bool nvs_held;
SemaphoreHandle_t xSemaphoreCreateMutex(void){return 1;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned ticks){assert(m==1 && ticks==1000 && !nvs_held);nvs_held=true;return pdTRUE;}
void xSemaphoreGive(SemaphoreHandle_t m){assert(m==1 && nvs_held);nvs_held=false;}
int nvs_open(const char *name,int mode,nvs_handle_t *h){assert(mode==1);*h=!strcmp(name,"tb_diag")?0:1;return ESP_OK;}
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){blob_t *b=&blobs[h][!strcmp(key,"b")];if(!b->size)return ESP_FAIL;assert(*size>=b->size);memcpy(out,b->value,b->size);*size=b->size;return ESP_OK;}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n){if(write_error)return ESP_FAIL;blob_t *b=&blobs[h][!strcmp(key,"b")];assert(n<=sizeof(b->pending));memcpy(b->pending,p,n);b->pending_size=n;return ESP_OK;}
int nvs_commit(nvs_handle_t h){if(write_error)return ESP_FAIL;for(unsigned i=0;i<2;i++){blob_t *b=&blobs[h][i];if(b->pending_size){memcpy(b->value,b->pending,b->pending_size);b->size=b->pending_size;b->pending_size=0;}}++writes;return ESP_OK;}
int tb_store_submit(const char *p){assert(strlen(p)<TB_RECORD_MAX);if(strlen(p)>peak_record)peak_record=(unsigned)strlen(p);if(store_error)return ESP_FAIL;in_store=true;cJSON *j=cJSON_Parse(p);assert(j);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(j,"sample_kind")->valuestring,"J1708_REQUEST_EVENT"));const cJSON *event=cJSON_GetObjectItemCaseSensitive(j,"event");assert(cJSON_IsString(event));if(!strcmp(event->valuestring,"DIAGNOSTIC_RAW") || !strcmp(event->valuestring,"DIAGNOSTIC_INVALID")){assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(j,"parameter_hex")));assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j,"capture_ms")));assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j,"total_bytes")));assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j,"offset")));}++records;const cJSON *notify=cJSON_GetObjectItemCaseSensitive(j,"notify");if(cJSON_IsTrue(notify))++notifications;cJSON_Delete(j);in_store=false;return ESP_OK;}
static void diagnostic(const tbx_j1587_observation_t *o,void *unused){(void)unused;tb_diagnostics_observe(o,UINT64_C(1794430000000),NULL);}
static void diagnostic_frame(uint8_t mid,const uint8_t *data,size_t length)
{
    uint8_t f[21]={mid,194,(uint8_t)length};assert(length<=17);if(length)memcpy(f+3,data,length);
    uint8_t sum=0;for(size_t i=0;i<length+3;i++)sum=(uint8_t)(sum+f[i]);f[length+3]=(uint8_t)(0u-sum);
    assert(tbx_j1587_bridge_decode(f,length+4,now_ms,diagnostic,NULL)==TBX_J1587_FRAME_OK);
}
static void diagnostics_persistence(void)
{
    tb_diagnostics_start();now_ms=1000;
    const uint8_t motor[]={0x12,0xb5,1},abs[]={2,0x35},gear[]={0x25,0xa1,1},old[]={0x25,0xe1,0};
    diagnostic_frame(128,motor,3);diagnostic_frame(136,abs,2);diagnostic_frame(130,gear,3);tb_diagnostics_flush();
    assert(notifications==3);unsigned before=records;
    for(unsigned i=0;i<1000;i++){++now_ms;diagnostic_frame(128,motor,3);tb_diagnostics_flush();}
    assert(records==before && notifications==3 && writes==0);
    now_ms=61000;tb_diagnostics_checkpoint();assert(writes==1);
    diagnostic_frame(130,old,3);tb_diagnostics_flush();assert(notifications==4);
    now_ms=122000;tb_diagnostics_checkpoint();assert(writes==2);
    blobs[0][0].value[8]^=0x80; /* corrupt newer generation2; keep slot1 */
    ++boot;tb_diagnostics_start();cJSON *j=tb_diagnostics_json(0,8);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(j,"faults"))==3);cJSON_Delete(j);
    before=notifications;tb_diagnostics_flush();assert(notifications==before); /* restart is not a new activation */
    diagnostic_frame(128,NULL,0);j=tb_diagnostics_json(0,8);cJSON *first=cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j,"faults"),0);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(first,"state")->valuestring,"ACTIVE"));cJSON_Delete(j);
    store_error=true;const uint8_t unknown[]={0xfa,0x2f};diagnostic_frame(144,unknown,2);tb_diagnostics_flush();
    store_error=false;before=records;tb_diagnostics_flush();assert(records>before);
}
static tbx_j1587_observation_t observation(unsigned pid,uint32_t raw,uint64_t ms)
{
    static uint8_t bytes[4];for(unsigned i=0;i<4;i++)bytes[i]=(uint8_t)(raw>>(8*i));
    tbx_j1587_observation_t o={.pid=(uint16_t)pid,.mid=128,.uptime_ms=ms,.quality=TBX_J1587_OBS_EM_VALIDACAO,.has_numeric=true,.raw=bytes,.raw_len=4};
    o.requested.raw_unsigned=raw;return o;
}
static bool sent(uint16_t pid,void *context){*(uint16_t *)context=pid;return true;}
static void counter_capture(tbx_j1708_request_scheduler_t *s,uint32_t rawbase)
{
    for(unsigned tick=0;tick<500;tick++){
        now_ms+=100;tb_ecu_counters_tick(s,true,0,true,now_ms,2,60);
        uint16_t pid=0;tbx_j1708_request_tick(s,(uint32_t)now_ms,sent,&pid);
        if(pid){tbx_j1587_observation_t o=observation(pid,rawbase+(pid==235?10:0),now_ms+20);
            bool matched=tbx_j1708_request_note_response(s,pid,(uint32_t)now_ms+20);assert(matched);tb_ecu_counters_observe(&o,matched);}
        tb_ecu_counters_flush();
    }
}
static void counters_and_reboot(void)
{
    tbx_j1708_request_scheduler_t scheduler;tbx_j1708_request_init(&scheduler);
    uint32_t periods[17];for(unsigned i=0;i<17;i++)periods[i]=tbx_j1708_production_period(scheduler.pids[i].pid,false);
    tbx_j1708_request_permit_stationary_test(&scheduler,true);assert(tbx_j1708_request_arm(&scheduler));
    tbx_j1708_request_production(&scheduler,periods,false,(uint32_t)now_ms,true);
    tb_ecu_counters_start();assert(tb_ecu_counters_boundary(true,"trip-actual-1"));counter_capture(&scheduler,1000);
    cJSON *j=tb_ecu_counters_json();assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(j,"latest"))==3);assert(!strcmp(cJSON_GetObjectItemCaseSensitive(j,"trip_client_uid")->valuestring,"trip-actual-1"));cJSON_Delete(j);
    unsigned count=writes;now_ms+=61000;tb_ecu_counters_checkpoint(false);assert(writes>=count);
    count=writes;counter_capture(&scheduler,1000);now_ms+=61000;tb_ecu_counters_checkpoint(false);assert(writes==count); /* unchanged raw values don't wear NVS */
    ++boot;tb_ecu_counters_start();j=tb_ecu_counters_json();assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"restored")));cJSON_Delete(j);
    assert(tb_ecu_counters_boundary(false,"trip-actual-1"));counter_capture(&scheduler,1001);
    tbx_ecu_counter_t a={.raw=1000,.boot=1,.uptime_ms=1000,.mid=128,.valid=true},b=a;double delta=0;
    b.raw=1010;b.uptime_ms+=3600000;assert(tbx_ecu_counter_delta(&a,&b,0,&delta)==0 && delta>4.73 && delta<4.74);
    b.raw=1;assert(tbx_ecu_counter_delta(&a,&b,0,&delta)==2);b=a;b.mid=130;assert(tbx_ecu_counter_delta(&a,&b,0,&delta)==2);
    b=a;b.boot=2;assert(tbx_ecu_counter_delta(&a,&b,0,&delta)==3);
    b=a;b.raw=1000000;b.uptime_ms+=1000;assert(tbx_ecu_counter_delta(&a,&b,0,&delta)==3);
    char uid[65];memset(uid,'a',64);uid[64]=0;
    count=records;assert(tb_ecu_counters_boundary(true,uid));counter_capture(&scheduler,2000);
    j=tb_ecu_counters_json();assert(!strcmp(cJSON_GetObjectItemCaseSensitive(j,"trip_client_uid")->valuestring,uid));
    assert(cJSON_GetObjectItemCaseSensitive(j,"store_errors")->valuedouble==0 && records>count);cJSON_Delete(j);
    write_error=true;assert(!tb_ecu_counters_boundary(false,uid));write_error=false;
    assert(tb_ecu_counters_boundary(false,uid));counter_capture(&scheduler,2001);
}
static void operating_sample(tbx_volvo_operating_t *v,unsigned pid,double value,uint64_t ms,const char *text)
{
    tbx_j1587_observation_t o={.pid=(uint16_t)pid,.mid=(pid==161 || pid==162 || pid==163)?130:128,
        .uptime_ms=ms,.quality=TBX_J1587_OBS_EM_VALIDACAO,.has_numeric=true,.value=value};
    if(text){o.has_numeric=false;o.passive.interpreted=true;snprintf(o.passive.text,sizeof(o.passive.text),"%s",text);}
    tbx_volvo_operating_observe(v,&o);
}
static void volvo_derived_signals(void)
{
    tbx_volvo_operating_t v={0};uint64_t ms=1000;
    for(unsigned i=0;i<20;i++,ms+=500){
        operating_sample(&v,84,40,ms,NULL);operating_sample(&v,190,650,ms,NULL);operating_sample(&v,91,0,ms,NULL);
        operating_sample(&v,188,650,ms,NULL);operating_sample(&v,183,3,ms,NULL);
        operating_sample(&v,162,0,ms,"A");operating_sample(&v,163,0,ms,"N");operating_sample(&v,122,50,ms,NULL);
    }
    assert(v.iroll_probable && v.iroll_seconds>5 && v.iroll_seconds<11 && v.iroll_distance_km>0 && v.iroll_fuel_l>0);
    assert(v.veb.valid && v.veb.value==50 && !v.slip_candidate); /* no lever/stage inference */
    tbx_volvo_operating_tick(&v,ms+4000);assert(!v.iroll_probable && !v.rpm_difference_valid);
    memset(&v,0,sizeof(v));
    for(unsigned i=0;i<20;i++,ms+=500){
        operating_sample(&v,84,20,ms,NULL);operating_sample(&v,190,1400,ms,NULL);operating_sample(&v,91,20,ms,NULL);
        operating_sample(&v,161,900,ms,NULL);operating_sample(&v,162,0,ms,"A");operating_sample(&v,163,0,ms,"12");
    }
    assert(v.rpm_difference_valid && v.rpm_difference==500 && v.slip_candidate);
    operating_sample(&v,163,0,ms,"13");assert(!v.slip_candidate); /* shift exclusion */
    operating_sample(&v,190,1400,ms+1500,NULL);assert(!v.rpm_difference_valid); /* async old shaft value */
    memset(&v,0,sizeof(v));operating_sample(&v,190,650,ms,NULL);tbx_volvo_operating_tick(&v,ms);assert(!v.iroll_probable);
}
static void counter_missing_read_retry(void)
{
    tbx_ecu_counters_t c;tbx_ecu_counters_init(&c);tbx_j1708_request_scheduler_t scheduler;tbx_j1708_request_init(&scheduler);
    assert(tbx_ecu_counters_trigger(&c,TBX_COUNTER_TRIP_START,"no-network-trip"));
    tbx_ecu_counters_tick(&c,&scheduler,1000);assert(!c.active && c.queued==1);
    tbx_j1708_request_permit_stationary_test(&scheduler,true);assert(tbx_j1708_request_arm(&scheduler));
    uint32_t periods[17];for(unsigned i=0;i<17;i++)periods[i]=tbx_j1708_production_period(scheduler.pids[i].pid,false);
    tbx_j1708_request_production(&scheduler,periods,false,1000,true);tbx_ecu_counters_tick(&c,&scheduler,1000);
    c.latest[0]=(tbx_ecu_counter_t){.raw=1234,.valid=true,.mid=128};
    tbx_j1587_observation_t na=observation(250,UINT32_MAX,1100);na.has_numeric=false;na.quality=TBX_J1587_OBS_INDISPONIVEL;
    tbx_ecu_counters_observe(&c,&na,true,306,0);assert(c.missing==1 && c.phase==1 && c.latest[0].raw==1234);
    tbx_ecu_counters_tick(&c,&scheduler,1200);tbx_j1708_request_stop(&scheduler);
    tbx_ecu_counters_tick(&c,&scheduler,33000);assert(c.record_ready && !c.active && c.failed==1 && c.missing==7);
    bool retry=false;for(unsigned i=0;i<c.queued;i++)if(c.waiting[i].attempt==1){assert(c.waiting[i].not_before_ms>=93000);retry=true;}assert(retry);
}
static void diagnostic_allocation_failures(void)
{
    const uint8_t motor[]={0x12,0xb5,1};cJSON_Hooks hooks={fault_malloc,free};cJSON_InitHooks(&hooks);
    tb_diagnostics_start();allocations=fail_allocation=0;diagnostic_frame(128,motor,3);tb_diagnostics_flush();size_t count=allocations;
    for(size_t failure=1;failure<=count;failure++){
        tb_diagnostics_start();allocations=0;fail_allocation=failure;diagnostic_frame(128,motor,3);tb_diagnostics_flush();
        fail_allocation=0;diagnostic_frame(128,motor,3);tb_diagnostics_flush();
    }
    cJSON_InitHooks(NULL);printf("PID194 OOM: %zu allocation fault points; no partial kind/raw identity accepted by store PASS\n",count);
}
int main(void)
{
    diagnostics_persistence();counters_and_reboot();counter_missing_read_retry();volvo_derived_signals();
    diagnostic_allocation_failures();
    printf("Volvo real modules: PID194 wire length/Cloud bytes, dedup, A/B NVS/torn checkpoint, offline store retry, counter capture/start/end/reboot, unchanged suppression and reset/MID/time/range guards PASS (%u bounded records, peak %u bytes)\n",records,peak_record);
    return 0;
}
