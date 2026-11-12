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
void tbx_j1708_request_production(tbx_j1708_request_scheduler_t *s,const uint32_t periods[17],bool launch,uint32_t now,bool initial)
{
    s->priority_polling=true;s->periodic=true;
    for(unsigned i=0;i<17;i++){
        tbx_j1708_request_stat_t *p=&s->pids[i];uint32_t before=p->period_ms;
        p->period_ms=periods[i]<500?500:periods[i];
        bool primary=p->pid==31 || p->pid==33;
        bool transmission=primary || p->pid==34 || p->pid==37 || p->pid==63 || p->pid==32 || p->pid==160;
        p->priority=launch && primary?0:transmission?1:2;
        if(initial){
            uint32_t offset=p->pid==31?0:p->pid==33?500:p->pid==34?1000:p->pid==63?1500:p->pid==37?2000:p->pid==32?2500:p->pid==160?3000:4000+i*500;
            p->next_due_ms=now+offset;p->retries=0;
        }else if(p->period_ms!=before){
            p->next_due_ms=p->sent?p->last_sent_ms+p->period_ms:now;
        }
    }
}
void tbx_j1708_request_init(tbx_j1708_request_scheduler_t *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    for(unsigned i=17;i<TBX_J1708_REQUEST_CAPACITY;i++){
        s->pids[i].pid=tbx_j1708_candidate_policy()[i-17].pid;
        s->pids[i].period_ms=tbx_j1708_candidate_policy()[i-17].period_ms;
    }
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
    for (uint8_t i=0; i<TBX_J1708_REQUEST_CAPACITY; i++) {
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
        tbx_j1708_request_stat_t *expired=&s->pids[s->pending];
        ++expired->timed_out;
        if(s->priority_polling && expired->retries==0){expired->next_due_ms=now+s->min_spacing_ms;expired->retries=1;}
        else if(s->priority_polling)expired->retries=0;
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
    uint8_t selected=TBX_J1708_NO_PENDING;
    if(s->priority_polling){
        for(uint8_t i=0;i<TBX_J1708_REQUEST_CAPACITY;i++){
            const tbx_j1708_request_stat_t *p=&s->pids[i];
            if(!p->enabled || !time_due(now,p->next_due_ms))continue;
            /* Aging prevents an indefinitely late secondary request from
             * being starved by a continually busy launch priority group. */
            unsigned priority=p->expedited || now-p->next_due_ms>=60000?0:p->priority;
            unsigned old=selected==TBX_J1708_NO_PENDING?255:(s->pids[selected].expedited || now-s->pids[selected].next_due_ms>=60000?0:s->pids[selected].priority);
            if(selected==TBX_J1708_NO_PENDING || priority<old || (priority==old && (int32_t)(p->next_due_ms-s->pids[selected].next_due_ms)<0))selected=i;
        }
        if(selected==TBX_J1708_NO_PENDING)return;
    }
    const uint8_t capacity=s->priority_polling?TBX_J1708_REQUEST_CAPACITY:TBX_J1708_REQUEST_COUNT;
    for (uint8_t tries=0; tries<capacity; tries++) {
        uint8_t i = s->priority_polling?selected:(uint8_t)((s->cursor + tries) % capacity);
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
            if(period<(s->priority_polling?500u:1200u))period=s->priority_polling?500u:1200u;
            uint32_t late=now-s->pids[i].next_due_ms;
            s->pids[i].next_due_ms=now+period-late%period;
        }
        s->pids[i].last_sent_ms = now;
        ++s->sent_total;
        s->pending = i;
        s->pids[i].expedited=false;
        s->pending_since_ms = now;
        s->cursor = (uint8_t)((i + 1) % capacity);
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
    tbx_j1708_request_stat_t *p=&s->pids[i];
    uint32_t latency=now-s->pending_since_ms;p->last_latency_ms=latency;
    if(!p->answered || latency<p->min_latency_ms)p->min_latency_ms=latency;
    if(latency>p->max_latency_ms)p->max_latency_ms=latency;
    p->retries=0;
    ++s->pids[i].answered;
    s->pids[i].last_answered_ms = now;
    ++s->answered_total;
    s->consecutive_timeouts = 0;
    s->pending = TBX_J1708_NO_PENDING;
    return true;
}

void tbx_j1708_request_expansion(tbx_j1708_request_scheduler_t *s,bool enabled,uint32_t now,bool initial)
{
    for(unsigned i=17;i<TBX_J1708_REQUEST_CAPACITY;i++){
        tbx_j1708_request_stat_t *p=&s->pids[i];p->enabled=enabled;
        p->priority=(p->pid==94 || p->pid==153 || p->pid==161)?1:2;
        if(initial){p->next_due_ms=now+15000+(i-17)*1500;p->retries=0;}
    }
}
bool tbx_j1708_request_expedite(tbx_j1708_request_scheduler_t *s,uint16_t pid,uint32_t now)
{
    if(!s || !s->priority_polling)return false;
    for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)if(s->pids[i].pid==pid && s->pids[i].enabled){
        if(s->pending!=i){s->pids[i].next_due_ms=now;s->pids[i].expedited=true;}
        return true;
    }
    return false;
}
