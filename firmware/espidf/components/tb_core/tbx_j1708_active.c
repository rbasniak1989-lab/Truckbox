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
    if (cfg->sequential_scan && !start_pid_known) return false;
    c->scan_start_index = cfg->sequential_scan ? scan_start_index : 0;
    c->scan_budget = cfg->sequential_scan ? (uint8_t)(TBX_J1708_REQUEST_COUNT - scan_start_index) : 0;
    c->config=*cfg;
    /* Conservative test policy: never reduce the idle guard below5ms.
     * (10+2*priority) bit times implements a priority-dependent minimum;
     * the larger guard gives the already-present nodes additional time. */
    uint32_t priority_guard=((10u+2u*cfg->priority)*1000000u+9599u)/9600u;
    c->idle_guard_us=priority_guard>5000u?priority_guard:5000u;
    c->configured=true;
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
    if (!c->configured || c->scheduler->safety_stopped) {
        event(c,"ARM_REJECTED",0,"CONFIGURATION_MISSING_OR_SAFETY_STOPPED");
        return false;
    }
    bool was_armed=c->scheduler->armed;
    tbx_j1708_request_permit_stationary_test(c->scheduler,true);
    if (!tbx_j1708_request_arm(c->scheduler)) return false;
    c->now_us=now_us;
    c->lease_deadline_us=now_us+(c->work_mode ? TBX_J1708_WORK_LEASE_US : TBX_J1708_ACTIVE_LEASE_US);
    if (!was_armed) {
        c->last_rx_us=now_us;
        c->scan_sent_baseline=c->scheduler->sent_total;
        c->scan_failed_baseline=c->scheduler->tx_failure_total;
        if(c->config.sequential_scan){
            c->scheduler->cursor=c->scan_start_index;
            /* Max one in flight. Each next PID waits for response or timeout,
             * plus >=1200ms between successful request transmissions. */
            tbx_j1708_request_set_spacing(c->scheduler,1200);
        }
    }
    event(c,was_armed?"LEASE_RENEWED":"ARMED",0,c->work_mode ? "AUTHORIZED_WORK_12_HOUR_LEASE" : "AUTHORIZED_PARKED_60_SECOND_LEASE");
    return true;
}

bool tbx_j1708_active_enable_work(tbx_j1708_active_t *c, uint64_t now_us)
{
    /* Keep the first transmitter validation physically stationary; a passive
     * matching MID/PID alone is not enough to establish working TX. */
    if (!c || !c->scheduler || !c->configured || !c->scheduler->armed ||
        c->scheduler->safety_stopped || c->config.sequential_scan || c->work_mode || c->tx_active ||
        now_us >= c->lease_deadline_us || c->echo_ok < 3u ||
        c->scheduler->answered_total == 0u) return false;
    c->work_mode=true;
    c->now_us=now_us;
    tbx_j1708_request_set_spacing(c->scheduler,TBX_J1708_WORK_MIN_SPACING_MS);
    /* The last parked send scheduled the next slot using the 250 ms test
     * spacing. Rebase that deadline so the FIRST road request also honors
     * the stricter 500 ms work-mode limit. */
    if (c->scheduler->next_tx_valid && c->scheduler->sent_total) {
        unsigned prev=(c->scheduler->cursor+TBX_J1708_REQUEST_COUNT-1u) %
            TBX_J1708_REQUEST_COUNT;
        c->scheduler->next_tx_ms=c->scheduler->pids[prev].last_sent_ms+
            TBX_J1708_WORK_MIN_SPACING_MS;
    }
    c->lease_deadline_us=now_us+TBX_J1708_WORK_LEASE_US;
    event(c,"WORK_ENABLED",0,"3_ECHO_AND_RESPONSE_CANDIDATE_CONFIRMED");
    return true;
}

void tbx_j1708_active_stop(tbx_j1708_active_t *c, const char *reason)
{
    if (!c) return;
    release_tx(c);
    c->work_mode=false;
    if (c->scheduler) {
        tbx_j1708_request_permit_stationary_test(c->scheduler,false);
        tbx_j1708_request_set_spacing(c->scheduler,250);
    }
    c->lease_deadline_us=0;
    event(c,"STOPPED",c->tx_pid,reason?reason:"OPERATOR_STOP");
}

void tbx_j1708_active_note_link_error(tbx_j1708_active_t *c, const char *reason)
{
    if (!c || !c->scheduler || (!c->scheduler->armed && !c->tx_active)) return;
    bool was_tx=c->tx_active;
    release_tx(c);
    if (was_tx) c->scheduler->pending=TBX_J1708_NO_PENDING;
    bool stopped=c->scheduler->safety_stopped;
    tbx_j1708_request_note_link_error(c->scheduler);
    event(c,"LINK_ERROR",c->tx_pid,reason?reason:"UNSPECIFIED_LINK_ERROR");
    if (c->config.sequential_scan && !c->scheduler->safety_stopped) {
        /* One disputed TX or UART framing error blocks further requests.
         * Passive bad-checksum frames are tracked separately by RX capture. */
        c->scheduler->safety_stopped=true;
        tbx_j1708_request_stop(c->scheduler);
        event(c,"AUTO_DISARMED",c->tx_pid,"SEQUENTIAL_TX_LINK_ERROR");
    } else if (!stopped && c->scheduler->safety_stopped)
        event(c,"AUTO_DISARMED",c->tx_pid,"THREE_LINK_ERRORS");
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
    if (!c->ops.begin_tx(c->tx_frame,sizeof(c->tx_frame),c->context)) {
        release_tx(c);
        event(c,"TX_ERROR",pid,"BACKEND_REJECTED_REQUEST");
        /* The scheduler counts the failed callback as a link error. */
        return false;
    }
    event(c,"REQUEST",pid,"PID128_TARGETED_REQUEST_QUEUED");
    return true;
}

