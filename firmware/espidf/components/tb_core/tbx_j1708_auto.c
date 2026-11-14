#include "tbx_j1708_auto.h"
#include <string.h>
const char *tbx_j1708_auto_phase_name(tbx_auto_phase_t phase)
{
    static const char *const names[]={"WAITING_HEALTHY_RX","RUNNING","RECOVERY_BACKOFF","BLOCKED","DISABLED","OTA_PAUSED"};
    return (unsigned)phase<6?names[phase]:"BLOCKED";
}
static bool healthy(const tbx_j1708_rx_guard_t *g,uint64_t after,uint64_t now)
{
    if(g->clock_fault || !g->seen_valid || now<g->last_valid_us || now-g->last_valid_us>TBX_RX_WINDOW_US/2 ||
       g->reference_count!=3 || g->suspect_streak || !g->last_window.count.valid)return false;
    for(unsigned i=0;i<3;i++){
        const tbx_rx_window_t *w=&g->reference[i];
        if(w->start_us<after || now<w->end_us || now-w->end_us>TBX_RX_REFERENCE_MAX_US ||
           !w->count.valid || w->count.overruns || w->count.uart_frame || w->count.read_errors)return false;
    }
    return true;
}
static void pause(tbx_j1708_auto_t *a,tbx_j1708_active_t *c,const char *reason,tbx_auto_phase_t phase,uint64_t now)
{
    if(c->production_mode){a->last_stop_reason=reason;a->stopped_us=now;tbx_j1708_active_stop(c,reason);}
    a->phase=phase;a->reason=reason;a->paused_us=now;a->due_since_us=0;
}
void tbx_j1708_auto_suspend(tbx_j1708_auto_t *a,tbx_j1708_active_t *c,const char *reason,uint64_t now)
{
    pause(a,c,reason,TBX_AUTO_BACKOFF,now);++a->pauses;
    a->last_stop_reason=reason;a->stopped_us=now;
    if(a->failures<4)++a->failures;
    a->resume_us=now+(UINT64_C(60000000)<<(a->failures-1));
}
void tbx_j1708_auto_tick(tbx_j1708_auto_t *a,tbx_j1708_active_t *c,const tbx_j1708_auto_env_t *e,bool launch,uint64_t now)
{
    uint64_t step=now>=a->checked_us?now-a->checked_us:0;
    if(step>TBX_RX_WINDOW_US)step=0; /* No fictitious eligible time after a gap. */
    a->checked_us=now;
    if(!e->enabled || e->operator_hold){
        const char *reason=!e->enabled?"PERSISTENT_OPERATOR_DISABLE":"OPERATOR_OR_MAINTENANCE_HOLD";
        if(a->phase!=TBX_AUTO_DISABLED || !a->reason || strcmp(a->reason,reason))pause(a,c,reason,TBX_AUTO_DISABLED,now);
        a->last_enabled=false;return;
    }
    if(!a->last_enabled){a->phase=TBX_AUTO_WAIT_RX;a->reason="WAITING_STABLE_PASSIVE_NETWORK";a->paused_us=now;a->last_enabled=true;}
    if(!e->persisted || !e->interface_ready || !e->local_mid || !e->topology_attested || e->local_conflict || c->rx_guard.clock_fault){
        const char *reason=e->local_conflict?"LOCAL_MID_CONFLICT":!e->persisted?"NVS_POLICY_UNAVAILABLE":!e->local_mid || !e->topology_attested?"LOCAL_TX_PROFILE_REQUIRED":c->rx_guard.clock_fault?"MONOTONIC_CLOCK_INVALID":"UART_OR_EDGE_MONITOR_UNAVAILABLE";
        if(a->phase!=TBX_AUTO_BLOCKED || a->reason!=reason)pause(a,c,reason,TBX_AUTO_BLOCKED,now);
        return;
    }
    if(e->ota_busy){if(a->phase!=TBX_AUTO_OTA)pause(a,c,"OTA_BUSY",TBX_AUTO_OTA,now);return;}
    if(a->phase==TBX_AUTO_BLOCKED || a->phase==TBX_AUTO_OTA){a->phase=TBX_AUTO_WAIT_RX;a->paused_us=now;}
    if(a->phase==TBX_AUTO_RUNNING && (!c->work_mode || !c->scheduler->armed)){
        ++a->pauses;if(a->failures<4)++a->failures;
        a->phase=TBX_AUTO_BACKOFF;a->paused_us=now;
        a->resume_us=now+(UINT64_C(60000000)<<(a->failures-1));
        a->reason=tbx_j1708_guard_cause_name(c->rx_guard.decision.cause);
        a->last_stop_reason=a->reason;a->stopped_us=now;return;
    }
    if(a->phase==TBX_AUTO_RUNNING){
        /* Supervise actual send progress, not periodic statistics. The grace
         * covers a complete healthy RX observation and a full queue rotation.
         * A busy bus is a deferral, never a reason to force transmission. */
        bool due=false;
        for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)
            if(c->scheduler->pids[i].enabled && (int32_t)((uint32_t)(now/1000)-c->scheduler->pids[i].next_due_ms)>=0)due=true;
        if(c->scheduler->sent_total!=a->last_sent || c->scheduler->deferred_total!=a->last_deferred || !due){
            a->last_sent=c->scheduler->sent_total;a->last_deferred=c->scheduler->deferred_total;
            a->progress_us=now;a->due_since_us=0;a->eligible_stall_us=0;
        }else{
            if(!a->due_since_us)a->due_since_us=now;
            if(!e->bus_busy)a->eligible_stall_us+=step;
        }
        uint64_t grace=3*TBX_RX_WINDOW_US;
        uint64_t rotation=(uint64_t)(e->timeout_ms+(TBX_J1708_REQUEST_CAPACITY+1)*c->scheduler->min_spacing_ms)*1000;
        if(rotation>grace)grace=rotation;
        if(a->eligible_stall_us>grace &&
           !c->tx_active && healthy(&c->rx_guard,0,now)){
            ++a->stalls;pause(a,c,"SCHEDULER_NO_TX_PROGRESS",TBX_AUTO_BACKOFF,now);
            if(a->failures<4)++a->failures;
            a->resume_us=now+(UINT64_C(60000000)<<(a->failures-1));
            return;
        }
        a->reason=e->bus_busy?"BUS_BUSY_DEFERRED":"PERSISTENT_POLICY_OPERATIONAL_INDIRECT";
        if(launch!=a->launch){tbx_j1708_request_production(c->scheduler,e->periods,launch,(uint32_t)(now/1000),false);a->launch=launch;}
        /* Noise warnings slow TX without attributing corrupt RX to our TX. */
        tbx_j1708_request_set_spacing(c->scheduler,c->rx_guard.suspect_streak || (a->failures && now-a->started_us<UINT64_C(30000000))?1000:500);
        if(now-a->started_us>UINT64_C(300000000) && c->scheduler->answered_total)a->failures=0;
        return;
    }
    if(a->phase==TBX_AUTO_BACKOFF && now<a->resume_us)return;
    if(!healthy(&c->rx_guard,a->paused_us,now))return;
    if(c->scheduler->armed || c->tx_active)return; /* Never duplicate a manual scheduler. */
    tbx_j1708_active_config_t cfg={.local_mid=e->local_mid,.priority=8,.inverted_de21_attested=e->topology_attested};
    memcpy(cfg.target_mid,e->targets,sizeof(e->targets));
    /* A healthy, recent passive observation permits a bounded new TX attempt;
     * it is not external proof. All three-failure/echo/idle guards run again. */
    c->scheduler->safety_stopped=false;c->scheduler->link_error_count=0;c->scheduler->consecutive_timeouts=0;
    if(!tbx_j1708_active_configure(c,&cfg)){a->reason="CONFIGURATION_REJECTED";return;}
    c->scheduler->timeout_ms=e->timeout_ms;
    if(!tbx_j1708_active_start_production(c,now)){a->reason="START_INTERLOCK_REFUSED";return;}
    tbx_j1708_request_production(c->scheduler,e->periods,launch,(uint32_t)(now/1000),true);
    tbx_j1708_request_expansion(c->scheduler,e->extended,(uint32_t)(now/1000),true);
    a->phase=TBX_AUTO_RUNNING;a->reason="PERSISTENT_POLICY_OPERATIONAL_INDIRECT";
    a->launch=launch;a->started_us=now;a->progress_us=now;a->due_since_us=0;a->eligible_stall_us=0;
    a->last_sent=c->scheduler->sent_total;a->last_deferred=c->scheduler->deferred_total;++a->starts;
}
