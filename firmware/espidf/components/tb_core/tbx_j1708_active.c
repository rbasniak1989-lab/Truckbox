#include "tbx_j1708_active.h"
#include <string.h>
#include <stdio.h>

static void event(tbx_j1708_active_t *c, const char *name, uint16_t pid, const char *detail)
{
    if (c->ops.event) c->ops.event(name,pid,detail,c->context);
}

static void release_tx(tbx_j1708_active_t *c)
{
    if (c->ops.abort_tx) c->ops.abort_tx(c->context);
    c->tx_active=false;
}

void tbx_j1708_active_init(tbx_j1708_active_t *c, tbx_j1708_request_scheduler_t *s,
                           const tbx_j1708_active_ops_t *ops, void *context)
{
    if (!c) return;
    memset(c,0,sizeof(*c));
    c->scheduler=s;
    if (ops) c->ops=*ops;
    c->context=context;
    c->idle_guard_us=5000;
    if (s) tbx_j1708_request_init(s);
}

bool tbx_j1708_active_configure(tbx_j1708_active_t *c, const tbx_j1708_active_config_t *cfg)
{
    if (!c || !c->scheduler || !cfg || c->scheduler->armed ||
        !c->ops.begin_tx || !c->ops.abort_tx) return false;
    if (!cfg->inverted_de21_attested || cfg->local_mid<128u ||
        cfg->priority<1u || cfg->priority>8u) return false;
    bool start_pid_known = cfg->start_pid == 0;
    uint8_t scan_start_index = 0;
    for (unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++) {
        if (cfg->target_mid[i]<128u || cfg->target_mid[i]==cfg->local_mid) return false;
        if (cfg->start_pid && c->scheduler->pids[i].pid==cfg->start_pid) {
            start_pid_known = true;
            scan_start_index = (uint8_t)i;
        }
    }
    if ((cfg->sequential_scan || cfg->single_shot) && !start_pid_known) return false;
    c->scan_start_index = (cfg->sequential_scan || cfg->single_shot) ? scan_start_index : 0;
    c->scan_budget = cfg->single_shot?1:cfg->sequential_scan ? (uint8_t)(TBX_J1708_REQUEST_COUNT - scan_start_index) : 0;
    c->config=*cfg;
    /* Conservative test policy: never reduce the idle guard below5ms.
     * (10+2*priority) bit times implements a priority-dependent minimum;
     * the larger guard gives the already-present nodes additional time. */
    uint32_t priority_guard=((10u+2u*cfg->priority)*1000000u+9599u)/9600u;
    c->idle_guard_us=priority_guard>5000u?priority_guard:5000u;
    c->configured=true;
    tbx_j1708_rx_reconfigure(c);
    memset(&c->field,0,sizeof(c->field));
    c->physical_validated=false;c->last_response_us=0;c->consecutive_failures=0;
    c->scheduler->periodic=false;
    c->trial_echo_base=c->echo_ok;
    c->trial_answer_base=c->scheduler->answered_total;
    c->promotion_deadline_us=0;
    event(c,"CONFIGURED",0,"EXPLICIT_MIDS_AND_INVERTED_DE21_ATTESTED");
    return true;
}

bool tbx_j1708_active_arm(tbx_j1708_active_t *c, bool authorized, bool stationary, uint64_t now_us)
{
    if (!c || !c->scheduler) return false;
    if (!authorized || !(stationary || c->work_mode)) {
        tbx_j1708_active_stop(c,"AUTHORIZATION_OR_OPERATION_MODE_MISSING");
        return false;
    }
    if (!c->configured || c->scheduler->safety_stopped || c->rx_guard.fault_latched) {
        event(c,"ARM_REJECTED",0,"CONFIGURATION_MISSING_OR_SAFETY_STOPPED");
        return false;
    }
    bool was_armed=c->scheduler->armed;
    tbx_j1708_request_permit_stationary_test(c->scheduler,true);
    if (!tbx_j1708_request_arm(c->scheduler)) return false;
    c->now_us=now_us;
    c->lease_deadline_us=now_us+(c->work_mode ? TBX_J1708_WORK_LEASE_US : TBX_J1708_ACTIVE_LEASE_US);
    if (!was_armed) {
        c->retry_after_us=0;
        c->last_rx_us=now_us;
        c->scan_sent_baseline=c->scheduler->sent_total;
        c->scan_completed=0;
        if(c->config.sequential_scan || c->config.single_shot){
            c->scheduler->cursor=c->scan_start_index;
            /* Max one in flight. Each next PID waits for response or timeout,
             * plus >=1200ms between successful request transmissions. */
            tbx_j1708_request_set_spacing(c->scheduler,1200);
        }
    }
    event(c,was_armed?"LEASE_RENEWED":"ARMED",0,c->work_mode ? "AUTHORIZED_WORK_12_HOUR_LEASE" : "AUTHORIZED_PARKED_60_SECOND_LEASE");
    return true;
}

