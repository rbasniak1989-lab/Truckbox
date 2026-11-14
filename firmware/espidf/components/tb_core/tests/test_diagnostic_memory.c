/* Production capture diagnostics and bridge preparation, synthetic bus state.
 * Only RTOS/config/clock and telemetry module boundaries are stubs. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "tb_json.h"
static bool diagnostic_lock_held;
#define TB_JSON_ALLOCATION_GUARD assert(!diagnostic_lock_held)
#include "../../tb_base/tests/json_heap.h"
#include "tbx_j1708_active.h"
#include "tbx_j1708_auto.h"
#include "tbx_transmission.h"
#define tb_config_j1708_auto_enabled() false
#define tb_config_j1708_auto_persisted() false
#include "tbx_j1708_policy.h"
#include "tbx_trip.h"
#include "tbx_fuel.h"
#include "accounting_json.h"
#include "polling_guard.h"
#include "telemetry_profile.h"
#define CONFIG_TB_J1708_ONLY 1
#define TB_RECORD_MAX 768
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define MAX_SAFE_SEQUENCE UINT64_C(9007199254740991)
#define BRIDGE_BATCH_LIMIT 8u
#define BRIDGE_BATCH_BYTES 8192u
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef unsigned nvs_handle_t;
static bool mutex_available=true;
static int64_t esp_timer_get_time(void){return 600000000;}
static bool xSemaphoreTake(void *lock,unsigned timeout){(void)lock;(void)timeout;assert(!diagnostic_lock_held);diagnostic_lock_held=mutex_available;return mutex_available;}
static void xSemaphoreGive(void *lock){(void)lock;assert(diagnostic_lock_held);diagnostic_lock_held=false;}
static uint32_t tb_boot_id(void){return 42;}
static const struct {bool capture_enabled,can_enabled,j1708_enabled,j1587_odometer_validated,j1587_fuel250_validated,full_decode_enabled;uint32_t can_baud;} config={true,false,true,false,false,false,250000};
#define tb_config() (&config)
static void tb_telemetry_add_json(cJSON *o)
{
    cJSON *v=cJSON_AddObjectToObject(o,"telemetry_v2");
    cJSON_AddBoolToObject(v,"decoding_enabled",true);
    cJSON_AddNumberToObject(v,"rx_queue_dropped",0);
}
#include "capture_diagnostic.h"
#include "bridge_prepare.h"
typedef struct{char bytes[24000];size_t used;}output_t;
static bool output(const char *bytes,size_t n,void *context)
{
    output_t *out=context;assert(n<sizeof(out->bytes)-out->used);
    memcpy(out->bytes+out->used,bytes,n);out->used+=n;out->bytes[out->used]=0;return true;
}
static void setup_capture(void)
{
    memset(&s,0,sizeof(s));s.lock=(void *)1;s.j1708_ready=s.edge_ready=true;
    tbx_j1708_request_init(&s.requests);tbx_j1708_active_init(&s.active,&s.requests,NULL,NULL);
    s.trip.quality="ESTIMADO_VELOCIDADE_J1587";s.fuel_account.quality="INTEGRADO_VAZAO_J1587_EM_VALIDACAO";
    strcpy(s.fuel_account.operation_uid,"operation-allocation-test");s.fuel_account.operation_open=true;
    s.speed_at=s.rpm_at=s.total_at=s.j1708_at=600000;s.rpm=840;s.j1708_frames=37525;s.frames_last_minute=3900;
    s.observed_mid[128>>3]|=1;s.observed_mid[130>>3]|=1<<(130&7);
    snprintf(s.j1708_last,sizeof(s.j1708_last),"{\"source\":\"j1708\",\"mid\":128,\"raw\":\"80BE401A68\",\"checksum_valid\":true,\"parameters\":[{\"pid\":190,\"data\":\"401A\"}]}");
}
static cJSON *field(cJSON *o,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);assert(v);return v;}
static size_t capture_allocation_calls(bool compact)
{
    heap_reset();cJSON *o=cJSON_CreateObject();
    if(compact)tb_capture_add_json_compact(o);else tb_capture_add_json(o);
    size_t calls=heap_calls;cJSON_Delete(o);assert(!heap_used);return calls;
}
static void fault_every_allocation(void)
{
    for(unsigned compact=0;compact<2;compact++){
        size_t total=capture_allocation_calls(compact);
        for(size_t fail=2;fail<=total;fail++){
            heap_reset();cJSON *o=cJSON_CreateObject();assert(o);heap_fail_call=fail;
            if(compact)tb_capture_add_json_compact(o);else tb_capture_add_json(o);
            const cJSON *a=cJSON_GetObjectItemCaseSensitive(o,"trip_accounting"),*op=cJSON_GetObjectItemCaseSensitive(a,"op");
            if(op)assert(cJSON_IsArray(op) && cJSON_GetArraySize(op)==3);
            cJSON_Delete(o);assert(!heap_used);
        }
        printf("Production capture %s: %zu individual allocation failures leave no orphan nodes\n",compact?"compact":"full",total-1);
    }
    heap_reset();cJSON *o=tb_capture_request_stats_json(0,8);size_t total=heap_calls;cJSON_Delete(o);assert(!heap_used);
    for(size_t fail=1;fail<=total;fail++){
        heap_reset();heap_fail_call=fail;o=tb_capture_request_stats_json(0,8);cJSON_Delete(o);assert(!heap_used);
    }
}
static void compact_and_pagination(void)
{
    heap_reset();cJSON *full=cJSON_CreateObject();tb_capture_add_json(full);
    size_t full_bytes=heap_used;
    cJSON *requests=field(field(full,"j1708"),"requests");assert(cJSON_GetArraySize(field(requests,"stats"))==TBX_J1708_REQUEST_CAPACITY);
    char *expected=cJSON_PrintUnformatted(full);assert(expected);
    size_t normal_peak=heap_peak;cJSON_free(expected);
    /* Reproduce pressure in the original state print; new path needs no spare cJSON buffer. */
    heap_budget=heap_used+256;assert(!cJSON_PrintUnformatted(full));size_t calls=heap_calls;
    output_t encoded={0};assert(tb_json_write_owned(full,output,&encoded));assert(calls==heap_calls && !heap_used);
    heap_reset();cJSON *small=cJSON_CreateObject();tb_capture_add_json_compact(small);
    size_t compact_bytes=heap_used;requests=field(field(small,"j1708"),"requests");
    assert(!cJSON_GetObjectItemCaseSensitive(requests,"stats") && !cJSON_GetObjectItemCaseSensitive(requests,"pids"));
    assert(cJSON_IsFalse(field(requests,"physical_validated")) && cJSON_IsFalse(field(requests,"work_mode")));
    assert(!strcmp(field(small,"capture_profile")->valuestring,"J1708_ONLY"));
    assert(compact_bytes<full_bytes*2/3);cJSON_Delete(small);assert(!heap_used);
    unsigned seen=0;
    for(unsigned offset=0;offset<TBX_J1708_REQUEST_CAPACITY;offset+=8){
        heap_reset();cJSON *page=tb_capture_request_stats_json(offset,8);assert(cJSON_IsTrue(field(page,"ok")));
        cJSON *rows=field(page,"stats");assert(cJSON_GetArraySize(rows)<=8);
        for(int i=0;i<cJSON_GetArraySize(rows);i++){
            cJSON *row=cJSON_GetArrayItem(rows,i);assert(field(row,"pid")->valueint==s.requests.pids[seen].pid);
            assert(field(row,"interval_ms")->valuedouble==s.requests.pids[seen].period_ms);seen++;
        }
        assert(heap_peak<full_bytes);cJSON_Delete(page);assert(!heap_used);
    }
    assert(seen==TBX_J1708_REQUEST_CAPACITY);
    heap_reset();cJSON *empty=tb_capture_request_stats_json(UINT32_MAX,UINT32_MAX);assert(field(empty,"count")->valueint==0);cJSON_Delete(empty);
    mutex_available=false;empty=tb_capture_request_stats_json(0,8);assert(cJSON_IsFalse(field(empty,"ok")));cJSON_Delete(empty);mutex_available=true;assert(!heap_used);
    printf("Production diagnostics native cJSON: full tree %zu bytes; old print peak %zu; compact tree %zu (%.1f%% smaller); 27 stats fetched in pages <=8\n",full_bytes,normal_peak,compact_bytes,100.0*(full_bytes-compact_bytes)/full_bytes);
}
static cJSON *batch(void)
{
    cJSON *o=cJSON_CreateObject();assert(cJSON_AddBoolToObject(o,"ok",true));assert(cJSON_AddNumberToObject(o,"count",8));
    cJSON *samples=cJSON_AddArrayToObject(o,"samples");assert(samples);
    for(unsigned i=0;i<8;i++){
        cJSON *row=cJSON_CreateObject();assert(cJSON_AddNumberToObject(row,"seq",i+1));
        assert(cJSON_AddStringToObject(row,"sample_kind","OPERATIONAL"));assert(cJSON_AddNumberToObject(row,"rpm",840));assert(tb_json_add_array(samples,row));
    }return o;
}
static void bridge_memory(void)
{
    heap_reset();cJSON *o=batch();char *expected=cJSON_PrintUnformatted(o);assert(expected);bridge_batch_t result;
    size_t calls=heap_calls;assert(prepare_bridge_batch(o,&result));assert(heap_calls==calls+1 && result.count==8);
    assert(!strcmp(expected,result.payload));for(unsigned i=0;i<8;i++)assert(result.sequences[i]==i+1);
    cJSON_free(expected);cJSON_free(result.payload);assert(!heap_used);
    o=batch();heap_fail_call=heap_calls+1;assert(!prepare_bridge_batch(o,&result));assert(!heap_used && !result.count && !result.payload);heap_fail_call=0;
    const char *invalid[]={"null","{}","{\"ok\":true}","{\"ok\":true,\"count\":1,\"samples\":[{\"seq\":0.5}]}","{\"ok\":true,\"count\":2,\"samples\":[{\"seq\":5},{\"seq\":4}]}","{\"ok\":true,\"count\":2,\"samples\":[{\"seq\":5},{\"seq\":5}]}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){assert(!prepare_bridge_batch(cJSON_Parse(invalid[i]),&result));assert(!heap_used && !result.count && !result.payload);}
    puts("Production bridge payload: one exact allocation, wire equivalence, exact sequence list, invalid batches and OOM consume all nodes");
}
/* Real HTTP handler + real capture builder under the field RAM budget.
 * Only the HTTP socket and base/network/store boundaries are synthetic. */
#define ESP_OK 0
#define ESP_FAIL -1
#define MALLOC_CAP_8BIT 1
typedef int esp_err_t;
typedef struct {char response[24000];size_t used;const char *query,*status;}httpd_req_t;
static const char *tb_role_name(void){return "core";}
static const char *tb_device_uid(void){return "synthetic-device";}
static const char *tb_version(void){return "0.2.18";}
static unsigned esp_get_free_heap_size(void){assert(heap_used<=41596);return (unsigned)(41596-heap_used);}
static unsigned esp_get_minimum_free_heap_size(void){return 12;}
static size_t heap_caps_get_largest_free_block(int cap){(void)cap;return 14592;}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;}
static void httpd_resp_set_type(httpd_req_t *r,const char *t){(void)r;(void)t;}
static void httpd_resp_set_status(httpd_req_t *r,const char *v){r->status=v;}
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *b,size_t n){assert(r->used+n<sizeof(r->response));memcpy(r->response+r->used,b,n);r->used+=n;r->response[r->used]=0;return ESP_OK;}
static esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *b,size_t n){return n?httpd_resp_send(r,b,n):ESP_OK;}
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *b,size_t n){if(!r->query || strlen(r->query)>=n)return ESP_FAIL;strcpy(b,r->query);return ESP_OK;}
static esp_err_t httpd_query_key_value(const char *q,const char *k,char *b,size_t n){if(strcmp(k,"compact") || strncmp(q,"compact=",8) || n<2)return ESP_FAIL;snprintf(b,n,"%s",q+8);return ESP_OK;}
static void tb_base_add_json(cJSON *o){cJSON_AddStringToObject(o,"role","core");cJSON_AddNumberToObject(o,"boot_id",42);}
static void tb_network_add_json(cJSON *o){cJSON_AddObjectToObject(o,"network");}
static void tb_application_add_json(cJSON *o){tb_capture_add_json(o);}
static void tb_application_add_json_compact(cJSON *o){tb_capture_add_json_compact(o);}
static bool tb_web_ota_busy(void){return false;}
#include "http_json.h"
static void http_pressure_and_diagnostic_isolation(void)
{
    setup_capture();s.active.field.started_us=1000000;s.active.field.reason="HISTORICAL_ONLY";
    heap_reset();heap_budget=41596;cJSON *full=cJSON_CreateObject();tb_capture_add_json(full);
    size_t unbounded_peak=heap_peak;assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(field(field(full,"j1708"),"requests"),"diagnostic_partial")));
    cJSON_Delete(full);assert(!heap_used);
    heap_reset();heap_budget=41596;
    void *upload=test_allocate(8192),*checkpoint=test_allocate(3232);assert(upload && checkpoint);
    const size_t resident=heap_used;
    for(unsigned i=0;i<200;i++){
        httpd_req_t r={.query=i&1?"compact=0":NULL};assert(state_handler(&r)==ESP_OK && !r.status && heap_used==resident);
        cJSON *o=cJSON_Parse(r.response);assert(o);
        assert(cJSON_IsTrue(field(o,"ok")) && cJSON_IsTrue(field(o,"diagnostic_compact")));
        assert(field(o,"heap_free")->valueint==41596-(int)resident && field(o,"heap_largest_block")->valueint==14592 && field(o,"heap_minimum")->valueint==12);
        assert(cJSON_IsFalse(field(field(field(o,"j1708"),"requests"),"armed")));
        cJSON_Delete(o);assert(heap_used==resident && !diagnostic_lock_held && s.j1708_ready && s.j1708_frames==37525);
    }
    size_t bounded_peak=heap_peak;test_free(upload);test_free(checkpoint);assert(!heap_used);
    heap_reset();heap_budget=0;httpd_req_t health={0};assert(health_handler(&health)==ESP_OK && heap_calls==0);
    assert(strstr(health.response,"\"boot_id\":42") && strstr(health.response,"heap_free") && strstr(health.response,"heap_largest_block"));
    heap_reset();printf("Real HTTP pressure: field budget41596, unbounded full peak%zu (incomplete), compact peak%zu including8192 upload+3232 checkpoint; 200 repeats, zero leaks/allocations under TX mutex; passive state and disabled polling preserved; health works with zero heap PASS\n",unbounded_peak,bounded_peak);
}

int main(void)
{
    setup_capture();compact_and_pagination();fault_every_allocation();
    s.active.field.started_us=1000000;s.active.field.phase=TBX_FIELD_SATISFACTORY;s.active.field.reason="SYNTHETIC_DIAGNOSTIC_FIXTURE";
    compact_and_pagination();fault_every_allocation();bridge_memory();http_pressure_and_diagnostic_isolation();return 0;
}
