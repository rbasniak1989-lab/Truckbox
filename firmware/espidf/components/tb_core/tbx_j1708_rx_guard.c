#include "tbx_j1708_active.h"
#include <math.h>
#include <string.h>

static tbx_rx_counts_t difference(tbx_rx_counts_t a,tbx_rx_counts_t b)
{
    return (tbx_rx_counts_t){a.valid-b.valid,a.checksum-b.checksum,a.framing-b.framing,
        a.overruns-b.overruns,a.uart_frame-b.uart_frame,a.read_errors-b.read_errors};
}
static void add(tbx_rx_counts_t *a,const tbx_rx_counts_t *b)
{
    a->valid+=b->valid;a->checksum+=b->checksum;a->framing+=b->framing;
    a->overruns+=b->overruns;a->uart_frame+=b->uart_frame;a->read_errors+=b->read_errors;
}
uint8_t tbx_j1708_active_flags(const tbx_j1708_active_t *c)
{
    return (uint8_t)((c->scheduler && c->scheduler->armed?1:0)|(c->work_mode?2:0)|(c->tx_active?4:0));
}
const char *tbx_j1708_guard_cause_name(tbx_guard_cause_t cause)
{
    static const char *const names[]={"NONE","RX_ERROR_RATE_PERSISTENT","RX_UART_FAULTS_PERSISTENT",
        "RX_MAJORITY_INVALID","RX_SILENCE","MONOTONIC_CLOCK_INVALID","RX_UNRELIABLE_DURING_TX",
        "TX_DRIVER_FAILURE","TX_ECHO_TIMEOUT","TX_ECHO_DIVERGENCE_OR_COLLISION",
        "MATCHING_RESPONSE_TIMEOUT","LOCAL_MID_CONFLICT","AUTHORIZATION_EXPIRED","OPERATOR_OR_OTA_STOP",
        "AUDIT_RECORD_LOST","OTHER_INTERLOCK"};
    return (unsigned)cause<sizeof(names)/sizeof(*names)?names[cause]:"OTHER_INTERLOCK";
}
static void start(tbx_j1708_rx_guard_t *g,uint64_t now)
{
    if(g->initialized)return;
    g->initialized=true;g->start_us=now;
    g->boundary=g->total;
}
void tbx_j1708_rx_note(tbx_j1708_active_t *c,tbx_rx_kind_t kind,uint64_t now)
{
    if(!c)return;
    tbx_j1708_rx_guard_t *g=&c->rx_guard;start(g,now);
    /* The first frame may have been timestamped just before a concurrent
     * monitor tick initialized the guard while the RX task waited for its lock.
     * Compare RX timestamps with RX timestamps, not that initialization tick. */
    if(g->observed && now<g->last_observed_us)g->clock_fault=true;
    else g->last_observed_us=now;
    g->observed=true;
    switch(kind){
        case TBX_RX_VALID:++g->total.valid;g->last_valid_us=now;g->seen_valid=true;break;
        case TBX_RX_CHECKSUM:++g->total.checksum;break;
        case TBX_RX_FRAMING:++g->total.framing;break;
        case TBX_RX_OVERRUN:++g->total.overruns;break;
        case TBX_RX_UART_FRAME:++g->total.uart_frame;break;
        case TBX_RX_READ_ERROR:++g->total.read_errors;break;
    }
}
static tbx_rx_window_t reference(const tbx_j1708_rx_guard_t *g)
{
    tbx_rx_window_t result={0};
    for(unsigned i=0;i<g->reference_count;i++){
        const tbx_rx_window_t *w=&g->reference[i];
        if(i==0 || w->start_us<result.start_us)result.start_us=w->start_us;
        if(w->end_us>result.end_us)result.end_us=w->end_us;
        add(&result.count,&w->count);
    }
    return result;
}
/* No assumed Poisson traffic rate or assumed independent ECU messages.
 * Wilson bounds are an operational comparison of observed corrupt frames;
 * shared noise/clock effects mean no electrical or causal proof follows. */
