#include "../src/tbx_j1708_requests.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint16_t observed[100];
static unsigned count=0;
static bool mock_send(uint16_t pid, void *ctx) {
    (void)ctx;
    if (count >= 100) return false;
    observed[count++] = pid;
    return true;
}
static void test_order_and_replies(void) {
    const uint16_t expected[17]={26,33,34,37,62,68,117,118,235,236,250,31,32,63,108,158,160};
    tbx_j1708_request_scheduler_t s;
    tbx_j1708_request_init(&s);
    count=0;
    for (unsigned i=0; i<17; i++) assert(s.pids[i].enabled && s.pids[i].pid==expected[i]);
    tbx_j1708_request_tick(&s, 0, mock_send, NULL);
    assert(count==0);
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    for (unsigned i=0;i<17;i++) {
        unsigned t=i*250;
        tbx_j1708_request_tick(&s,t,mock_send,NULL);
        assert(count==i+1 && observed[i]==expected[i]);
        assert(tbx_j1708_request_note_response(&s,expected[i],t+50));
    }
    assert(s.sent_total==17 && s.answered_total==17 && s.timeout_total==0);
}
static void test_timeouts(void) {
    tbx_j1708_request_scheduler_t s;
    tbx_j1708_request_init(&s);
    count=0;
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    for(unsigned i=0;i<17;i++) {
        tbx_j1708_request_tick(&s,i*500,mock_send,NULL);
        tbx_j1708_request_tick(&s,i*500+451,NULL,NULL);
    }
    assert(!s.safety_stopped && s.armed && s.timeout_total==17 && s.sent_total==17);
}
static void test_delayed_brake_reply(void) {
    tbx_j1708_request_scheduler_t s;
    tbx_j1708_request_init(&s);
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    count=0;
    for(unsigned i=0;i<6;i++) {
        uint32_t t=i*250;
        tbx_j1708_request_tick(&s,t,mock_send,NULL);
        assert(tbx_j1708_request_note_response(&s,s.pids[i].pid,t+50));
    }
    tbx_j1708_request_tick(&s,1500,mock_send,NULL);
    assert(observed[6]==117);
    assert(tbx_j1708_request_note_response(&s,117,1800));
    assert(s.answered_total==7);
}
static void test_link_cutoff(void) {
    tbx_j1708_request_scheduler_t s;
    tbx_j1708_request_init(&s);
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    for(int i=0;i<3;i++) tbx_j1708_request_note_link_error(&s);
    assert(s.safety_stopped && !s.armed);
}
static bool failing_send(uint16_t pid, void *ctx) {
    (void)pid; (void)ctx;
    return false;
}
static void test_send_failure_and_immediate_stop(void) {
    tbx_j1708_request_scheduler_t s;
    tbx_j1708_request_init(&s);
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    for(unsigned i=0;i<3;i++)
        tbx_j1708_request_tick(&s,i*250,failing_send,NULL);
    assert(s.tx_failure_total==3 && s.link_error_count==3);
    assert(!s.armed && s.safety_stopped && s.sent_total==0);
    tbx_j1708_request_init(&s);
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    count=0;
    tbx_j1708_request_tick(&s,0,mock_send,NULL);
    assert(count==1 && s.pending!=TBX_J1708_NO_PENDING);
    tbx_j1708_request_permit_stationary_test(&s,false);
    assert(!s.armed && s.pending==TBX_J1708_NO_PENDING);
    tbx_j1708_request_tick(&s,1000,mock_send,NULL);
    assert(count==1 && !tbx_j1708_request_note_response(&s,26,1001));
}
static void test_uptime_wrap(void) {
    tbx_j1708_request_scheduler_t s;
    tbx_j1708_request_init(&s);
    tbx_j1708_request_permit_stationary_test(&s,true);
    assert(tbx_j1708_request_arm(&s));
    count=0;
    uint32_t now=UINT32_MAX-100u;
    tbx_j1708_request_tick(&s,now,mock_send,NULL);
    assert(count==1 && s.pending==0);
    assert(tbx_j1708_request_note_response(&s,26,now+50u));
    tbx_j1708_request_tick(&s,now+249u,mock_send,NULL);
    assert(count==1);
    tbx_j1708_request_tick(&s,now+250u,mock_send,NULL);
    assert(count==2 && s.pending==1);
    tbx_j1708_request_tick(&s,now+699u,mock_send,NULL);
    assert(s.timeout_total==0);
    tbx_j1708_request_tick(&s,now+700u,mock_send,NULL);
    assert(s.timeout_total==1);
}
static uint32_t cadence_now,last_transmit;static unsigned cadence_count,by_pid[17];
static bool cadence_send(uint16_t pid,void *context)
{
    tbx_j1708_request_scheduler_t *s=context;unsigned i=0;while(i<17 && s->pids[i].pid!=pid)++i;assert(i<17);
    if(cadence_count)assert((uint32_t)(cadence_now-last_transmit)>=1200);
    ++cadence_count;++by_pid[i];last_transmit=cadence_now;return true;
}
static void test_individual_cadence_configurable_and_wrap(void)
{
    tbx_j1708_request_scheduler_t s;tbx_j1708_request_init(&s);
    for(unsigned i=0;i<17;i++){
        uint16_t pid=s.pids[i].pid;
        uint32_t period=pid==235 || pid==236?900000:pid==108 || pid==250?300000:60000;
        assert(s.pids[i].period_ms==period);
    }
    s.pids[0].period_ms=120000; /* A persisted per-PID override. */
    tbx_j1708_request_permit_stationary_test(&s,true);assert(tbx_j1708_request_arm(&s));
    tbx_j1708_request_set_spacing(&s,1200);
    uint32_t start=UINT32_MAX-10000;tbx_j1708_request_start_periodic(&s,start);
    cadence_count=0;memset(by_pid,0,sizeof(by_pid));
    for(uint32_t elapsed=0;elapsed<180000;elapsed+=100){
        cadence_now=start+elapsed;tbx_j1708_request_tick(&s,cadence_now,cadence_send,&s);
        if(s.pending<17)assert(tbx_j1708_request_note_response(&s,s.pids[s.pending].pid,cadence_now+1));
    }
    for(unsigned i=0;i<17;i++){
        uint32_t phase=i*60000u/17u;
        unsigned expected=(180000u-1-phase)/s.pids[i].period_ms+1;
        assert(by_pid[i]==expected);
    }
    tbx_j1708_request_stop(&s);unsigned before=cadence_count;
    tbx_j1708_request_tick(&s,start+200000,cadence_send,&s);assert(cadence_count==before);
}

int main(void) {
    test_individual_cadence_configurable_and_wrap();
    test_order_and_replies();
    test_timeouts();
    test_link_cutoff();
    test_delayed_brake_reply();
    test_send_failure_and_immediate_stop();
    test_uptime_wrap();
    puts("OK: 17 PIDs, authorization, spacing, replies and cutoffs");
    return 0;
}