bool tbx_j1708_active_work_ready(const tbx_j1708_active_t *c, uint64_t now_us)
{
    /* Keep the first transmitter validation physically stationary; a passive
     * matching MID/PID alone is not enough to establish working TX. */
    if (!c || !c->scheduler || !c->configured ||
        c->scheduler->safety_stopped || c->rx_guard.fault_latched || c->work_mode || c->tx_active || !(c->physical_validated || c->field.confirmed) ||
        c->scheduler->link_error_count ||
        now_us >= (c->scheduler->armed ? c->lease_deadline_us : c->promotion_deadline_us) ||
        c->echo_ok-c->trial_echo_base < 1u ||
        c->scheduler->answered_total-c->trial_answer_base == 0u) return false;
    return true;
}

bool tbx_j1708_active_attest_physical(tbx_j1708_active_t *c,bool external_tx,bool external_reply,uint64_t now)
{
    if(!c || !c->configured || !external_tx || !external_reply || c->tx_active ||
       c->scheduler->safety_stopped || c->rx_guard.fault_latched || c->scheduler->link_error_count ||
       !c->last_response_us || now<c->last_response_us || now-c->last_response_us>UINT64_C(900000000) ||
       c->last_response_pid!=c->tx_pid || c->echo_ok==c->trial_echo_base)return false;
    c->physical_validated=true;
    event(c,"PHYSICAL_VALIDATION",c->tx_pid,"OPERATOR_CONFIRMED_EXTERNAL_TX_AND_REPLY");
    return true;
}

bool tbx_j1708_active_enable_work(tbx_j1708_active_t *c, uint64_t now_us)
{
    if(!tbx_j1708_active_work_ready(c,now_us))return false;
    c->work_mode=true;
    c->now_us=now_us;
    c->config.sequential_scan=false;c->config.single_shot=false;
    tbx_j1708_request_permit_stationary_test(c->scheduler,true);
    (void)tbx_j1708_request_arm(c->scheduler);
    tbx_j1708_request_set_spacing(c->scheduler,TBX_J1708_WORK_MIN_SPACING_MS);
    /* Preserve the last send deadline even after a completed parked scan.
     * Promotion must never create an immediate extra request. */
    if (c->scheduler->sent_total && c->tx_started_us) {
        c->scheduler->next_tx_ms=(uint32_t)(c->tx_started_us/1000u)+
            TBX_J1708_WORK_MIN_SPACING_MS;
        c->scheduler->next_tx_valid=true;
    }
    /* A resumed parked scan may start at PID117; road polling covers all17. */
    tbx_j1708_request_start_periodic(c->scheduler,(uint32_t)(now_us/1000u));
    c->lease_deadline_us=now_us+TBX_J1708_WORK_LEASE_US;
    event(c,"WORK_ENABLED",0,c->physical_validated?"EXTERNAL_CAPTURE_ATTESTED_AND_MATCHING_RESPONSE":"SUPERVISED_FIELD_OPERATIONAL_INDIRECT_OPERATOR_CONFIRMED");
    return true;
}