static void bounds(uint32_t valid,uint32_t errors,double *lower,double *upper)
{
    double n=(double)valid+errors;
    if(!n){*lower=0;*upper=1;return;}
    double p=errors/n,z2=TBX_RX_WILSON_Z2,denom=1+z2/n;
    double center=(p+z2/(2*n))/denom;
    double margin=sqrt(z2*(p*(1-p)/n+z2/(4*n*n)))/denom;
    *lower=fmax(0,center-margin);*upper=fmin(1,center+margin);
}
static tbx_rx_decision_t snapshot(tbx_j1708_active_t *c,tbx_rx_window_t window,uint64_t now)
{
    tbx_j1708_rx_guard_t *g=&c->rx_guard;
    tbx_rx_decision_t d={.at_us=now,.window=window,.reference=reference(g),.totals=g->total,
        .streak=g->suspect_streak,.before_flags=tbx_j1708_active_flags(c),.after_flags=tbx_j1708_active_flags(c),
        .last_failure=g->last_failure,.echoes=c->echo_ok,.collisions=c->collisions,.echo_timeouts=c->echo_timeouts};
    if(c->scheduler){
        d.sent=c->scheduler->sent_total;d.candidates=c->scheduler->answered_total;
        d.timeouts=c->scheduler->timeout_total;d.tx_errors=c->scheduler->tx_failure_total;
        d.timeout_ms=c->scheduler->timeout_ms;d.spacing_ms=c->scheduler->min_spacing_ms;d.request_failures=c->consecutive_failures;
        if(c->scheduler->pending<TBX_J1708_REQUEST_CAPACITY){
            unsigned i=c->scheduler->pending;d.pid=c->scheduler->pids[i].pid;d.target_mid=c->config.target_mid[i];
        }else if(c->tx_active){d.pid=c->tx_pid;for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)if(c->scheduler->pids[i].pid==c->tx_pid)d.target_mid=c->config.target_mid[i];}
    }
    return d;
}
static tbx_rx_window_t current(const tbx_j1708_rx_guard_t *g,uint64_t now)
{
    return (tbx_rx_window_t){g->start_us,now,difference(g->total,g->boundary)};
}
static void emit(tbx_j1708_active_t *c)
{
    if(c->ops.event)c->ops.event("GUARD_DECISION",c->rx_guard.decision.pid,
        tbx_j1708_guard_cause_name(c->rx_guard.decision.cause),c->context);
}
static void trip(tbx_j1708_active_t *c,tbx_rx_decision_t d)
{
    tbx_j1708_rx_guard_t *g=&c->rx_guard;
    if(g->fault_latched && !c->scheduler->armed && !c->tx_active)return;
    g->decision=d;g->decision_pending=true;g->fault_latched=true;
    const char *reason=tbx_j1708_guard_cause_name(d.cause);
    if(tbx_j1708_field_running(&c->field) || c->field.phase==TBX_FIELD_SATISFACTORY || c->field.confirmed)
        tbx_j1708_field_fail(c,reason);
    else tbx_j1708_active_stop(c,reason);
}
void tbx_j1708_rx_uart_fault(tbx_j1708_active_t *c,tbx_rx_kind_t kind,uint64_t now)
{
    tbx_j1708_rx_note(c,kind,now);
    /* Receiver faults do not increment collision/TX failure counters. Losing
     * RX while echo is in flight requires immediate release, not a blind retry. */
    if(c->tx_active){
        c->now_us=now;tbx_rx_decision_t d=snapshot(c,current(&c->rx_guard,now),now);
        d.cause=TBX_GUARD_RX_DURING_TX;d.predicates=TBX_GUARD_RX_BLIND_TX;trip(c,d);
    }
}
void tbx_j1708_rx_reconfigure(tbx_j1708_active_t *c)
{
    /* Only an explicit new configuration clears the fault latch. Keep RX
     * history measured in this boot; Wi-Fi/recovery never arms the scheduler. */
    c->rx_guard.fault_latched=false;c->rx_guard.suspect_streak=0;c->rx_guard.warned=false;
    c->rx_guard.last_failure=TBX_GUARD_NONE;
}
void tbx_j1708_rx_tick(tbx_j1708_active_t *c,uint64_t now)
{
    tbx_j1708_rx_guard_t *g=&c->rx_guard;start(g,now);
    bool enabled=c->scheduler && (c->scheduler->armed || c->tx_active);
    if(g->clock_fault || now<g->start_us || now<g->last_observed_us){
        if(enabled){tbx_rx_decision_t d=snapshot(c,current(g,now),now);d.cause=TBX_GUARD_CLOCK;d.predicates=TBX_GUARD_CLOCK_INVALID;trip(c,d);}
        return;
    }
    if(enabled && g->seen_valid && now-g->last_valid_us>=TBX_RX_WINDOW_US){
        tbx_rx_decision_t d=snapshot(c,current(g,now),now);d.cause=TBX_GUARD_RX_SILENCE;d.predicates=TBX_GUARD_SILENT;trip(c,d);return;
    }
    if(now-g->start_us<TBX_RX_WINDOW_US)return;
    tbx_rx_window_t w=current(g,now),r=reference(g);uint64_t duration=now-g->start_us;
    bool fresh=g->reference_count==TBX_RX_REFERENCE_WINDOWS && now>=r.end_us && now-r.end_us<=TBX_RX_REFERENCE_MAX_US;
    uint16_t flags=fresh?TBX_GUARD_REFERENCE_READY:0;
    if(duration>=TBX_RX_WINDOW_US*TBX_RX_REFERENCE_WINDOWS){
        flags=(uint16_t)((flags&~TBX_GUARD_REFERENCE_READY)|TBX_GUARD_WINDOW_STALE);
        g->reference_count=g->reference_cursor=g->suspect_streak=0;fresh=false;
    }
    double low=0,high=1,ref_low=0,ref_high=1;
    if(fresh){
        bounds(w.count.valid,w.count.checksum,&low,&high);bounds(r.count.valid,r.count.checksum,&ref_low,&ref_high);
        if(w.count.checksum>TBX_RX_PERSISTENCE && low>ref_high)flags|=TBX_GUARD_CRC_ELEVATED;
        double a,b,x,y;bounds(w.count.valid,w.count.framing,&a,&b);bounds(r.count.valid,r.count.framing,&x,&y);
        if(w.count.framing>TBX_RX_PERSISTENCE && a>y)flags|=TBX_GUARD_FRAME_ELEVATED;
    }
    if(w.count.overruns || w.count.uart_frame || w.count.read_errors)flags|=TBX_GUARD_UART_FAULT;
    if((uint64_t)w.count.checksum+w.count.framing>=w.count.valid && (w.count.checksum || w.count.framing))flags|=TBX_GUARD_MAJORITY_INVALID;
    bool suspect=(flags&(TBX_GUARD_CRC_ELEVATED|TBX_GUARD_FRAME_ELEVATED|TBX_GUARD_UART_FAULT|TBX_GUARD_MAJORITY_INVALID))!=0;
    if(suspect && !(flags&TBX_GUARD_WINDOW_STALE)){if(g->suspect_streak<UINT8_MAX)++g->suspect_streak;}else g->suspect_streak=0;
    if(g->suspect_streak>=TBX_RX_PERSISTENCE)flags|=TBX_GUARD_PERSISTENT;
    tbx_rx_decision_t d=snapshot(c,w,now);d.predicates=flags;d.current_lower_ppm=(uint32_t)(low*1000000+0.5);
    d.reference_upper_ppm=(uint32_t)(ref_high*1000000+0.5);d.streak=g->suspect_streak;
    d.cause=(flags&TBX_GUARD_MAJORITY_INVALID)?TBX_GUARD_RX_UNUSABLE:(flags&TBX_GUARD_UART_FAULT)?TBX_GUARD_RX_UART:TBX_GUARD_RX_QUALITY;
    g->last_window=w;g->start_us=now;g->boundary=g->total;
    if(enabled && (flags&TBX_GUARD_PERSISTENT)){trip(c,d);return;}
    /* Transition records only, not one record per window or raw frame. */
    if(enabled && suspect && !g->warned){g->decision=d;emit(c);g->warned=true;}
    else if(enabled && !suspect && g->warned){d.cause=TBX_GUARD_NONE;g->decision=d;emit(c);g->warned=false;}
    if(!suspect && w.count.valid && !(flags&TBX_GUARD_WINDOW_STALE)){
        if(g->reference_count && ((!fresh && g->reference_count==TBX_RX_REFERENCE_WINDOWS) || w.start_us!=r.end_us))g->reference_count=g->reference_cursor=0;
        g->reference[g->reference_cursor]=w;g->reference_cursor=(uint8_t)((g->reference_cursor+1u)%TBX_RX_REFERENCE_WINDOWS);
        if(g->reference_count<TBX_RX_REFERENCE_WINDOWS)++g->reference_count;
    }
}
static bool completed_normally(const char *reason)
{
    return reason && (!strcmp(reason,"FIELD_TEST_COMPLETE") || !strcmp(reason,"PHYSICAL_TRIAL_COMPLETE") || !strcmp(reason,"SEQUENTIAL_SCAN_COMPLETE"));
}
void tbx_j1708_rx_prepare_stop(tbx_j1708_active_t *c,const char *reason)
{
    tbx_j1708_rx_guard_t *g=&c->rx_guard;
    if(completed_normally(reason) || g->decision_pending || !tbx_j1708_active_flags(c))return;
    uint64_t now=c->now_us;if(now<g->last_observed_us)now=g->last_observed_us;
    g->decision=snapshot(c,current(g,now),now);
    g->decision.cause=TBX_GUARD_OTHER;
    if(reason && (!strcmp(reason,"THREE_CONSECUTIVE_REQUEST_FAILURES") || strstr(reason,"TX_LINK_ERROR") || strstr(reason,"BACKEND")))g->decision.cause=g->last_failure?g->last_failure:TBX_GUARD_OTHER;
    if(reason && !strcmp(reason,"FIELD_RESPONSE_TIMEOUT"))g->decision.cause=TBX_GUARD_REPLY_TIMEOUT;
    if(reason && !strcmp(reason,"PASSIVE_BUS_SILENCE")){g->decision.cause=TBX_GUARD_RX_SILENCE;g->decision.predicates|=TBX_GUARD_SILENT;}
    if(reason && !strcmp(reason,"LOCAL_MID_CONFLICT"))g->decision.cause=TBX_GUARD_MID_CONFLICT;
    if(reason && !strcmp(reason,"AUTHORIZATION_LEASE_EXPIRED"))g->decision.cause=TBX_GUARD_AUTHORITY;
    if(reason && (!strcmp(reason,"OPERATOR_STOP") || !strcmp(reason,"OTA_BUSY")))g->decision.cause=TBX_GUARD_OPERATOR;
    if(reason && strstr(reason,"AUDIT"))g->decision.cause=TBX_GUARD_AUDIT;
    if(reason && !strcmp(reason,"THREE_CONSECUTIVE_REQUEST_FAILURES"))g->decision.predicates|=TBX_GUARD_REQUEST_FAILURES;
    if(!g->decision.pid && (g->decision.cause==TBX_GUARD_TX_DRIVER || g->decision.cause==TBX_GUARD_ECHO_TIMEOUT || g->decision.cause==TBX_GUARD_ECHO_DIVERGENCE || g->decision.cause==TBX_GUARD_REPLY_TIMEOUT)){
        g->decision.pid=c->tx_pid;
        for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)if(c->scheduler->pids[i].pid==c->tx_pid)g->decision.target_mid=c->config.target_mid[i];
    }
    g->decision_pending=true;
}
void tbx_j1708_rx_stopped(tbx_j1708_active_t *c,const char *reason,uint8_t before)
{
    tbx_j1708_rx_guard_t *g=&c->rx_guard;
    if(completed_normally(reason) || !g->decision_pending)return;
    g->decision.before_flags=before;g->decision.after_flags=tbx_j1708_active_flags(c);g->decision.stopped=true;
    /* An expired lease or an operator stop requires fresh authorization, but
     * is not an electrical/RX fault. Real faults remain latched until an
     * explicit reconfiguration; neither category is automatically rearmed. */
    if((g->decision.cause>TBX_GUARD_NONE && g->decision.cause<=TBX_GUARD_MID_CONFLICT) || g->decision.cause==TBX_GUARD_AUDIT)g->fault_latched=true;
    g->decision_pending=false;emit(c);
}
