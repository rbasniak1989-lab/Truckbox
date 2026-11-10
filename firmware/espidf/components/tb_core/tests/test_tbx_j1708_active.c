/* Synthetic bus callbacks exercise the production controller and scheduler.
 * MID values here are test data; firmware supplies no default vehicle MID. */
#include "tbx_j1708_active.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    tbx_j1708_request_scheduler_t scheduler;
    tbx_j1708_active_t active;
    tbx_j1708_active_config_t config;
    unsigned sends, aborts, requests, responses, timeouts, disarms, stops, errors;
    bool backend_reject;
    uint8_t sent_frame[5];
    const char *last_event, *last_detail;
} fixture_t;
static const uint16_t expected_pids[17] = {26,33,34,37,62,68,117,118,235,236,250,31,32,63,108,158,160};
static bool begin_tx(const uint8_t *frame, size_t length, void *context)
{
    fixture_t *f = context;
    assert(length == sizeof(f->sent_frame));
    ++f->sends;
    memcpy(f->sent_frame, frame, length);
    return !f->backend_reject;
}
static void abort_tx(void *context) { ++((fixture_t *)context)->aborts; }
static void event(const char *name, uint16_t pid, const char *detail, void *context)
{
    fixture_t *f = context;
    (void)pid;
    f->last_event = name; f->last_detail = detail;
    if (!strcmp(name,"REQUEST")) ++f->requests;
    if (!strcmp(name,"RESPONSE_CANDIDATE")) ++f->responses;
    if (!strcmp(name,"TIMEOUT")) ++f->timeouts;
    if (!strcmp(name,"AUTO_DISARMED")) ++f->disarms;
    if (!strcmp(name,"STOPPED")) ++f->stops;
    if (!strcmp(name,"LINK_ERROR") || !strcmp(name,"TX_ERROR")) ++f->errors;
}
static void initialize(fixture_t *f)
{
    memset(f,0,sizeof(*f));
    tbx_j1708_active_ops_t ops = {.begin_tx=begin_tx,.abort_tx=abort_tx,.event=event};
    tbx_j1708_active_init(&f->active,&f->scheduler,&ops,f);
    f->config.local_mid=250; /* Synthetic only; not a firmware default. */
    f->config.priority=8;
    f->config.inverted_de21_attested=true;
    for (unsigned i=0;i<17;i++) f->config.target_mid[i]=(uint8_t)(128u+i%4u);
}
static void configure_and_arm(fixture_t *f, uint64_t now)
{
    assert(tbx_j1708_active_configure(&f->active,&f->config));
    assert(tbx_j1708_active_arm(&f->active,true,true,now));
}
static void echo(fixture_t *f, uint64_t start)
{
    assert(f->active.tx_active);
    /* Echo arrives in chunks, as the UART driver may deliver it. */
    tbx_j1708_active_note_rx(&f->active,f->sent_frame,1,start+1100);
    assert(f->active.tx_active && f->active.echo_position==1);
    tbx_j1708_active_note_rx(&f->active,f->sent_frame+1,2,start+3300);
    assert(f->active.tx_active && f->active.echo_position==3);
    tbx_j1708_active_note_rx(&f->active,f->sent_frame+3,2,start+5500);
    assert(!f->active.tx_active && f->active.echo_position==5);
}
static void check_frame(const fixture_t *f, unsigned index)
{
    assert(f->sent_frame[0]==250 && f->sent_frame[1]==128);
    assert(f->sent_frame[2]==expected_pids[index]);
    assert(f->sent_frame[3]==f->config.target_mid[index]);
    unsigned sum=0;
    for(unsigned i=0;i<5;i++) sum+=f->sent_frame[i];
    assert((sum&255u)==0);
}
static void test_no_tx_by_default_and_configuration_validation(void)
{
    fixture_t f; initialize(&f);
    assert(!f.active.configured && !f.scheduler.armed && !f.scheduler.test_permitted);
    for(unsigned i=0;i<17;i++) assert(f.scheduler.pids[i].pid==expected_pids[i] && f.scheduler.pids[i].enabled);
    for(unsigned i=0;i<1000;i++) {
        const uint8_t passive[]={128,84,100,200};
        tbx_j1708_active_note_rx(&f.active,passive,sizeof(passive),(uint64_t)i*10000);
        tbx_j1708_active_tick(&f.active,(uint64_t)i*10000);
    }
    assert(f.sends==0);
    assert(!tbx_j1708_active_arm(&f.active,true,true,10000000));
    tbx_j1708_active_config_t bad=f.config;
    bad.local_mid=127; assert(!tbx_j1708_active_configure(&f.active,&bad));
    bad=f.config; bad.priority=0; assert(!tbx_j1708_active_configure(&f.active,&bad));
    bad.priority=9; assert(!tbx_j1708_active_configure(&f.active,&bad));
    bad=f.config; bad.inverted_de21_attested=false; assert(!tbx_j1708_active_configure(&f.active,&bad));
    for(unsigned i=0;i<17;i++) {
        bad=f.config; bad.target_mid[i]=0; assert(!tbx_j1708_active_configure(&f.active,&bad));
        bad.target_mid[i]=250; assert(!tbx_j1708_active_configure(&f.active,&bad));
    }
    assert(tbx_j1708_active_configure(&f.active,&f.config));
    assert(f.active.idle_guard_us>=5000);
    assert(!tbx_j1708_active_arm(&f.active,false,true,10000000));
    assert(!tbx_j1708_active_arm(&f.active,true,false,10000000));
    assert(!f.scheduler.armed && !f.scheduler.test_permitted && f.sends==0);
    tbx_j1708_active_tick(&f.active,20000000); assert(f.sends==0);
}
static void test_all_17_framing_echo_matching_spacing(void)
{
    fixture_t f; initialize(&f);
    uint64_t sent=1005000;
    configure_and_arm(&f,1000000);
    assert(!tbx_j1708_active_configure(&f.active,&f.config));
    tbx_j1708_active_tick(&f.active,sent-1); assert(f.sends==0);
    for(unsigned i=0;i<17;i++) {
        tbx_j1708_active_tick(&f.active,sent);
        assert(f.sends==i+1 && f.scheduler.sent_total==i+1 && f.active.tx_active);
        check_frame(&f,i);
        assert(!tbx_j1708_active_note_response(&f.active,f.config.target_mid[i],expected_pids[i],sent+1000));
        echo(&f,sent);
        assert(tbx_j1708_active_is_tx_echo(&f.active,f.sent_frame,5,sent+5500));
        assert(!tbx_j1708_active_is_tx_echo(&f.active,f.sent_frame,4,sent+5500));
        assert(!tbx_j1708_active_is_tx_echo(&f.active,f.sent_frame,5,sent+30001));
        assert(!tbx_j1708_active_note_response(&f.active,249,expected_pids[i],sent+100000));
        assert(!tbx_j1708_active_note_response(&f.active,f.config.target_mid[i],999,sent+100000));
        assert(tbx_j1708_active_note_response(&f.active,f.config.target_mid[i],expected_pids[i],sent+200000));
        assert(!strcmp(f.last_detail,"MID_PID_TIME_MATCH_IS_NOT_CAUSAL_PROOF"));
        assert(!tbx_j1708_active_note_response(&f.active,f.config.target_mid[i],expected_pids[i],sent+210000));
        tbx_j1708_active_tick(&f.active,sent+249999); assert(f.sends==i+1);
        sent+=250000;
    }
    assert(f.requests==17 && f.responses==17 && f.active.echo_ok==17);
    for(unsigned i=0;i<17;i++) assert(f.scheduler.pids[i].sent==1 && f.scheduler.pids[i].answered==1);
    tbx_j1708_active_stop(&f.active,"OPERATOR_STOP");
    assert(!f.scheduler.armed && !f.scheduler.test_permitted && f.scheduler.pending==TBX_J1708_NO_PENDING);
    unsigned before=f.sends;
    tbx_j1708_active_tick(&f.active,sent+1000000); assert(f.sends==before);
}
static void test_lease_renewal_expiry_and_stop_release(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    tbx_j1708_active_tick(&f.active,1005000); echo(&f,1005000);
    uint8_t pending=f.scheduler.pending;
    uint32_t spacing=f.scheduler.next_tx_ms;
    assert(tbx_j1708_active_arm(&f.active,true,true,1100000));
    assert(f.scheduler.pending==pending && f.scheduler.next_tx_ms==spacing);
    assert(f.active.lease_deadline_us==1100000+TBX_J1708_ACTIVE_LEASE_US);
    tbx_j1708_active_tick(&f.active,f.active.lease_deadline_us);
    assert(!f.scheduler.armed && !f.active.tx_active && f.active.lease_expirations==1);
    assert(!strcmp(f.last_detail,"AUTHORIZATION_LEASE_EXPIRED"));
    assert(tbx_j1708_active_arm(&f.active,true,true,70000000));
    tbx_j1708_active_tick(&f.active,70005000); assert(f.active.tx_active);
    unsigned aborts=f.aborts;
    tbx_j1708_active_stop(&f.active,"IMMEDIATE_OPERATOR_STOP");
    assert(f.aborts==aborts+1 && !f.active.tx_active && !f.scheduler.armed);
    assert(f.scheduler.pending==TBX_J1708_NO_PENDING);
    assert(!tbx_j1708_active_arm(&f.active,false,true,70006000));
}
static void test_collision_release_and_three_error_stop(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    uint64_t sent=1005000;
    for(unsigned i=0;i<3;i++) {
        tbx_j1708_active_tick(&f.active,sent); assert(f.active.tx_active);
        unsigned aborts=f.aborts;
        uint8_t wrong=(uint8_t)(f.sent_frame[0]^1u);
        tbx_j1708_active_note_rx(&f.active,&wrong,1,sent+1100);
        assert(f.aborts==aborts+1 && !f.active.tx_active && f.active.collisions==i+1);
        assert(f.scheduler.pending==TBX_J1708_NO_PENDING);
        sent+=250000;
    }
    assert(f.scheduler.safety_stopped && !f.scheduler.armed && f.disarms==1);
    assert(f.scheduler.link_error_count==3 && f.scheduler.timeout_total==0);
    assert(!tbx_j1708_active_arm(&f.active,true,true,sent));
    tbx_j1708_active_tick(&f.active,sent+1000000); assert(f.sends==3);
    /* Passive RX remains callable after safety stop. */
    const uint8_t rx[]={128,84,50,150};
    tbx_j1708_active_note_rx(&f.active,rx,sizeof(rx),sent+1000000);
    assert(f.active.last_rx_us==sent+1000000 && f.sends==3);
}
static void test_echo_timeout_and_backend_failure(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    tbx_j1708_active_tick(&f.active,1005000); assert(f.active.tx_active);
    tbx_j1708_active_tick(&f.active,1024999); assert(f.active.tx_active);
    tbx_j1708_active_tick(&f.active,1025000);
    assert(!f.active.tx_active && f.active.echo_timeouts==1 && f.scheduler.link_error_count==1);
    assert(f.scheduler.pending==TBX_J1708_NO_PENDING && f.scheduler.timeout_total==0);
    initialize(&f); f.backend_reject=true; configure_and_arm(&f,1000000);
    for(unsigned i=0;i<3;i++) tbx_j1708_active_tick(&f.active,1005000+(uint64_t)i*250000);
    assert(f.sends==3 && f.requests==0 && f.scheduler.tx_failure_total==3);
    assert(f.scheduler.link_error_count==3 && f.scheduler.safety_stopped && !f.scheduler.armed && !f.active.tx_active);
    assert(f.disarms==1);
}
static void test_response_timeouts_even_when_bus_busy(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    tbx_j1708_active_tick(&f.active,1005000); echo(&f,1005000);
    const uint8_t unrelated=0x80;
    tbx_j1708_active_note_rx(&f.active,&unrelated,1,1455000);
    tbx_j1708_active_tick(&f.active,1455000);
    assert(f.scheduler.timeout_total==1 && f.timeouts==1 && f.sends==1);
    assert(f.scheduler.tx_failure_total==0 && f.scheduler.link_error_count==0);
    /* Each next attempt gets a complete echo but no matching parameter. */
    uint64_t sent=1460000;
    for(unsigned i=1;i<8;i++) {
        tbx_j1708_active_tick(&f.active,sent); assert(f.active.tx_active);
        echo(&f,sent);
        tbx_j1708_active_note_rx(&f.active,&unrelated,1,sent+450000);
        tbx_j1708_active_tick(&f.active,sent+450000);
        sent+=455000;
    }
    assert(f.scheduler.timeout_total==8 && f.timeouts==8 && f.scheduler.safety_stopped && !f.scheduler.armed);
    assert(f.scheduler.sent_total==8 && f.disarms==1 && !f.active.tx_active);
}
static void test_idle_deferral_and_millisecond_wrap(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    tbx_j1708_active_note_rx(&f.active,NULL,0,1005000);
    tbx_j1708_active_tick(&f.active,1009999); assert(f.sends==0);
    tbx_j1708_active_tick(&f.active,1010000); assert(f.sends==1);
    initialize(&f);
    const uint64_t arm=(uint64_t)(UINT32_MAX-100u)*1000u;
    configure_and_arm(&f,arm);
    uint64_t sent=arm+5000;
    tbx_j1708_active_tick(&f.active,sent); assert(f.sends==1);
    echo(&f,sent);
    assert(tbx_j1708_active_note_response(&f.active,f.config.target_mid[0],26,sent+200000));
    tbx_j1708_active_tick(&f.active,sent+249999); assert(f.sends==1);
    tbx_j1708_active_tick(&f.active,sent+250000); assert(f.sends==2);
    check_frame(&f,1);
}
static void test_response_lower_bound_and_expired_lease(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    tbx_j1708_active_tick(&f.active,1005000); echo(&f,1005000);
    assert(!tbx_j1708_active_note_response(&f.active,f.config.target_mid[0],26,1004999));
    assert(f.scheduler.pending==0 && f.responses==0);
    /* The response entry point must enforce lease expiry even before tick. */
    f.active.lease_deadline_us=1100000;
    assert(!tbx_j1708_active_note_response(&f.active,f.config.target_mid[0],26,1100000));
    assert(!f.scheduler.armed && !f.active.tx_active);
}