void tbx_j1708_active_stop(tbx_j1708_active_t *c, const char *reason)
{
    if (!c) return;
    uint8_t before_flags=tbx_j1708_active_flags(c);
    tbx_j1708_rx_prepare_stop(c,reason);
    if(tbx_j1708_field_running(&c->field) ||
       ((c->field.phase==TBX_FIELD_SATISFACTORY || c->field.confirmed) && (!reason || strcmp(reason,"FIELD_TEST_COMPLETE")))){
        c->field.phase=TBX_FIELD_FAILED;c->field.confirmed=false;c->field.reason=reason?reason:"OPERATOR_STOP";
    }
    c->promotion_deadline_us=(reason && (!strcmp(reason,"SEQUENTIAL_SCAN_COMPLETE") || !strcmp(reason,"PHYSICAL_TRIAL_COMPLETE") || !strcmp(reason,"FIELD_TEST_COMPLETE")))?
        c->now_us+UINT64_C(900000000):0;
    release_tx(c);
    c->work_mode=false;
    if (c->scheduler) {
        tbx_j1708_request_permit_stationary_test(c->scheduler,false);
        tbx_j1708_request_set_spacing(c->scheduler,250);
    }
    c->lease_deadline_us=0;
    tbx_j1708_rx_stopped(c,reason,before_flags);
    event(c,"STOPPED",c->tx_pid,reason?reason:"OPERATOR_STOP");
}

void tbx_j1708_active_note_link_error(tbx_j1708_active_t *c, const char *reason)
{
    if (!c || !c->scheduler || (!c->scheduler->armed && !c->tx_active)) return;
    bool was_field=tbx_j1708_field_running(&c->field);
    bool was_tx=c->tx_active;
    release_tx(c);
    if (was_tx) c->scheduler->pending=TBX_J1708_NO_PENDING;
    /* Retry the disputed PID only after a new idle guard AND existing spacing.
     * Do not wrap away from a failed request or transmit into the same burst. */
    for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++)
        if(c->scheduler->pids[i].pid==c->tx_pid){c->scheduler->cursor=(uint8_t)i;
            if(c->scheduler->periodic)c->scheduler->pids[i].next_due_ms=(uint32_t)(c->now_us/1000u);}
    c->retry_after_us=c->now_us+20000u+
        ((c->config.local_mid*37u+c->collisions*97u)%31u)*1000u;
    bool stopped=c->scheduler->safety_stopped;
    tbx_j1708_request_note_link_error(c->scheduler);
    if(c->consecutive_failures<UINT8_MAX)++c->consecutive_failures;
    if(c->consecutive_failures>=3){c->scheduler->safety_stopped=true;tbx_j1708_request_stop(c->scheduler);}
    event(c,"LINK_ERROR",c->tx_pid,reason?reason:"UNSPECIFIED_LINK_ERROR");
    if (!stopped && c->scheduler->safety_stopped)
        event(c,"AUTO_DISARMED",c->tx_pid,"THREE_LINK_ERRORS");
    if(was_field){tbx_j1708_field_fail(c,"FIELD_TX_LINK_ERROR");return;}
    if(c->scheduler->safety_stopped)tbx_j1708_active_stop(c,"THREE_CONSECUTIVE_REQUEST_FAILURES");
    else if(c->config.single_shot)tbx_j1708_active_stop(c,"PHYSICAL_TRIAL_TX_FAILED");
}

static bool send_request(uint16_t pid, void *context)
{
    tbx_j1708_active_t *c=context;
    unsigned index=TBX_J1708_REQUEST_COUNT;
    for (unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++)
        if (c->scheduler->pids[i].pid==pid) {index=i;break;}
    if (index==TBX_J1708_REQUEST_COUNT || pid>255u) return false;
    c->tx_frame[0]=c->config.local_mid;
    c->tx_frame[1]=128; /* Proven component-specific request PID. */
    c->tx_frame[2]=(uint8_t)pid;
    c->tx_frame[3]=c->config.target_mid[index];
    c->tx_frame[4]=(uint8_t)(0u-c->tx_frame[0]-c->tx_frame[1]-c->tx_frame[2]-c->tx_frame[3]);
    c->tx_pid=pid;
    c->echo_position=0;
    c->tx_started_us=c->now_us;
    c->tx_active=true;
    const tbx_j1708_tx_result_t result=c->ops.begin_tx(c->tx_frame,sizeof(c->tx_frame),c->context);
    if(result==TBX_J1708_TX_DEFERRED){
        c->tx_active=false;
        c->scheduler->send_deferred=true;
        c->last_rx_us=c->now_us;
        return false;
    }
    if (result!=TBX_J1708_TX_STARTED) {
        release_tx(c);
        c->rx_guard.last_failure=TBX_GUARD_TX_DRIVER;
        event(c,"TX_ERROR",pid,"BACKEND_REJECTED_REQUEST");
        if(c->consecutive_failures<UINT8_MAX)++c->consecutive_failures;
        if(tbx_j1708_field_running(&c->field))tbx_j1708_field_fail(c,"FIELD_TX_BACKEND_REJECTED");
        else if(c->config.single_shot)tbx_j1708_active_stop(c,"PHYSICAL_TRIAL_BACKEND_FAILED");
        /* The scheduler counts the failed callback as a link error. */
        return false;
    }
    tbx_j1708_field_sent(c);
    event(c,"REQUEST",pid,"PID128_TARGETED_REQUEST_QUEUED");
    return true;
}

