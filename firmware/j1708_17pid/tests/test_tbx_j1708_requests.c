#include "../src/tbx_j1708_requests.h"
#include <assert.h>
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
    for(unsigned i=0;i<8;i++) {
        tbx_j1708_request_tick(&s,i*500,mock_send,NULL);
        tbx_j1708_request_tick(&s,i*500+451,mock_send,NULL);
    }
    assert(s.safety_stopped && !s.armed && s.timeout_total==8);
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
int main(void) {
    test_order_and_replies();
    test_timeouts();
    test_link_cutoff();
    test_delayed_brake_reply();
    puts("OK: 17 PIDs, authorization, spacing, replies and cutoffs");
    return 0;
}
