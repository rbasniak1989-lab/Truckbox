#ifndef TB_DIAGNOSTIC_HOST_TEST
#include "tb.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#else
#include "tests/diagnostics_host_stubs.h"
#endif
#include "diagnostics.h"
#include "record_time.h"
#include "tbx_j1587_diagnostics.h"
#include "store_internal.h"
#include "../tb_base/include/tb_json.h"
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

typedef struct {uint64_t utc;tb_diagnostic_context_t nearby;}diagnostic_payload_context_t;
typedef struct {
    uint32_t version, generation;
    tbx_dtc_history_t history;
    tb_diagnostic_context_t contexts[TBX_DTC_HISTORY_CAPACITY];
    uint32_t crc;
} diagnostic_checkpoint_t;
static struct {
    tbx_dtc_history_t history;
    tb_diagnostic_context_t contexts[TBX_DTC_HISTORY_CAPACITY];
    tbx_dtc_sections_t sections;
    uint64_t last_persist_ms;
    uint32_t generation, persisted_generation, write_errors, store_errors;
    struct { uint8_t mid; bool used; uint32_t crc; uint64_t at_ms; } raw_seen[16];
    nvs_handle_t nvs;
    bool nvs_open;
} d;
static portMUX_TYPE diagnostic_mux=portMUX_INITIALIZER_UNLOCKED;
static bool checkpoint_valid(const tbx_dtc_history_t *history)
{
    for(unsigned i=0;i<TBX_DTC_HISTORY_CAPACITY;i++){
        const tbx_dtc_history_entry_t *entry=&history->entries[i];
        if(entry->used && (entry->mid<128 || entry->code.identifier>511 || entry->code.fmi>15 ||
           entry->code.raw_length!=(entry->code.has_occurrences?3:2)))return false;
    }
    return true;
}

