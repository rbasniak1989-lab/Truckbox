#ifndef TB_COUNTER_HOST_TEST
#include "tb.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#else
#include "tests/diagnostics_host_stubs.h"
#endif
#include "ecu_counters.h"
#include "record_time.h"
#include "store_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include "../tb_base/include/tb_json.h"
#include <string.h>
#include <sys/time.h>

typedef struct {uint32_t version,generation;tbx_ecu_counters_t state;uint32_t crc;}counter_checkpoint_t;
static struct {
    tbx_ecu_counters_t state;
    nvs_handle_t nvs;SemaphoreHandle_t persist_lock;
    bool nvs_open,restored;
    uint64_t last_persist_ms;
    uint32_t generation,write_errors,store_errors,unchanged_suppressed;
    uint32_t recorded_raw[3];uint8_t recorded_mask;
} ec;
static portMUX_TYPE counter_mux=portMUX_INITIALIZER_UNLOCKED;
static uint64_t utc_ms(void)
{
    struct timeval t;
    return gettimeofday(&t,NULL)==0 && t.tv_sec>=1704067200?(uint64_t)t.tv_sec*1000+(uint64_t)t.tv_usec/1000:0;
}
void tb_ecu_counters_start(void)
{
    memset(&ec,0,sizeof(ec));tbx_ecu_counters_init(&ec.state);ec.persist_lock=xSemaphoreCreateMutex();
    ec.nvs_open=nvs_open("tb_ecuc",NVS_READWRITE,&ec.nvs)==ESP_OK;
    if(!ec.nvs_open)return;
    counter_checkpoint_t *p=calloc(1,sizeof(*p));if(!p)return;
    for(unsigned i=0;i<2;i++){
        size_t size=sizeof(*p);
        if(nvs_get_blob(ec.nvs,i?"b":"a",p,&size)!=ESP_OK || size!=sizeof(*p) || p->version!=1 ||
           p->crc!=tb_store_crc32(p,offsetof(counter_checkpoint_t,crc)))continue;
        if(!ec.restored || (int32_t)(p->generation-ec.generation)>0){ec.state=p->state;ec.generation=p->generation;ec.restored=true;}
    }
    free(p);
    /* A request interrupted by power loss is repeated as a new observation,
     * retaining the original boundary time; not promoted to an exact capture. */
    if(ec.state.active && ec.state.job.reason>=TBX_COUNTER_TRIP_START){
        if(ec.state.queued<4){
            memmove(ec.state.waiting+1,ec.state.waiting,ec.state.queued*sizeof(ec.state.waiting[0]));
            ec.state.waiting[0]=ec.state.job;++ec.state.queued;
        }else ++ec.state.queue_full;
    }
    for(unsigned i=0;i<ec.state.queued;i++)ec.state.waiting[i].not_before_ms=0;
    ec.state.active=ec.state.waiting_response=ec.state.boot_requested=false;
    ec.state.phase=0;ec.state.stopped_since_ms=ec.state.last_parked_ms=0;ec.state.stop_requested=false;
}
void tb_ecu_counters_tick(tbx_j1708_request_scheduler_t *scheduler,bool speed_valid,double speed,bool ignition,uint64_t now,uint32_t dwell,uint32_t interval)
{
    portENTER_CRITICAL(&counter_mux);
    tbx_ecu_counters_motion(&ec.state,speed_valid,speed,ignition,now,dwell,interval);
    tbx_ecu_counters_tick(&ec.state,scheduler,now);
    portEXIT_CRITICAL(&counter_mux);
}
void tb_ecu_counters_observe(const tbx_j1587_observation_t *o,bool matched)
{
    if(o->pid!=250 && o->pid!=236 && o->pid!=235)return;
    uint64_t utc=utc_ms();
    portENTER_CRITICAL(&counter_mux);tbx_ecu_counters_observe(&ec.state,o,matched,tb_boot_id(),utc);portEXIT_CRITICAL(&counter_mux);
}
bool tb_ecu_counters_boundary_at(bool start,const char *uid,uint64_t requested_unix_ms,uint32_t queue_age_ms)
{
    if(!uid || !*uid || strlen(uid)>64)return false;
    for(const char *p=uid;*p;p++)if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='-' || *p=='_'))return false;
    portENTER_CRITICAL(&counter_mux);
    unsigned before=ec.state.queued;
    bool ok=tbx_ecu_counters_trigger(&ec.state,start?TBX_COUNTER_TRIP_START:TBX_COUNTER_TRIP_END,uid);
    if(ok && ec.state.queued>before){
        tbx_counter_job_t *job=&ec.state.waiting[ec.state.queued-1];job->requested_unix_ms=requested_unix_ms;
        job->queue_age_ms=queue_age_ms;job->requested_boot=tb_boot_id();job->requested_uptime_ms=(uint64_t)esp_timer_get_time()/1000;
        ec.state.dirty=true;++ec.state.revision;
    }
    portEXIT_CRITICAL(&counter_mux);
    return ok && tb_ecu_counters_checkpoint(true);
}
bool tb_ecu_counters_boundary(bool start,const char *uid)
{return tb_ecu_counters_boundary_at(start,uid,utc_ms(),0);}
static cJSON *values(const tbx_ecu_counter_t *p)
{
    cJSON *a=cJSON_CreateArray();if(!a)return NULL;
    for(unsigned i=0;i<3;i++){
        cJSON *row=cJSON_CreateArray();if(!row){cJSON_Delete(a);return NULL;}
        char raw[9];snprintf(raw,sizeof(raw),"%02X%02X%02X%02X",(unsigned)(p[i].raw&255),(unsigned)((p[i].raw>>8)&255),(unsigned)((p[i].raw>>16)&255),(unsigned)(p[i].raw>>24));
        bool ok=tb_json_add_array(row,cJSON_CreateNumber(tbx_ecu_counter_pid(i))) &&
            tb_json_add_array(row,p[i].valid?cJSON_CreateNumber(p[i].mid):cJSON_CreateNull()) &&
            tb_json_add_array(row,p[i].valid?cJSON_CreateString(raw):cJSON_CreateNull()) &&
            tb_json_add_array(row,p[i].valid?cJSON_CreateNumber(p[i].raw*(i==2?0.05:0.473176473)):cJSON_CreateNull()) &&
            tb_json_add_array(row,cJSON_CreateString(i==2?"h":"L")) &&
            tb_json_add_array(row,p[i].valid?cJSON_CreateNumber(p[i].boot):cJSON_CreateNull()) &&
            tb_json_add_array(row,p[i].valid?cJSON_CreateNumber((double)p[i].uptime_ms):cJSON_CreateNull()) &&
            tb_json_add_array(row,p[i].valid && p[i].unix_ms?cJSON_CreateNumber((double)p[i].unix_ms):cJSON_CreateNull()) &&
            tb_json_add_array(row,cJSON_CreateString(p[i].valid?"ECU_COUNTER_EM_VALIDACAO":"NOVA_LEITURA_AUSENTE"));
        if(!ok){cJSON_Delete(row);cJSON_Delete(a);return NULL;}
        cJSON_AddItemToArray(a,row);
    }
    return a;
}
static bool synchronized(const tbx_ecu_counter_t *p)
{
    if(!p[0].valid || !p[1].valid || p[0].boot!=p[1].boot)return false;
    uint64_t a=p[0].uptime_ms,b=p[1].uptime_ms;
    return a>b?a-b<=5000:b-a<=5000;
}
static void deltas(cJSON *row,const tbx_ecu_counters_t *state)
{
    double delta[3]={0};unsigned status[3];
    const char *names[]={"ecu_trip_fuel_l","ecu_trip_idle_fuel_l","ecu_trip_idle_h"};
    for(unsigned i=0;i<3;i++){
        status[i]=tbx_ecu_counter_delta(&state->baseline[i],&state->captured[i],i,&delta[i]);
        if(status[i]==0)cJSON_AddNumberToObject(row,names[i],delta[i]);else cJSON_AddNullToObject(row,names[i]);
    }
    if(!status[0] && !status[1] && delta[0]>=delta[1] && synchronized(state->baseline) && synchronized(state->captured))
        cJSON_AddNumberToObject(row,"ecu_trip_nonidle_fuel_l",delta[0]-delta[1]);
    else cJSON_AddNullToObject(row,"ecu_trip_nonidle_fuel_l");
    uint64_t delay=UINT64_MAX;
    if(state->baseline[0].valid){
        if(state->start_job.requested_unix_ms && state->baseline[0].unix_ms>=state->start_job.requested_unix_ms)
            delay=state->baseline[0].unix_ms-state->start_job.requested_unix_ms;
        else if(state->start_job.queue_age_ms!=UINT32_MAX && state->baseline[0].boot==state->start_job.requested_boot && state->baseline[0].uptime_ms>=state->start_job.requested_uptime_ms)
            delay=(uint64_t)state->start_job.queue_age_ms+state->baseline[0].uptime_ms-state->start_job.requested_uptime_ms;
    }
    cJSON_AddStringToObject(row,"interval_quality",status[0]?"AUSENTE_RESET_MID_TEMPO_OU_SALTO":delay<=30000?"DELTA_ECU_EM_VALIDACAO":"CAPTURA_TARDIA_OU_TEMPO_INDETERMINADO");
    if(delay!=UINT64_MAX)cJSON_AddNumberToObject(row,"start_capture_delay_ms",(double)delay);else cJSON_AddNullToObject(row,"start_capture_delay_ms");
}
cJSON *tb_ecu_counters_json(void)
{
    portENTER_CRITICAL(&counter_mux);tbx_ecu_counters_t state=ec.state;portEXIT_CRITICAL(&counter_mux);
    cJSON *row=cJSON_CreateObject();if(!row)return NULL;
    cJSON_AddBoolToObject(row,"ok",true);cJSON_AddBoolToObject(row,"active",state.active);
    cJSON_AddBoolToObject(row,"persistent_ready",ec.nvs_open && ec.persist_lock);
    cJSON_AddBoolToObject(row,"restored",ec.restored);cJSON_AddStringToObject(row,"trip_client_uid",state.trip_uid);
    cJSON_AddNumberToObject(row,"captures",state.completed);cJSON_AddNumberToObject(row,"failed",state.failed);
    cJSON_AddNumberToObject(row,"queue_full",state.queue_full);cJSON_AddNumberToObject(row,"write_errors",ec.write_errors);
    cJSON_AddNumberToObject(row,"store_errors",ec.store_errors);cJSON_AddNumberToObject(row,"unchanged_suppressed",ec.unchanged_suppressed);
    cJSON *latest=values(state.latest);if(!latest){cJSON_Delete(row);return NULL;}
    cJSON_AddItemToObject(row,"latest",latest);
    uint64_t now=(uint64_t)esp_timer_get_time()/1000,utc=utc_ms(),age=UINT64_MAX;
    if(state.latest[0].valid){
        if(state.latest[0].boot==tb_boot_id() && now>=state.latest[0].uptime_ms)age=now-state.latest[0].uptime_ms;
        else if(state.latest[0].unix_ms && utc>=state.latest[0].unix_ms)age=utc-state.latest[0].unix_ms;
    }
    cJSON_AddBoolToObject(row,"ecu_reference_fresh",age<=360000);
    if(age!=UINT64_MAX)cJSON_AddNumberToObject(row,"ecu_reference_age_ms",(double)age);else cJSON_AddNullToObject(row,"ecu_reference_age_ms");
    if(state.trip_open){memcpy(state.captured,state.latest,sizeof(state.captured));deltas(row,&state);}
    return row;
}
void tb_ecu_counters_flush(void)
{
    portENTER_CRITICAL(&counter_mux);tbx_ecu_counters_t state=ec.state;portEXIT_CRITICAL(&counter_mux);
    if(!state.record_ready)return;
    uint8_t mask=0;bool changed=false;
    for(unsigned i=0;i<3;i++)if(state.captured[i].valid){mask|=(uint8_t)(1u<<i);if(state.captured[i].raw!=ec.recorded_raw[i])changed=true;}
    changed=changed || mask!=ec.recorded_mask;
    if(!changed && state.job.reason!=TBX_COUNTER_BOOT && state.job.reason<TBX_COUNTER_TRIP_START){
        portENTER_CRITICAL(&counter_mux);if(ec.state.sequence==state.sequence)ec.state.record_ready=false;portEXIT_CRITICAL(&counter_mux);
        ++ec.unchanged_suppressed;return;
    }
    cJSON *row=cJSON_CreateObject();if(!row)return;
    cJSON_AddNumberToObject(row,"schema_version",2);cJSON_AddStringToObject(row,"sample_kind","J1708_REQUEST_EVENT");
    cJSON_AddStringToObject(row,"event","COUNTER_CAPTURE");
    bool time_ok=tb_record_time(row,tb_boot_id(),(uint64_t)esp_timer_get_time()/1000,utc_ms());
    cJSON_AddNumberToObject(row,"capture_id",state.sequence);cJSON_AddNumberToObject(row,"reason",state.job.reason);cJSON_AddNumberToObject(row,"attempt",state.job.attempt);
    cJSON_AddStringToObject(row,"trip_client_uid",state.job.trip_uid);
    if(state.job.requested_unix_ms)cJSON_AddNumberToObject(row,"boundary_unix_ms",(double)state.job.requested_unix_ms);
    cJSON_AddBoolToObject(row,"same_instant",false);cJSON_AddNumberToObject(row,"missing_mask",state.missing);
    cJSON *captured=values(state.captured);if(!captured){cJSON_Delete(row);++ec.store_errors;return;}
    cJSON_AddItemToObject(row,"counters",captured);
    char text[TB_RECORD_MAX];bool ok=time_ok && cJSON_PrintPreallocated(row,text,sizeof(text),false) && tb_store_submit(text)==ESP_OK;
    cJSON_Delete(row);
    if(ok){
        portENTER_CRITICAL(&counter_mux);if(ec.state.sequence==state.sequence)ec.state.record_ready=false;portEXIT_CRITICAL(&counter_mux);
        ec.recorded_mask=mask;for(unsigned i=0;i<3;i++)ec.recorded_raw[i]=state.captured[i].raw;
    }else ++ec.store_errors;
}
bool tb_ecu_counters_checkpoint(bool force)
{
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;
    if(!ec.nvs_open || !ec.persist_lock || (!force && now-ec.last_persist_ms<60000))return false;
    if(xSemaphoreTake(ec.persist_lock,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    counter_checkpoint_t *p=calloc(1,sizeof(*p));if(!p){xSemaphoreGive(ec.persist_lock);return false;}
    portENTER_CRITICAL(&counter_mux);p->state=ec.state;portEXIT_CRITICAL(&counter_mux);
    if(!p->state.dirty){free(p);xSemaphoreGive(ec.persist_lock);return true;}
    ec.last_persist_ms=now;p->version=1;p->generation=ec.generation+1;p->crc=tb_store_crc32(p,offsetof(counter_checkpoint_t,crc));
    bool ok=nvs_set_blob(ec.nvs,(p->generation&1)?"b":"a",p,sizeof(*p))==ESP_OK && nvs_commit(ec.nvs)==ESP_OK;
    if(ok){
        ec.generation=p->generation;
        portENTER_CRITICAL(&counter_mux);if(ec.state.revision==p->state.revision)ec.state.dirty=false;portEXIT_CRITICAL(&counter_mux);
    }else ++ec.write_errors;
    free(p);xSemaphoreGive(ec.persist_lock);return ok;
}