static void test_work_mode_requires_actual_parked_echo_and_response(void)
{
    fixture_t f; initialize(&f);
    assert(!tbx_j1708_active_enable_work(&f.active,1000000));
    configure_and_arm(&f,1000000);
    assert(!tbx_j1708_active_enable_work(&f.active,1000001));
    uint64_t now=1005000;
    for (unsigned i=0;i<3;i++) {
        tbx_j1708_active_tick(&f.active,now);
        assert(f.active.tx_active);
        assert(!tbx_j1708_active_enable_work(&f.active,now));
        echo(&f,now);
        /* Before three valid echoes, even matching responses cannot enable work. */
        if (i < 2) assert(!tbx_j1708_active_enable_work(&f.active,now+6000));
        assert(tbx_j1708_active_note_response(&f.active,
            f.config.target_mid[i],expected_pids[i],now+200000));
        if (i < 2) assert(!tbx_j1708_active_enable_work(&f.active,now+201000));
        now+=250000;
    }
    const uint64_t authorized_at=now+1000;
    assert(tbx_j1708_active_enable_work(&f.active,authorized_at));
    assert(f.active.work_mode);
    assert(f.scheduler.armed && f.scheduler.min_spacing_ms>=TBX_J1708_WORK_MIN_SPACING_MS);
    assert(f.active.lease_deadline_us==authorized_at+TBX_J1708_WORK_LEASE_US);
    assert(!tbx_j1708_active_enable_work(&f.active,authorized_at));
    /* Permission is now based on physical TX evidence, not vehicle speed. */
    assert(tbx_j1708_active_arm(&f.active,true,false,authorized_at+1000));
    assert(f.active.work_mode);
    assert(f.active.lease_deadline_us==authorized_at+1000+TBX_J1708_WORK_LEASE_US);
    const unsigned sent_before=f.sends;
    const uint64_t last_sent=now-250000;
    tbx_j1708_active_tick(&f.active,last_sent+499000);
    assert(f.sends==sent_before); /* no 250 ms burst after promotion */
    tbx_j1708_active_tick(&f.active,last_sent+500000);
    assert(f.sends==sent_before+1);
    tbx_j1708_active_stop(&f.active,"OPERATOR_STOP");
    assert(!f.active.work_mode);
    assert(f.scheduler.min_spacing_ms==250);
    assert(!tbx_j1708_active_arm(&f.active,true,false,authorized_at+2000));
}

