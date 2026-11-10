#include "tbx_j1708_requests.h"
#include <string.h>
#include "tbx_j1708_policy.h"
/* Owner's chosen 17 J1587 PIDs. Every entry is enabled from initialization. */
static const uint16_t selected_pids[TBX_J1708_REQUEST_COUNT] = {
    26, 33, 34, 37, 62, 68, 117, 118, 235,
    236, 250, 31, 32, 63, 108, 158, 160
};
static bool time_due(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}
uint32_t tbx_j1708_request_default_period(uint16_t pid)
{
    for(unsigned i=0;i<17;i++)if(tbx_j1708_policy()[i].pid==pid)return tbx_j1708_policy()[i].period_ms;
    return 0;
}
void tbx_j1708_request_start_periodic(tbx_j1708_request_scheduler_t *s,uint32_t now)
{
    if(!s)return;
    s->periodic=true;s->cursor=0;
    for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++)
        s->pids[i].next_due_ms=now+(uint32_t)(i*60000u/TBX_J1708_REQUEST_COUNT);
}
void tbx_j1708_request_init(tbx_j1708_request_scheduler_t *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->pending = TBX_J1708_NO_PENDING;
    s->min_spacing_ms = 250; /* maximum 4 requests/s; waits can slow it */
    s->timeout_ms = 450; /* MID 140 brake sensors had ~205-210 ms mean reply latency in field capture */
    for (uint8_t i=0; i<TBX_J1708_REQUEST_COUNT; ++i) {
        s->pids[i].pid = selected_pids[i];
        s->pids[i].enabled = true;
        s->pids[i].period_ms=tbx_j1708_request_default_period(selected_pids[i]);
    }
}
void tbx_j1708_request_stop(tbx_j1708_request_scheduler_t *s) {
    if (!s) return;
    s->armed = false;
    s->pending = TBX_J1708_NO_PENDING;
}
void tbx_j1708_request_permit_stationary_test(tbx_j1708_request_scheduler_t *s, bool permitted) {
    if (!s) return;
    s->test_permitted = permitted;
    if (!permitted) tbx_j1708_request_stop(s);
}
bool tbx_j1708_request_arm(tbx_j1708_request_scheduler_t *s) {
    if (!s || !s->test_permitted || s->safety_stopped) return false;
    if (s->armed) return true;
    s->armed = true;
    s->next_tx_valid = false;
    return true;
}
bool tbx_j1708_request_enable_pid(tbx_j1708_request_scheduler_t *s, uint16_t pid, bool enabled) {
    if (!s) return false;
    for (uint8_t i=0; i<TBX_J1708_REQUEST_COUNT; i++) {
        if (s->pids[i].pid == pid) {
            s->pids[i].enabled = enabled;
            if (!enabled && s->pending == i) s->pending = TBX_J1708_NO_PENDING;
            return true;
        }
    }
    return false;
}
void tbx_j1708_request_set_spacing(tbx_j1708_request_scheduler_t *s, uint32_t ms) {
    if (s) s->min_spacing_ms = ms < 100 ? 100 : ms;
}
void tbx_j1708_request_note_link_error(tbx_j1708_request_scheduler_t *s) {
    if (!s) return;
    if (s->link_error_count < UINT8_MAX) ++s->link_error_count;
    if (s->link_error_count >= 3) {
        s->safety_stopped = true;
        tbx_j1708_request_stop(s);
    }
}
void tbx_j1708_request_tick(tbx_j1708_request_scheduler_t *s, uint32_t now,
                            tbx_j1708_send_request_fn send, void *ctx) {
    if (!s || !s->armed || !s->test_permitted || s->safety_stopped) return;
    if (s->pending != TBX_J1708_NO_PENDING) {
        if (!time_due(now, s->pending_since_ms + s->timeout_ms)) return;
        ++s->pids[s->pending].timed_out;
        ++s->timeout_total;
        s->pending = TBX_J1708_NO_PENDING;
        if (s->consecutive_timeouts < UINT8_MAX) ++s->consecutive_timeouts;
        /* A silent/unsupported PID is not a failed transmitter. Continue the
         * bounded scan; physical TX errors and the authorization lease retain
         * independent cutoffs. Never let one absent ECU hide the other PIDs. */
    }
    /* A NULL sender permits timeout accounting while transmission is deferred
     * by the bus-access controller. No send or link error is then generated. */
    if (!send) return;
    if (s->next_tx_valid && !time_due(now, s->next_tx_ms)) return;
    for (uint8_t tries=0; tries<TBX_J1708_REQUEST_COUNT; tries++) {
        uint8_t i = (uint8_t)((s->cursor + tries) % TBX_J1708_REQUEST_COUNT);
        if (!s->pids[i].enabled) continue;
        if(s->periodic && !time_due(now,s->pids[i].next_due_ms))continue;
        s->send_deferred = false;
        if (!send(s->pids[i].pid, ctx)) {
            if (s->send_deferred) {
                ++s->deferred_total;
                return; /* No UART bytes accepted, no cursor/deadline change. */
            }
            ++s->tx_failure_total;
            tbx_j1708_request_note_link_error(s);
            s->next_tx_ms = now + s->min_spacing_ms;
            s->next_tx_valid = true;
            return;
        }
        ++s->pids[i].sent;
        if(s->periodic){
            uint32_t period=s->pids[i].period_ms;
            if(period<1200)period=1200;
            uint32_t late=now-s->pids[i].next_due_ms;
            s->pids[i].next_due_ms=now+period-late%period;
        }
        s->pids[i].last_sent_ms = now;
        ++s->sent_total;
        s->pending = i;
        s->pending_since_ms = now;
        s->cursor = (uint8_t)((i + 1) % TBX_J1708_REQUEST_COUNT);
        s->next_tx_ms = now + s->min_spacing_ms;
        s->next_tx_valid = true;
        return;
    }
}
bool tbx_j1708_request_note_response(tbx_j1708_request_scheduler_t *s, uint16_t pid, uint32_t now) {
    if (!s || !s->armed || s->pending == TBX_J1708_NO_PENDING) return false;
    uint8_t i = s->pending;
    if (s->pids[i].pid != pid) return false;
    if (time_due(now, s->pending_since_ms + s->timeout_ms)) return false;
    ++s->pids[i].answered;
    s->pids[i].last_answered_ms = now;
    ++s->answered_total;
    s->consecutive_timeouts = 0;
    s->pending = TBX_J1708_NO_PENDING;
    return true;
}
