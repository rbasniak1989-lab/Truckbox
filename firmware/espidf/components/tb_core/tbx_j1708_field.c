#include "tbx_j1708_active.h"
#include <string.h>

static void emit(tbx_j1708_active_t *c,const char *name,uint16_t pid,const char *detail)
{
    if(c->ops.event)c->ops.event(name,pid,detail,c->context);
}
const char *tbx_j1708_field_phase_name(tbx_j1708_field_phase_t phase)
{
    static const char *const names[]={"IDLE","PASSIVE_BEFORE","QUIET_CONTROL","REQUEST","SPACING",
        "PASSIVE_AFTER","SATISFACTORY_INDIRECT","INCONCLUSIVE","FAILED","OPERATOR_CONFIRMED_INDIRECT"};
    return (unsigned)phase<sizeof(names)/sizeof(*names)?names[phase]:"FAILED";
}
bool tbx_j1708_field_running(const tbx_j1708_field_t *f)
{
    return f && f->phase>=TBX_FIELD_BEFORE && f->phase<=TBX_FIELD_AFTER;
}
void tbx_j1708_field_fail(tbx_j1708_active_t *c,const char *reason)
{
    if(!c || (!tbx_j1708_field_running(&c->field) && c->field.phase!=TBX_FIELD_SATISFACTORY && !c->field.confirmed))return;
    c->field.phase=TBX_FIELD_FAILED;c->field.confirmed=false;c->field.reason=reason;
    emit(c,"FIELD_FAILED",0,reason);
    tbx_j1708_active_stop(c,reason);
}
bool tbx_j1708_field_start(tbx_j1708_active_t *c,bool authorized,bool stationary,uint64_t now)
{
    if(!c || !c->scheduler || !c->configured || c->scheduler->armed || c->tx_active ||
       c->scheduler->safety_stopped || !authorized || !stationary)return false;
    for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++)if(!c->scheduler->pids[i].enabled)return false;
    memset(&c->field,0,sizeof(c->field));
    c->field.phase=TBX_FIELD_BEFORE;c->field.started_us=now;
    c->field.phase_until_us=now+TBX_J1708_FIELD_WINDOW_US;
    c->field.watch_at_us=c->field.last_frame_us=now;c->field.reason="MEASURING_PASSIVE_BEFORE";
    c->physical_validated=false;c->config.single_shot=false;c->config.sequential_scan=false;
    c->scheduler->periodic=false;
    c->trial_echo_base=c->echo_ok;c->trial_answer_base=c->scheduler->answered_total;
    if(!tbx_j1708_active_arm(c,true,true,now)){c->field.phase=TBX_FIELD_FAILED;return false;}
    c->lease_deadline_us=now+TBX_J1708_FIELD_LEASE_US;
    tbx_j1708_request_set_spacing(c->scheduler,TBX_J1708_WORK_MIN_SPACING_MS);
    emit(c,"FIELD_STARTED",0,"PARKED_AUTHORIZED_OPERATIONAL_INDIRECT_NOT_EXTERNAL_PROOF");
    return true;
}
static void control_start(tbx_j1708_active_t *c,uint64_t now)
{
    tbx_j1708_field_t *f=&c->field;
    unsigned round=f->completed/TBX_J1708_REQUEST_COUNT,position=f->completed%TBX_J1708_REQUEST_COUNT;
    /* A different PID order and spacing in each round avoids a fixed phase
     * relationship with periodic broadcasts; this is still indirect evidence. */
    f->index=(uint8_t)((position*7u+round*5u)%TBX_J1708_REQUEST_COUNT);
    f->phase=TBX_FIELD_CONTROL;f->control_seen=f->echo_seen=false;
    f->phase_until_us=now+(uint64_t)c->scheduler->timeout_ms*1000u;
    emit(c,"FIELD_CONTROL",c->scheduler->pids[f->index].pid,"MATCHED_DURATION_WITHOUT_TRANSMISSION");
}
void tbx_j1708_field_note_frame(tbx_j1708_active_t *c,uint8_t mid,bool valid,uint64_t now)
{
    tbx_j1708_field_note_frame_kind(c,mid,valid?TBX_RX_VALID:TBX_RX_CHECKSUM,now);
}
void tbx_j1708_field_note_frame_kind(tbx_j1708_active_t *c,uint8_t mid,tbx_rx_kind_t kind,uint64_t now)
{
    if(!c)return;
    c->now_us=now;
    tbx_j1708_rx_note(c,kind,now);
    if(!tbx_j1708_field_running(&c->field) && c->field.phase!=TBX_FIELD_SATISFACTORY && !c->field.confirmed)return;
    tbx_j1708_field_t *f=&c->field;
    if(kind==TBX_RX_VALID){
        ++f->frames;f->last_frame_us=now;
        if(mid==c->config.local_mid){c->scheduler->safety_stopped=true;tbx_j1708_field_fail(c,"LOCAL_MID_CONFLICT");}
    }else ++f->bad_frames;
}
void tbx_j1708_field_note_parameter(tbx_j1708_active_t *c,uint8_t mid,uint16_t pid,uint64_t now)
{
    if(!c || !tbx_j1708_field_running(&c->field))return;
    tbx_j1708_field_t *f=&c->field;
    for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++){
        if(c->scheduler->pids[i].pid!=pid || c->config.target_mid[i]!=mid)continue;
        tbx_j1708_field_pid_t *p=&f->pids[i];
        if(f->phase==TBX_FIELD_BEFORE && p->before<UINT16_MAX)++p->before;
        if(f->phase==TBX_FIELD_AFTER && !f->result_cursor && p->after<UINT16_MAX)++p->after;
        if(f->phase==TBX_FIELD_CONTROL && f->index==i && now<f->phase_until_us)f->control_seen=true;
    }
}
void tbx_j1708_field_sent(tbx_j1708_active_t *c)
{
    if(c->field.phase!=TBX_FIELD_REQUEST)return;
    ++c->field.pids[c->field.index].sent;c->field.echo_seen=false;
}
void tbx_j1708_field_echo(tbx_j1708_active_t *c)
{
    if(c->field.phase!=TBX_FIELD_REQUEST)return;
    ++c->field.pids[c->field.index].echoes;c->field.echo_seen=true;
}
void tbx_j1708_field_reply(tbx_j1708_active_t *c,uint64_t now)
{
    tbx_j1708_field_t *f=&c->field;
    if(f->phase!=TBX_FIELD_REQUEST)return;
    if(!f->echo_seen || now<c->tx_started_us || now-c->tx_started_us>=(uint64_t)c->scheduler->timeout_ms*1000u){
        tbx_j1708_field_fail(c,"FIELD_INVALID_ECHO_OR_LATENCY");return;
    }
    tbx_j1708_field_pid_t *p=&f->pids[f->index];
    uint32_t latency=(uint32_t)(now-c->tx_started_us);
    if(!p->replies || latency<p->latency_min_us)p->latency_min_us=latency;
    if(latency>p->latency_max_us)p->latency_max_us=latency;
    p->latency_sum_us+=latency;++p->replies;++f->completed;
    if(f->completed==TBX_J1708_FIELD_ROUNDS*TBX_J1708_REQUEST_COUNT){
        f->phase=TBX_FIELD_AFTER;f->phase_until_us=now+TBX_J1708_FIELD_WINDOW_US;
        f->after_start_frames=f->frames;f->after_start_errors=f->bad_frames;emit(c,"FIELD_AFTER",0,"MEASURING_PASSIVE_AFTER_WITHOUT_TRANSMISSION");
    }else{
        static const uint32_t gaps[]={1500000,2200000,2900000};
        f->phase=TBX_FIELD_GAP;f->phase_until_us=now+gaps[(f->completed+f->completed/17u)%3u];
    }
}
static bool healthy(tbx_j1708_active_t *c,uint64_t now)
{
    tbx_j1708_field_t *f=&c->field;
    if(now<f->last_frame_us || now-f->last_frame_us>=UINT64_C(10000000)){
        tbx_j1708_field_fail(c,"PASSIVE_BUS_SILENCE");return false;
    }
    if(__atomic_load_n(&f->audit_lost,__ATOMIC_RELAXED)){
        tbx_j1708_field_fail(c,"FIELD_AUDIT_RECORD_LOST");return false;
    }
    /* Rolling RX quality is evaluated independently in rx_guard. The parked
     * validation baseline below remains historical context, never a road-rate veto. */
    return true;
}
bool tbx_j1708_field_tick(tbx_j1708_active_t *c,uint64_t now)
{
    tbx_j1708_field_t *f=&c->field;
    if(!tbx_j1708_field_running(f)){
        if(f->phase==TBX_FIELD_SATISFACTORY || (f->confirmed && c->work_mode))(void)healthy(c,now);
        return true;
    }
    if(!healthy(c,now))return false;
    if(now<f->phase_until_us && f->phase!=TBX_FIELD_REQUEST)return false;
    switch(f->phase){
        case TBX_FIELD_BEFORE:
            f->before_frames=f->frames;f->before_errors=f->bad_frames;f->before_duration_us=(uint32_t)(now-f->started_us);
            if(f->before_frames<60u || (f->before_errors>3u && (uint64_t)f->before_errors*100u>f->before_frames)){
                tbx_j1708_field_fail(c,"PASSIVE_BASELINE_INSUFFICIENT_OR_NOISY");return false;
            }
            control_start(c,now);return false;
        case TBX_FIELD_GAP:control_start(c,now);return false;
        case TBX_FIELD_CONTROL:
            ++f->pids[f->index].controls;
            if(f->control_seen)++f->pids[f->index].control_hits;
            f->phase=TBX_FIELD_REQUEST;c->scheduler->cursor=f->index;
            return true;
        case TBX_FIELD_REQUEST:return true;
        case TBX_FIELD_AFTER:{
            if(!f->result_cursor){
                f->after_frames=f->frames-f->after_start_frames;f->after_errors=f->bad_frames-f->after_start_errors;
                f->after_duration_us=(uint32_t)(now-(f->phase_until_us-TBX_J1708_FIELD_WINDOW_US));
            }
            uint64_t before_rate=(uint64_t)f->before_frames*f->after_duration_us;
            uint64_t after_rate=(uint64_t)f->after_frames*f->before_duration_us;
            if(after_rate*2u<before_rate || after_rate>before_rate*2u){
                tbx_j1708_field_fail(c,"PASSIVE_BEFORE_AFTER_RATE_CHANGED");return false;
            }
            if(f->result_cursor<TBX_J1708_REQUEST_COUNT){
                emit(c,"FIELD_PID_RESULT",c->scheduler->pids[f->result_cursor++].pid,"MEASURED_BEFORE_CONTROL_REPLY_AFTER_AND_LATENCY");
                return false; /* One result/tick, never overflow the bounded audit queue. */
            }
            f->contrasting_pids=0;
            for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++){
                tbx_j1708_field_pid_t *p=&f->pids[i];
                if(p->sent!=3 || p->echoes!=3 || p->replies!=3 || p->controls!=3){tbx_j1708_field_fail(c,"FIELD_INCOMPLETE_PID_COVERAGE");return false;}
                if(p->control_hits<=1)++f->contrasting_pids;
            }
            bool satisfactory=f->contrasting_pids>=TBX_J1708_FIELD_MIN_CONTRAST;
            f->phase=satisfactory?TBX_FIELD_SATISFACTORY:TBX_FIELD_INCONCLUSIVE;
            f->reason=satisfactory?"OPERATIONAL_INDIRECT_OPERATOR_CONFIRMATION_REQUIRED":"RESPONSES_NOT_DISTINGUISHABLE_FROM_PASSIVE";
            f->completed_us=now;
            emit(c,satisfactory?"FIELD_SATISFACTORY":"FIELD_INCONCLUSIVE",0,f->reason);
            tbx_j1708_active_stop(c,"FIELD_TEST_COMPLETE");return false;
        }
        default:return false;
    }
}
bool tbx_j1708_field_confirm(tbx_j1708_active_t *c,uint64_t session_started,bool acknowledged,uint64_t now)
{
    if(!c || !acknowledged || c->field.phase!=TBX_FIELD_SATISFACTORY || c->field.started_us!=session_started ||
       c->scheduler->armed || c->tx_active || c->scheduler->safety_stopped || !c->field.completed_us ||
       now<c->field.completed_us || now-c->field.completed_us>=UINT64_C(900000000) ||
       __atomic_load_n(&c->field.audit_lost,__ATOMIC_RELAXED))return false;
    c->field.confirmed=true;c->field.phase=TBX_FIELD_CONFIRMED;
    c->field.reason="OPERATOR_CONFIRMED_OPERATIONAL_INDIRECT_NOT_EXTERNAL";
    /* Legacy session bookkeeping only; the rolling protection owns separate
     * current RX counters and reference windows in c->rx_guard. */
    c->field.watch_at_us=now;c->field.watch_frames=c->field.frames;c->field.watch_errors=c->field.bad_frames;
    emit(c,"FIELD_CONFIRMED",0,c->field.reason);return true;
}
