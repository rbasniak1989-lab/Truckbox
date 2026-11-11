#include "tbx_ecu_counters.h"
#include <math.h>
#include <string.h>

uint16_t tbx_ecu_counter_pid(unsigned i){static const uint16_t p[]={250,236,235};return i<3?p[i]:0;}
void tbx_ecu_counters_init(tbx_ecu_counters_t *c){memset(c,0,sizeof(*c));}
bool tbx_ecu_counters_trigger(tbx_ecu_counters_t *c,tbx_counter_reason_t reason,const char *uid)
{
    if(!c || reason<TBX_COUNTER_BOOT || reason>TBX_COUNTER_TRIP_END || (uid && strlen(uid)>64))return false;
    if(reason==TBX_COUNTER_TRIP_START && c->trip_open && uid && !strcmp(uid,c->trip_uid))return true;
    if(reason==TBX_COUNTER_TRIP_END && !c->trip_open && c->baseline_generation && uid && !strcmp(uid,c->trip_uid))return true;
    if(c->active && c->job.reason==reason && !strcmp(c->job.trip_uid,uid?uid:""))return true;
    for(unsigned i=0;i<c->queued;i++)if(c->waiting[i].reason==reason && !strcmp(c->waiting[i].trip_uid,uid?uid:""))return true;
    /* Repeated stop notifications coalesce; distinct trip boundaries do not. */
    if(reason<=TBX_COUNTER_PARKED){
        if(c->active && c->job.reason==reason)return true;
        for(unsigned i=0;i<c->queued;i++)if(c->waiting[i].reason==reason)return true;
    }
    if(c->queued>=4){++c->queue_full;return false;}
    tbx_counter_job_t *j=&c->waiting[c->queued++];memset(j,0,sizeof(*j));j->reason=reason;
    if(uid)memcpy(j->trip_uid,uid,strlen(uid)+1);
    return true;
}
void tbx_ecu_counters_motion(tbx_ecu_counters_t *c,bool valid,double speed,bool ignition,uint64_t now,uint32_t dwell_s,uint32_t parked_s)
{
    if(!valid || !isfinite(speed) || speed<0 || !ignition){c->stopped_since_ms=0;c->previous_speed_valid=false;return;}
    if(speed<=0.9){
        if(!c->stopped_since_ms)c->stopped_since_ms=now;
        if(!c->stop_requested && now>=c->stopped_since_ms && now-c->stopped_since_ms>=(uint64_t)dwell_s*1000){
            c->stop_requested=tbx_ecu_counters_trigger(c,TBX_COUNTER_STOP,c->trip_open?c->trip_uid:NULL);c->last_parked_ms=now;
        }else if(c->stop_requested && now>=c->last_parked_ms && now-c->last_parked_ms>=(uint64_t)parked_s*1000){
            if(tbx_ecu_counters_trigger(c,TBX_COUNTER_PARKED,c->trip_open?c->trip_uid:NULL))c->last_parked_ms=now;
        }
    }else{c->stopped_since_ms=0;c->stop_requested=false;}
    c->previous_speed=speed;c->previous_speed_valid=true;
}
static void finish_capture(tbx_ecu_counters_t *c,uint64_t now)
{
    c->active=false;++c->completed;
    if(c->missing)++c->failed;
    if(c->job.reason==TBX_COUNTER_TRIP_START){
        if(!c->job.attempt){
            memcpy(c->baseline,c->captured,sizeof(c->baseline));memcpy(c->trip_uid,c->job.trip_uid,sizeof(c->trip_uid));c->start_job=c->job;c->trip_open=true;
        }else if(!strcmp(c->trip_uid,c->job.trip_uid)){
            for(unsigned i=0;i<3;i++)if(!c->baseline[i].valid && c->captured[i].valid)c->baseline[i]=c->captured[i];
        }
        ++c->baseline_generation;++c->revision;c->dirty=true;
    }else if(c->job.reason==TBX_COUNTER_TRIP_END){
        if(!strcmp(c->trip_uid,c->job.trip_uid))c->trip_open=false;
        c->dirty=true;++c->revision;
    }
    if(c->missing && c->job.attempt<2 && c->queued<4){
        tbx_counter_job_t retry=c->job;++retry.attempt;retry.not_before_ms=now+60000;
        c->waiting[c->queued++]=retry;c->dirty=true;++c->revision;
    }
    c->record_ready=true;
}
void tbx_ecu_counters_tick(tbx_ecu_counters_t *c,tbx_j1708_request_scheduler_t *s,uint64_t now)
{
    if(c->active && c->phase>=3){finish_capture(c,now);return;}
    if(!s->armed || !s->priority_polling){
        if(c->active && c->waiting_response && now>=c->phase_started_ms && now-c->phase_started_ms>30000){
            for(unsigned i=c->phase;i<3;i++)c->missing|=(uint8_t)(1u<<i);
            c->phase=3;finish_capture(c,now);
        }
        return;
    }
    if(!c->boot_requested)c->boot_requested=tbx_ecu_counters_trigger(c,TBX_COUNTER_BOOT,NULL);
    if(!c->active){
        if(!c->queued || c->record_ready)return;
        unsigned index=0;while(index<c->queued && c->waiting[index].not_before_ms>now)++index;
        if(index==c->queued)return;
        c->job=c->waiting[index];--c->queued;
        memmove(c->waiting+index,c->waiting+index+1,(c->queued-index)*sizeof(c->waiting[0]));
        c->active=true;c->phase=0;c->missing=0;c->waiting_response=false;++c->sequence;
        memset(c->captured,0,sizeof(c->captured));
    }
    uint16_t pid=tbx_ecu_counter_pid(c->phase);tbx_j1708_request_stat_t *p=NULL;
    for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)if(s->pids[i].pid==pid){p=&s->pids[i];break;}
    if(!p)return;
    if(!c->waiting_response){
        c->phase_sent=p->sent;c->phase_timeouts=p->timed_out;c->phase_started_ms=now;c->waiting_response=true;
        (void)tbx_j1708_request_expedite(s,pid,(uint32_t)now);
    }else if(p->timed_out-c->phase_timeouts>=2 || (now>=c->phase_started_ms && now-c->phase_started_ms>30000)){
        c->missing|=(uint8_t)(1u<<c->phase);++c->phase;c->waiting_response=false;
    }
}
void tbx_ecu_counters_observe(tbx_ecu_counters_t *c,const tbx_j1587_observation_t *o,bool matched,uint32_t boot,uint64_t utc)
{
    if(!c || !o || o->mid!=128 || o->raw_len!=4)return;
    unsigned i;for(i=0;i<3;i++)if(tbx_ecu_counter_pid(i)==o->pid)break;if(i==3)return;
    uint32_t raw=o->requested.raw_unsigned;
    if(o->quality!=TBX_J1587_OBS_EM_VALIDACAO || !o->has_numeric || raw==UINT32_MAX){
        if(c->active && c->phase==i && c->waiting_response && matched){c->missing|=(uint8_t)(1u<<i);++c->phase;c->waiting_response=false;}
        return;
    }
    tbx_ecu_counter_t next={raw,boot,o->uptime_ms,utc,o->mid,true};
    if(!c->latest[i].valid || c->latest[i].raw!=raw || c->latest[i].mid!=o->mid){c->dirty=true;++c->revision;}
    c->latest[i]=next;
    if(c->active && c->phase==i && c->waiting_response && matched && o->uptime_ms>=c->phase_started_ms){
        c->captured[i]=next;++c->phase;c->waiting_response=false;
    }
}
unsigned tbx_ecu_counter_delta(const tbx_ecu_counter_t *a,const tbx_ecu_counter_t *b,unsigned i,double *delta)
{
    if(!a || !b || !a->valid || !b->valid || i>2)return 1;
    if(a->mid!=b->mid || b->raw<a->raw)return 2;
    uint64_t elapsed;
    if(a->unix_ms && b->unix_ms && b->unix_ms>=a->unix_ms)elapsed=b->unix_ms-a->unix_ms;
    else if(a->boot==b->boot && b->uptime_ms>=a->uptime_ms)elapsed=b->uptime_ms-a->uptime_ms;
    else return 3;
    double value=(b->raw-a->raw)*(i==2?0.05:0.473176473);
    double limit=(i==2?1.0:1000.0)*(double)elapsed/3600000.0+(i==2?0.1:0.946352946);
    if(!isfinite(value) || value>limit)return 3;
    if(delta)*delta=value;
    return 0;
}