static void hex_bytes(char *out,const uint8_t *bytes,size_t n)
{
    static const char h[]="0123456789ABCDEF";
    for(size_t i=0;i<n;i++){out[2*i]=h[bytes[i]>>4];out[2*i+1]=h[bytes[i]&15];}
    out[2*n]=0;
}
static bool submit(cJSON *row)
{
    char text[TB_RECORD_MAX];
    const cJSON *kind=cJSON_GetObjectItemCaseSensitive(row,"sample_kind");
    const cJSON *event=cJSON_GetObjectItemCaseSensitive(row,"event");
    bool metadata=row && cJSON_IsString(kind) && !strcmp(kind->valuestring,"J1708_REQUEST_EVENT") && cJSON_IsString(event) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"mid")) && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"boot_id")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"uptime_ms")) && cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(row,"time_valid")) &&
        cJSON_IsString(cJSON_GetObjectItemCaseSensitive(row,"time_source"));
    if(metadata && (!strcmp(event->valuestring,"DIAGNOSTIC_RAW") || !strcmp(event->valuestring,"DIAGNOSTIC_INVALID")))
        metadata=cJSON_IsString(cJSON_GetObjectItemCaseSensitive(row,"parameter_hex")) && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"offset")) &&
            cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"total_bytes")) && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"capture_ms"));
    bool ok=metadata && cJSON_PrintPreallocated(row,text,sizeof(text),false) && tb_store_submit(text)==ESP_OK;
    if(!ok)__atomic_fetch_add(&d.store_errors,1,__ATOMIC_RELAXED);
    cJSON_Delete(row);return ok;
}
static void identity(cJSON *row,const char *event,uint64_t uptime,uint32_t boot,uint64_t utc)
{
    cJSON_AddNumberToObject(row,"schema_version",2);
    cJSON_AddStringToObject(row,"sample_kind","J1708_REQUEST_EVENT");
    cJSON_AddStringToObject(row,"event",event);
    (void)tb_record_time(row,boot,uptime,utc);
    cJSON_AddStringToObject(row,"source","j1708");
}
static void payload(uint8_t mid,const uint8_t *bytes,size_t n,uint64_t started,uint64_t ended,void *context)
{
    const diagnostic_payload_context_t *metadata=context;
    uint64_t utc=metadata?metadata->utc:0;
    tbx_dtc_time_t at={tb_boot_id(),ended,utc};
    portENTER_CRITICAL(&diagnostic_mux);
    bool ok=tbx_dtc_history_observe(&d.history,mid,bytes,n,at);
    if(d.history.changed){++d.generation;d.history.changed=false;}
    for(unsigned i=0;i<TBX_DTC_HISTORY_CAPACITY;i++)if(metadata && d.history.entries[i].used && d.history.entries[i].mid==mid && d.history.entries[i].last.uptime_ms==ended)d.contexts[i]=metadata->nearby;
    portEXIT_CRITICAL(&diagnostic_mux);
    uint32_t crc=tb_store_crc32(bytes,n);unsigned slot=0;
    for(unsigned i=0;i<16;i++){
        if(d.raw_seen[i].used && d.raw_seen[i].mid==mid){slot=i;break;}
        if(!d.raw_seen[i].used || d.raw_seen[i].at_ms<d.raw_seen[slot].at_ms)slot=i;
    }
    bool changed=!d.raw_seen[slot].used || d.raw_seen[slot].mid!=mid || d.raw_seen[slot].crc!=crc;
    if(changed){
        bool stored=true;
        /* Only PID194 changes, not continuous raw traffic. This also retains
         * every code when the bounded local history is full, and all239 bytes
         * of a multi-section result. Chunks share boot/MID/capture_ms. */
        for(size_t offset=0;offset<n;offset+=32){
            cJSON *row=cJSON_CreateObject();identity(row,ok?"DIAGNOSTIC_RAW":"DIAGNOSTIC_INVALID",ended,tb_boot_id(),utc);
            cJSON_AddNumberToObject(row,"mid",mid);cJSON_AddNumberToObject(row,"pid",194);
            cJSON_AddNumberToObject(row,"capture_ms",(double)ended);
            cJSON_AddNumberToObject(row,"started_ms",(double)started);
            cJSON_AddNumberToObject(row,"total_bytes",(double)n);
            char raw[65];size_t len=n-offset;if(len>32)len=32;hex_bytes(raw,bytes+offset,len);
            cJSON_AddStringToObject(row,"parameter_hex",raw);cJSON_AddNumberToObject(row,"offset",(double)offset);
            stored=submit(row) && stored;
        }
        if(stored){d.raw_seen[slot].used=true;d.raw_seen[slot].mid=mid;d.raw_seen[slot].crc=crc;d.raw_seen[slot].at_ms=ended;}
    }
    (void)started;
}
void tb_diagnostics_start(void)
{
    memset(&d,0,sizeof(d));
    d.nvs_open=nvs_open("tb_diag",NVS_READWRITE,&d.nvs)==ESP_OK;
    if(!d.nvs_open)return;
    diagnostic_checkpoint_t *checkpoint=calloc(1,sizeof(*checkpoint));
    if(!checkpoint)return;
    for(unsigned slot=0;slot<2;slot++){
        size_t size=sizeof(*checkpoint);
        if(nvs_get_blob(d.nvs,slot?"b":"a",checkpoint,&size)!=ESP_OK || size!=sizeof(*checkpoint) ||
           checkpoint->version!=1 || checkpoint->crc!=tb_store_crc32(checkpoint,offsetof(diagnostic_checkpoint_t,crc)) ||
           !checkpoint_valid(&checkpoint->history))continue;
        if(!d.generation || (int32_t)(checkpoint->generation-d.generation)>0){
            d.history=checkpoint->history;memcpy(d.contexts,checkpoint->contexts,sizeof(d.contexts));d.generation=checkpoint->generation;
        }
    }
    d.persisted_generation=d.generation;
    /* Replay after restart is idempotent by code identity; not a new alert. */
    for(unsigned i=0;i<TBX_DTC_HISTORY_CAPACITY;i++)if(d.history.entries[i].used){
        d.history.entries[i].dirty=true;d.history.entries[i].notify=false;
    }
    free(checkpoint);
}
void tb_diagnostics_observe(const tbx_j1587_observation_t *o,uint64_t unix_ms,const tb_diagnostic_context_t *nearby)
{
    diagnostic_payload_context_t metadata={.utc=unix_ms};
    if(nearby)metadata.nearby=*nearby;
    if(o->pid==194)payload(o->mid,o->raw,o->raw_len,o->uptime_ms,o->uptime_ms,&metadata);
    else if(o->pid==192)(void)tbx_dtc_section_feed(&d.sections,o->mid,o->raw,o->raw_len,o->uptime_ms,payload,&metadata);
}
static cJSON *time_json(tbx_dtc_time_t at)
{
    cJSON *a=cJSON_CreateArray();if(!a)return NULL;
    bool ok=tb_json_add_array(a,cJSON_CreateNumber(at.boot)) &&
        tb_json_add_array(a,cJSON_CreateNumber((double)at.uptime_ms)) &&
        tb_json_add_array(a,at.unix_ms?cJSON_CreateNumber((double)at.unix_ms):cJSON_CreateNull());
    if(!ok){cJSON_Delete(a);return NULL;}return a;
}
static cJSON *entry_json(const tbx_dtc_history_entry_t *e)
{
    cJSON *row=cJSON_CreateObject();if(!row)return NULL;
    char bytes[7];hex_bytes(bytes,e->code.raw,e->code.raw_length);
    bool ok=cJSON_AddNumberToObject(row,"mid",e->mid) &&
        cJSON_AddStringToObject(row,"identifier_type",e->code.sid?"SID":"PID") &&
        cJSON_AddNumberToObject(row,"identifier",e->code.identifier) && cJSON_AddNumberToObject(row,"fmi",e->code.fmi) &&
        cJSON_AddBoolToObject(row,"page2",e->code.page2) && cJSON_AddStringToObject(row,"state",e->code.active?"ACTIVE":"INACTIVE") &&
        (e->code.has_occurrences?cJSON_AddNumberToObject(row,"occurrences",e->code.occurrences):cJSON_AddNullToObject(row,"occurrences")) &&
        cJSON_AddStringToObject(row,"parameter_hex",bytes) && tb_json_add_object(row,"first",time_json(e->first)) &&
        tb_json_add_object(row,"last",time_json(e->last)) && cJSON_AddNumberToObject(row,"observations",e->observations) &&
        cJSON_AddNumberToObject(row,"transitions",e->transitions) && cJSON_AddBoolToObject(row,"notify",e->notify) &&
        cJSON_AddStringToObject(row,"quality","SAE_PARSED_OEM_UNCONFIRMED") && cJSON_AddBoolToObject(row,"resolved_by_absence",false);
    if(!ok){cJSON_Delete(row);return NULL;}return row;
}
cJSON *tb_diagnostics_json(unsigned offset,unsigned limit)
{
    if(offset>TBX_DTC_HISTORY_CAPACITY)offset=TBX_DTC_HISTORY_CAPACITY;
    if(!limit || limit>8)limit=8;
    cJSON *row=cJSON_CreateObject();if(!row)return NULL;
    cJSON_AddBoolToObject(row,"ok",true);cJSON_AddStringToObject(row,"device_uid",tb_device_uid());cJSON_AddStringToObject(row,"catalog_version","volvo-fh-e5-0.1.0");
    cJSON_AddNumberToObject(row,"offset",offset);cJSON_AddNumberToObject(row,"capacity",TBX_DTC_HISTORY_CAPACITY);
    cJSON_AddNumberToObject(row,"overflow",d.history.capacity_rejected);
    cJSON_AddNumberToObject(row,"invalid_messages",d.history.invalid_messages);
    cJSON_AddNumberToObject(row,"empty_messages",d.history.empty_messages);
    cJSON_AddBoolToObject(row,"persistent_ready",d.nvs_open);
    cJSON_AddNumberToObject(row,"multisection_completed",d.sections.completed);
    cJSON_AddNumberToObject(row,"multisection_errors",d.sections.invalid+d.sections.timeouts);
    cJSON_AddNumberToObject(row,"write_errors",d.write_errors);cJSON_AddNumberToObject(row,"store_errors",d.store_errors);
    cJSON *entries=cJSON_AddArrayToObject(row,"faults");
    unsigned next=offset;
    for(;next<TBX_DTC_HISTORY_CAPACITY && cJSON_GetArraySize(entries)<(int)limit;next++){
        portENTER_CRITICAL(&diagnostic_mux);tbx_dtc_history_entry_t e=d.history.entries[next];portEXIT_CRITICAL(&diagnostic_mux);
        if(e.used){cJSON *entry=entry_json(&e);if(!tb_json_add_array(entries,entry)){cJSON_Delete(row);return NULL;}}
    }
    cJSON_AddNumberToObject(row,"next_offset",next);return row;
}
void tb_diagnostics_flush(void)
{
    for(unsigned i=0,count=0;i<TBX_DTC_HISTORY_CAPACITY && count<4;i++){
        portENTER_CRITICAL(&diagnostic_mux);tbx_dtc_history_entry_t e=d.history.entries[i];portEXIT_CRITICAL(&diagnostic_mux);
        if(!e.used || !e.dirty)continue;
        cJSON *row=entry_json(&e);if(!row)return;
        identity(row,"DIAGNOSTIC_DTC",e.last.uptime_ms,e.last.boot,e.last.unix_ms);
        cJSON_DeleteItemFromObjectCaseSensitive(row,"observations");
        cJSON_DeleteItemFromObjectCaseSensitive(row,"resolved_by_absence");
        portENTER_CRITICAL(&diagnostic_mux);tb_diagnostic_context_t near=d.contexts[i];portEXIT_CRITICAL(&diagnostic_mux);
        cJSON *context=cJSON_CreateArray();
        bool complete=context && tb_json_add_array(context,(near.valid&1)?cJSON_CreateNumber(near.speed):cJSON_CreateNull()) &&
            tb_json_add_array(context,(near.valid&2)?cJSON_CreateNumber(near.rpm):cJSON_CreateNull()) &&
            tb_json_add_array(context,(near.valid&4)?cJSON_CreateNumber(near.pedal):cJSON_CreateNull()) &&
            tb_json_add_array(context,(near.valid&8)?cJSON_CreateString(near.gear):cJSON_CreateNull()) &&
            tb_json_add_array(context,(near.valid&16)?cJSON_CreateString(near.selected):cJSON_CreateNull());
        if(!complete){cJSON_Delete(context);cJSON_Delete(row);__atomic_fetch_add(&d.store_errors,1,__ATOMIC_RELAXED);return;}
        if(!tb_json_add_object(row,"context",context)){cJSON_Delete(row);__atomic_fetch_add(&d.store_errors,1,__ATOMIC_RELAXED);return;}
        cJSON_AddNumberToObject(row,"pid",194);
        if(submit(row)){
            portENTER_CRITICAL(&diagnostic_mux);
            if(d.history.entries[i].observations==e.observations){d.history.entries[i].dirty=false;d.history.entries[i].notify=false;}
            portEXIT_CRITICAL(&diagnostic_mux);
        }
        ++count;
    }
}
void tb_diagnostics_checkpoint(void)
{
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;
    if(!d.nvs_open || d.generation==d.persisted_generation || now-d.last_persist_ms<60000)return;
    d.last_persist_ms=now;
    diagnostic_checkpoint_t *p=calloc(1,sizeof(*p));if(!p)return;
    portENTER_CRITICAL(&diagnostic_mux);p->version=1;p->generation=d.generation;p->history=d.history;memcpy(p->contexts,d.contexts,sizeof(p->contexts));portEXIT_CRITICAL(&diagnostic_mux);
    p->crc=tb_store_crc32(p,offsetof(diagnostic_checkpoint_t,crc));
    if(nvs_set_blob(d.nvs,(p->generation&1)?"b":"a",p,sizeof(*p))==ESP_OK && nvs_commit(d.nvs)==ESP_OK)d.persisted_generation=p->generation;
    else ++d.write_errors;
    free(p);
}