static void test_work_mode_auto_disarms_and_must_revalidate(void)
{
    fixture_t f; initialize(&f); configure_and_arm(&f,1000000);
    /* Demonstrate a passed experimental trial, still with synthetic bus. */
    uint64_t now=1005000;
    for(unsigned i=0;i<3;i++) {
        tbx_j1708_active_tick(&f.active,now);
        echo(&f,now);
        assert(tbx_j1708_active_note_response(&f.active,
            f.config.target_mid[i],expected_pids[i],now+200000));
        now+=250000;
    }
    assert(tbx_j1708_active_enable_work(&f.active,now+1000));
    unsigned before=f.sends;
    tbx_j1708_active_tick(&f.active,f.active.lease_deadline_us);
    assert(!f.active.work_mode && !f.scheduler.armed && !f.active.tx_active);
    tbx_j1708_active_tick(&f.active,now+TBX_J1708_WORK_LEASE_US+1000000);
    assert(f.sends==before && !f.scheduler.armed);
}


static void test_sequential_scan_resumes_and_finishes_without_repeating(void)
{
    fixture_t f; initialize(&f);
    f.config.sequential_scan=true;
    f.config.start_pid=117; /* Resume after the six real packets of boot126. */
    configure_and_arm(&f,1000000);
    assert(f.active.scan_budget==11 && f.active.scan_start_index==6);
    assert(f.scheduler.cursor==6 && f.scheduler.min_spacing_ms>=1200);
    assert(!tbx_j1708_active_enable_work(&f.active,1001000));
    uint64_t now=1005000;
    for (unsigned i=6;i<17;i++) {
        tbx_j1708_active_tick(&f.active,now);
        assert(f.sends==i-5 && f.scheduler.sent_total==i-5);
        assert(f.scheduler.pending==i);
        check_frame(&f,i);
        tbx_j1708_active_tick(&f.active,now+500);
        assert(f.sends==i-5); /* Never overlap before echo. */
        echo(&f,now);
        assert(!f.active.tx_active);
        if((i&1u)==0) {
            assert(tbx_j1708_active_note_response(&f.active,
                f.config.target_mid[i],expected_pids[i],now+200000));
            tbx_j1708_active_tick(&f.active,now+200001);
        } else {
            tbx_j1708_active_tick(&f.active,now+450001);
            assert(f.scheduler.pids[i].timed_out==1);
        }
        assert(f.sends==i-5); /* No early second packet after one response. */
        if(i!=16) {
            assert(f.scheduler.armed);
            tbx_j1708_active_tick(&f.active,now+1199999);
            assert(f.sends==i-5); /* Enforce 1200ms between requests. */
            now+=1200000;
        }
    }
    assert(f.sends==11 && f.scheduler.sent_total==11);
    assert(!f.scheduler.armed && !f.scheduler.safety_stopped);
    assert(!f.active.work_mode && !f.active.tx_active);
    assert(f.stops>=1 && !strcmp(f.last_detail,"SEQUENTIAL_SCAN_COMPLETE"));
    for(unsigned i=0;i<6;i++) assert(f.scheduler.pids[i].sent==0);
    for(unsigned i=6;i<17;i++) assert(f.scheduler.pids[i].sent==1);
    tbx_j1708_active_tick(&f.active,now+5000000);
    assert(f.sends==11); /* No wrap to PID26 or repeated scan. */
}
static void test_sequential_scan_blocks_first_echo_collision(void)
{
    fixture_t f; initialize(&f);
    f.config.sequential_scan=true;
    f.config.start_pid=117;
    configure_and_arm(&f,1000000);
    tbx_j1708_active_tick(&f.active,1005000);
    assert(f.sends==1 && f.active.tx_active);
    const uint8_t wrong=(uint8_t)(f.sent_frame[0]^1u);
    tbx_j1708_active_note_rx(&f.active,&wrong,1,1006100);
    assert(f.active.collisions==1);
    assert(f.scheduler.safety_stopped && !f.scheduler.armed);
    assert(!f.active.tx_active && f.disarms==1);
    assert(!strcmp(f.last_detail,"SEQUENTIAL_TX_LINK_ERROR"));
    tbx_j1708_active_tick(&f.active,2000000);
    assert(f.sends==1);
    assert(!tbx_j1708_active_arm(&f.active,true,true,2001000));
}
static void test_sequential_unknown_start_is_rejected(void)
{
    fixture_t f; initialize(&f);
    f.config.sequential_scan=true;
    f.config.start_pid=99;
    assert(!tbx_j1708_active_configure(&f.active,&f.config));
    f.config.start_pid=0;
    assert(tbx_j1708_active_configure(&f.active,&f.config));
    assert(f.active.scan_budget==17 && f.active.scan_start_index==0);
}

int main(void)
{
    test_no_tx_by_default_and_configuration_validation();
    test_all_17_framing_echo_matching_spacing();
    test_lease_renewal_expiry_and_stop_release();
    test_collision_release_and_three_error_stop();
    test_echo_timeout_and_backend_failure();
    test_response_timeouts_even_when_bus_busy();
    test_idle_deferral_and_millisecond_wrap();
    test_response_lower_bound_and_expired_lease();
    test_work_mode_requires_actual_parked_echo_and_response();
    test_work_mode_auto_disarms_and_must_revalidate();
    test_sequential_scan_resumes_and_finishes_without_repeating();
    test_sequential_scan_blocks_first_echo_collision();
    test_sequential_unknown_start_is_rejected();
    puts("J1708 active: 17 targeted PID128 frames, no TX default, explicit MIDs, idle/spacing, echo/collision, lease, MID/PID timing, errors/timeouts, wrap and immediate stop passed");
    return 0;
}