void tbx_j1708_active_tick(tbx_j1708_active_t *c, uint64_t now_us)
{
    if (!c || !c->scheduler || !c->scheduler->armed) return;
    c->now_us=now_us;
    if (now_us>=c->lease_deadline_us) {
        ++c->lease_expirations;
        tbx_j1708_active_stop(c,"AUTHORIZATION_LEASE_EXPIRED");
        return;
    }
    if (c->tx_active && now_us>=c->tx_started_us &&
        now_us-c->tx_started_us>=TBX_J1708_ACTIVE_ECHO_TIMEOUT_US) {
        ++c->echo_timeouts;
        tbx_j1708_active_note_link_error(c,"TX_ECHO_TIMEOUT");
    }
    if (!c->scheduler->armed) return;
    uint32_t previous_timeouts=c->scheduler->timeout_total;
    uint16_t pending_pid=c->scheduler->pending<TBX_J1708_REQUEST_COUNT?
        c->scheduler->pids[c->scheduler->pending].pid:0;
    /* Deadline processing is independent of bus availability; a busy bus
     * cannot be mistaken for a failed send or suppress response timeouts. */
    tbx_j1708_request_tick(c->scheduler,(uint32_t)(now_us/1000u),NULL,NULL);
    if (c->scheduler->timeout_total!=previous_timeouts)
        event(c,"TIMEOUT",pending_pid,"NO_MATCHING_MID_PID_WITHIN_450MS");
    if (!c->scheduler->armed) {
        release_tx(c);
        event(c,"AUTO_DISARMED",pending_pid,"EIGHT_RESPONSE_TIMEOUTS");
        return;
    }
    if (c->config.sequential_scan) {
        if (c->scheduler->tx_failure_total > c->scan_failed_baseline) {
            c->scheduler->safety_stopped=true;
            tbx_j1708_active_stop(c,"SEQUENTIAL_TX_START_FAILED");
            return;
        }
        if (c->scheduler->sent_total - c->scan_sent_baseline >= c->scan_budget) {
            /* Last request must finish echo and receive a response or timeout.
             * Stop before the scheduler wraps back to the first PID. */
            if (!c->tx_active && c->scheduler->pending == TBX_J1708_NO_PENDING)
                tbx_j1708_active_stop(c,"SEQUENTIAL_SCAN_COMPLETE");
            return;
        }
    }
    if (c->tx_active || now_us<c->last_rx_us || now_us-c->last_rx_us<c->idle_guard_us) return;
    bool was_stopped=c->scheduler->safety_stopped;
    tbx_j1708_request_tick(c->scheduler,(uint32_t)(now_us/1000u),send_request,c);
    if (c->config.sequential_scan &&
        c->scheduler->tx_failure_total > c->scan_failed_baseline &&
        !c->scheduler->safety_stopped) {
        c->scheduler->safety_stopped=true;
        tbx_j1708_active_stop(c,"SEQUENTIAL_TX_START_FAILED");
        event(c,"AUTO_DISARMED",c->tx_pid,"SEQUENTIAL_TX_START_FAILED");
    } else if (!was_stopped && c->scheduler->safety_stopped) {
        release_tx(c);
        event(c,"AUTO_DISARMED",c->tx_pid,"THREE_LINK_ERRORS");
    }
}

void tbx_j1708_active_note_rx(tbx_j1708_active_t *c, const uint8_t *bytes, size_t length, uint64_t now_us)
{
    if (!c || (!bytes && length)) return;
    c->now_us=now_us;
    c->last_rx_us=now_us;
    for (size_t i=0;i<length && c->tx_active;i++) {
        if (bytes[i]!=c->tx_frame[c->echo_position]) {
            ++c->collisions;
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
    c->now_us=now_us;
    event(c,"RESPONSE_CANDIDATE",pid,"MID_PID_TIME_MATCH_IS_NOT_CAUSAL_PROOF");
    return true;
}

bool tbx_j1708_active_is_tx_echo(const tbx_j1708_active_t *c, const uint8_t *frame, size_t length, uint64_t now_us)
{
    return c && frame && c->echo_position==sizeof(c->tx_frame) && length==sizeof(c->tx_frame) &&
        now_us>=c->tx_started_us && now_us-c->tx_started_us<=UINT64_C(30000) &&
        memcmp(frame,c->tx_frame,sizeof(c->tx_frame))==0;
}