void tbx_j1708_active_tick(tbx_j1708_active_t *c, uint64_t now_us)
{
    if (!c || !c->scheduler) return;
    c->now_us=now_us;
    if (c->scheduler->armed && now_us>=c->lease_deadline_us) {
        ++c->lease_expirations;
        tbx_j1708_active_stop(c,"AUTHORIZATION_LEASE_EXPIRED");
        return;
    }
    tbx_j1708_rx_tick(c,now_us);
    bool field_may_send=tbx_j1708_field_tick(c,now_us);
    if(!c->scheduler->armed)return;
    if (c->tx_active && now_us>=c->tx_started_us &&
        now_us-c->tx_started_us>=TBX_J1708_ACTIVE_ECHO_TIMEOUT_US) {
        ++c->echo_timeouts;c->rx_guard.last_failure=TBX_GUARD_ECHO_TIMEOUT;
        tbx_j1708_active_note_link_error(c,"TX_ECHO_TIMEOUT");
    }
    if (!c->scheduler->armed) return;
    uint32_t previous_timeouts=c->scheduler->timeout_total;
    uint16_t pending_pid=c->scheduler->pending<TBX_J1708_REQUEST_COUNT?
        c->scheduler->pids[c->scheduler->pending].pid:0;
    /* Deadline processing is independent of bus availability; a busy bus
     * cannot be mistaken for a failed send or suppress response timeouts. */
    tbx_j1708_request_tick(c->scheduler,(uint32_t)(now_us/1000u),NULL,NULL);
    if (c->scheduler->timeout_total!=previous_timeouts) {
        if(c->config.sequential_scan)++c->scan_completed;
        c->rx_guard.last_failure=TBX_GUARD_REPLY_TIMEOUT;
        event(c,"TIMEOUT",pending_pid,"NO_MATCHING_MID_PID_WITHIN_CONFIGURED_TIMEOUT");
        if(tbx_j1708_field_running(&c->field)){tbx_j1708_field_fail(c,"FIELD_RESPONSE_TIMEOUT");return;}
        if(c->work_mode && c->consecutive_failures<UINT8_MAX)++c->consecutive_failures;
        if(c->work_mode && c->consecutive_failures>=3){
            c->scheduler->safety_stopped=true;event(c,"AUTO_DISARMED",pending_pid,"THREE_CONSECUTIVE_REQUEST_FAILURES");
            tbx_j1708_active_stop(c,"THREE_CONSECUTIVE_REQUEST_FAILURES");
        }
    }
    if (!c->scheduler->armed) {
        release_tx(c);
        return;
    }
    if(c->config.single_shot && c->scheduler->sent_total>c->scan_sent_baseline){
        if(!c->tx_active && c->scheduler->pending==TBX_J1708_NO_PENDING)
            tbx_j1708_active_stop(c,"PHYSICAL_TRIAL_COMPLETE");
        return;
    }
    if (c->config.sequential_scan) {
        if (c->scheduler->cursor == 0 && c->tx_pid==c->scheduler->pids[TBX_J1708_REQUEST_COUNT-1].pid &&
            c->scheduler->sent_total > c->scan_sent_baseline) {
            /* Last request must finish echo and receive a response or timeout.
             * Stop before the scheduler wraps back to the first PID. */
            if (!c->tx_active && c->scheduler->pending == TBX_J1708_NO_PENDING)
                tbx_j1708_active_stop(c,"SEQUENTIAL_SCAN_COMPLETE");
            return;
        }
    }
    if (!field_may_send || c->tx_active || now_us<c->retry_after_us || now_us<c->last_rx_us || now_us-c->last_rx_us<c->idle_guard_us) return;
    bool was_stopped=c->scheduler->safety_stopped;
    tbx_j1708_request_tick(c->scheduler,(uint32_t)(now_us/1000u),send_request,c);
    if(c->consecutive_failures>=3){c->scheduler->safety_stopped=true;tbx_j1708_request_stop(c->scheduler);}
    if (!was_stopped && c->scheduler->safety_stopped) {
        release_tx(c);
        event(c,"AUTO_DISARMED",c->tx_pid,"THREE_LINK_ERRORS");
        tbx_j1708_active_stop(c,"THREE_CONSECUTIVE_REQUEST_FAILURES");
    }
}

