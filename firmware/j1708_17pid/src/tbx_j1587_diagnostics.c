#include "tbx_j1587_diagnostics.h"
#include <string.h>

bool tbx_j1587_dtc_parse(const uint8_t *data,size_t length,tbx_dtc_fn callback,void *context,size_t *count)
{
    if(count)*count=0;
    if((!data && length) || length>TBX_DTC_MAX_PAYLOAD)return false;
    size_t offset=0, found=0;
    while(offset<length){
        if(length-offset<2)return false;
        uint8_t a=data[offset],b=data[offset+1];
        size_t size=(b&0x80u)?3:2;
        if(length-offset<size)return false;
        tbx_dtc_t d={.identifier=(uint16_t)(a+((b&0x20u)?0:256)),
            .fmi=(uint8_t)(b&15u),.sid=(b&0x10u)!=0,.page2=(b&0x20u)==0,
            .active=(b&0x40u)==0,.has_occurrences=(b&0x80u)!=0,.raw_length=(uint8_t)size};
        memcpy(d.raw,data+offset,size);
        if(d.has_occurrences)d.occurrences=data[offset+2];
        if(callback)callback(&d,context);
        offset+=size;++found;
        if(count)*count=found;
    }
    return true;
}
typedef struct { tbx_dtc_history_t *h; uint8_t mid; tbx_dtc_time_t at; } history_context_t;
static void observe_code(const tbx_dtc_t *code,void *context)
{
    history_context_t *c=context;
    tbx_dtc_history_entry_t *entry=NULL,*available=NULL;
    for(unsigned i=0;i<TBX_DTC_HISTORY_CAPACITY;i++){
        tbx_dtc_history_entry_t *p=&c->h->entries[i];
        if(!p->used){if(!available)available=p;continue;}
        if(p->mid==c->mid && p->code.identifier==code->identifier &&
           p->code.sid==code->sid && p->code.page2==code->page2 && p->code.fmi==code->fmi){entry=p;break;}
    }
    if(!entry){
        if(!available){++c->h->capacity_rejected;return;}
        entry=available;memset(entry,0,sizeof(*entry));entry->used=true;
        entry->mid=c->mid;entry->first=c->at;entry->dirty=entry->notify=true;c->h->changed=true;
    }else if(entry->code.active!=code->active){
        ++entry->transitions;entry->dirty=entry->notify=true;c->h->changed=true;
    }else if(entry->code.has_occurrences!=code->has_occurrences ||
             (code->has_occurrences && entry->code.occurrences!=code->occurrences)){
        entry->dirty=true;c->h->changed=true; /* Counter change is not a new activation. */
    }
    entry->code=*code;entry->last=c->at;
    if(entry->observations<UINT32_MAX)++entry->observations;
}
bool tbx_dtc_history_observe(tbx_dtc_history_t *h,uint8_t mid,const uint8_t *data,size_t length,tbx_dtc_time_t at)
{
    if(!h || mid<128)return false;
    /* Validate first: do not apply a prefix from a malformed diagnostic list. */
    if(!tbx_j1587_dtc_parse(data,length,NULL,NULL,NULL)){++h->invalid_messages;return false;}
    if(!length){++h->empty_messages;return true;}
    history_context_t c={h,mid,at};
    return tbx_j1587_dtc_parse(data,length,observe_code,&c,NULL);
}
void tbx_dtc_section_expire(tbx_dtc_sections_t *s,uint64_t now)
{
    if(!s)return;
    for(unsigned i=0;i<TBX_DTC_SECTION_SESSIONS;i++){
        tbx_dtc_section_t *p=&s->sessions[i];
        if(p->used && (now<p->last_ms || now-p->last_ms>TBX_DTC_SECTION_TIMEOUT_MS)){
            p->used=false;++s->timeouts;
        }
    }
}
bool tbx_dtc_section_feed(tbx_dtc_sections_t *s,uint8_t mid,const uint8_t *data,size_t length,uint64_t now,tbx_dtc_payload_fn callback,void *context)
{
    if(!s || !data || mid<128)return false;
    tbx_dtc_section_expire(s,now);
    if(length<3 || data[0]!=194){++s->invalid;return false;}
    unsigned last=data[1]>>4,section=data[1]&15;
    tbx_dtc_section_t *p=NULL,*available=NULL;
    for(unsigned i=0;i<TBX_DTC_SECTION_SESSIONS;i++){
        if(s->sessions[i].used && s->sessions[i].mid==mid)p=&s->sessions[i];
        else if(!s->sessions[i].used && !available)available=&s->sessions[i];
    }
    if(!last || section>last){if(p)p->used=false;++s->invalid;return false;}
    size_t offset=2;
    if(!section){
        if(length<4 || data[2]<=17 || data[2]>TBX_DTC_MAX_PAYLOAD){if(p)p->used=false;++s->invalid;return false;}
        if(!p)p=available;
        if(!p){++s->capacity_rejected;return false;}
        *p=(tbx_dtc_section_t){.mid=mid,.used=true,.last_section=(uint8_t)last,
            .total=data[2],.started_ms=now};offset=3;
    }else if(!p || p->expected!=section || p->last_section!=last){if(p)p->used=false;++s->invalid;return false;}
    if(length-offset>(size_t)(p->total-p->length)){p->used=false;++s->invalid;return false;}
    memcpy(p->bytes+p->length,data+offset,length-offset);
    p->length+=(uint16_t)(length-offset);p->last_ms=now;p->expected=(uint8_t)(section+1);
    if(section==last){
        p->used=false;
        if(p->length!=p->total){++s->invalid;return false;}
        ++s->completed;
        if(callback)callback(mid,p->bytes,p->length,p->started_ms,now,context);
    }else if(p->length>=p->total){p->used=false;++s->invalid;return false;}
    return true;
}