void tbx_j1708_active_note_rx(tbx_j1708_active_t *c, const uint8_t *bytes, size_t length, uint64_t now_us)
{
    if (!c || (!bytes && length)) return;
    c->now_us=now_us;
    c->last_rx_us=now_us;
    for (size_t i=0;i<length && c->tx_active;i++) {
        if (bytes[i]!=c->tx_frame[c->echo_position]) {
            ++c->collisions;c->rx_guard.last_failure=TBX_GUARD_ECHO_DIVERGENCE;
            char detail[96];
            snprintf(detail,sizeof(detail),
                     "TX_ECHO_MISMATCH_OR_COLLISION index=%u expected=%02X received=%02X",
                     (unsigned)c->echo_position,
                     (unsigned)c->tx_frame[c->echo_position],(unsigned)bytes[i]);
            tbx_j1708_active_note_link_error(c,detail);
            return;
        }
        if (++c->echo_position==sizeof(c->tx_frame)) {
            ++c->echo_ok;
            tbx_j1708_field_echo(c);
            if(!c->work_mode)c->scheduler->link_error_count=0; /* Travel resets after matching reply. */
            release_tx(c);
            event(c,"TX_ECHO",c->tx_pid,"COMPLETE_MATCHING_BUS_ECHO");
        }
    }
}

bool tbx_j1708_active_note_response(tbx_j1708_active_t *c, uint8_t mid, uint16_t pid, uint64_t now_us)
{
    if (!c || !c->scheduler || !c->scheduler->armed || c->tx_active ||
        c->scheduler->pending>=TBX_J1708_REQUEST_COUNT) return false;
    if (now_us >= c->lease_deadline_us) {
        ++c->lease_expirations;
        tbx_j1708_active_stop(c, "AUTHORIZATION_LEASE_EXPIRED");
        return false;
    }
    if (now_us < c->tx_started_us) return false;
    uint8_t index=c->scheduler->pending;
    if (mid!=c->config.target_mid[index] || pid!=c->scheduler->pids[index].pid) return false;
    if (!tbx_j1708_request_note_response(c->scheduler,pid,(uint32_t)(now_us/1000u))) return false;
    if(c->config.sequential_scan)++c->scan_completed;
    if(now_us>c->now_us)c->now_us=now_us;
    c->consecutive_failures=0;c->scheduler->link_error_count=0;
    c->last_response_pid=pid;c->last_response_mid=mid;c->last_response_us=now_us;
    event(c,"RESPONSE_CANDIDATE",pid,"MID_PID_TIME_MATCH_IS_NOT_CAUSAL_PROOF");
    tbx_j1708_field_reply(c,now_us);
    return true;
}

bool tbx_j1708_active_is_tx_echo(const tbx_j1708_active_t *c, const uint8_t *frame, size_t length, uint64_t now_us)
{
    return c && frame && c->echo_position==sizeof(c->tx_frame) && length==sizeof(c->tx_frame) &&
        now_us>=c->tx_started_us && now_us-c->tx_started_us<=UINT64_C(30000) &&
        memcmp(frame,c->tx_frame,sizeof(c->tx_frame))==0;
}
