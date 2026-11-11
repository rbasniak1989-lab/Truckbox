/* Synthetic clock/bus and labelled reconstruction compatible with HEALTH
 * aggregates. This is NOT a replay of unavailable 10 s field counters. */
#include "tbx_j1708_active.h"
#include "tbx_j1708_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fixtures/healthy_0214.h"
typedef struct {
    tbx_j1708_active_t a;tbx_j1708_request_scheduler_t s;
    uint64_t now;unsigned writes,aborts,decisions,stops,credit,upload_ok,upload_failed;
    bool no_reply,driver_fault,wifi_online;
    uint8_t tx[5];
}fixture_t;
static tbx_j1708_tx_result_t tx(const uint8_t *data,size_t n,void *context)
{
    fixture_t *f=context;assert(n==5);if(f->driver_fault)return TBX_J1708_TX_FAULT;
    unsigned sum=0;for(unsigned i=0;i<n;i++)sum+=data[i];assert(!(sum&255));
    memcpy(f->tx,data,n);++f->writes;return TBX_J1708_TX_STARTED;
}
static void abort_tx(void *ctx){++((fixture_t *)ctx)->aborts;}
static void event(const char *name,uint16_t pid,const char *detail,void *ctx)
{
    fixture_t *f=ctx;(void)pid;(void)detail;
    if(!strcmp(name,"GUARD_DECISION"))++f->decisions;
    if(!strcmp(name,"STOPPED"))++f->stops;
}
static void initialize(fixture_t *f)
{
    memset(f,0,sizeof(*f));f->now=1000000;f->wifi_online=true;
    const tbx_j1708_active_ops_t ops={tx,abort_tx,event};tbx_j1708_active_init(&f->a,&f->s,&ops,f);
    tbx_j1708_active_config_t cfg={.local_mid=172,.priority=8,.inverted_de21_attested=true};
    for(unsigned i=0;i<17;i++)cfg.target_mid[i]=tbx_j1708_policy()[i].mid;
    assert(tbx_j1708_active_configure(&f->a,&cfg));
    /* Authorization boundary fixture; full parked validation is tested by run_j1708_field. */
    f->a.field.phase=TBX_FIELD_CONFIRMED;f->a.field.confirmed=true;f->a.field.started_us=1;
    f->a.field.before_frames=1916;f->a.field.before_duration_us=30000787;
    f->a.echo_ok=1;f->s.answered_total=1;f->a.promotion_deadline_us=f->now+900000000;
}
static void frame(fixture_t *f,tbx_rx_kind_t kind)
{
    tbx_j1708_field_note_frame_kind(&f->a,128,kind,f->now);
}
static void step(fixture_t *f,unsigned fps)
{
    f->now+=10000;f->credit+=fps;
    while(f->credit>=100){frame(f,TBX_RX_VALID);f->credit-=100;}
    if(f->a.tx_active)tbx_j1708_active_note_rx(&f->a,f->tx,5,f->now);
    if(!f->no_reply && f->s.pending<17 && !f->a.tx_active && f->now-f->a.tx_started_us>=210000){
        unsigned i=f->s.pending;frame(f,TBX_RX_VALID);
        assert(tbx_j1708_active_note_response(&f->a,f->a.config.target_mid[i],f->s.pids[i].pid,f->now));
    }
    if(f->now%1000000==0){if(f->wifi_online)++f->upload_ok;else ++f->upload_failed;}
    tbx_j1708_active_tick(&f->a,f->now);
}
static void seconds(fixture_t *f,unsigned seconds,unsigned fps)
{
    for(unsigned i=0;i<seconds*100;i++)step(f,fps);
}
static void warm_and_enable(fixture_t *f)
{
    initialize(f);seconds(f,31,60);assert(f->a.rx_guard.reference_count==3);
    assert(tbx_j1708_active_enable_work(&f->a,f->now));assert(!f->a.physical_validated);
}
static void normal_conditions(void)
{
    fixture_t f;warm_and_enable(&f);seconds(&f,120,60);
    assert(f.s.armed && f.a.work_mode && f.s.sent_total>17 && !f.a.physical_validated);
    /* B: isolated errors, including a >1% burst resembling the old guard trigger. */
    for(unsigned n=0;n<6;n++)frame(&f,TBX_RX_CHECKSUM);
    seconds(&f,10,59);assert(f.s.armed);seconds(&f,40,60);assert(f.s.armed);
    /* C/F: engine off -> engine running -> moving, ordinary traffic changes.
     * No electrical claim or assumption that the calibrated frame rate is fixed. */
    seconds(&f,40,5);seconds(&f,40,120);seconds(&f,40,25);assert(f.s.armed && f.a.work_mode);
    /* G: connectivity is deliberately outside the controller. Time/RX continue. */
    f.wifi_online=false;seconds(&f,30,60);f.wifi_online=true;seconds(&f,30,60);
    assert(f.s.armed && f.a.work_mode && f.upload_failed && f.upload_ok);
    /* J: historical calibration changes cannot enter the road guard decision. */
    f.a.field.before_frames=1;f.a.field.before_duration_us=30000000;
    seconds(&f,40,60);assert(f.s.armed);
    f.a.field.before_frames=UINT32_MAX;seconds(&f,40,60);assert(f.s.armed);
    /* H: reset returns to a disarmed state and never restores authority. */
    fixture_t reboot;initialize(&reboot);assert(!reboot.s.armed && !reboot.a.work_mode);
    seconds(&reboot,40,60);assert(!reboot.s.armed && !reboot.writes);
}
static void persistent_errors_and_faults(void)
{
    fixture_t f;warm_and_enable(&f);
    for(unsigned window=0;window<4 && f.s.armed;window++){
        for(unsigned i=0;i<40;i++)frame(&f,TBX_RX_CHECKSUM);
        seconds(&f,10,60);
    }
    assert(!f.s.armed && f.a.rx_guard.fault_latched);
    assert(f.a.rx_guard.decision.predicates&TBX_GUARD_PERSISTENT);
    assert(f.a.rx_guard.decision.streak==3 && f.a.rx_guard.decision.stopped);
    assert((f.a.rx_guard.decision.before_flags&3)==3 && !f.a.rx_guard.decision.after_flags);
    unsigned writes=f.writes;seconds(&f,60,60);assert(f.writes==writes && !f.s.armed);
    assert(!tbx_j1708_active_arm(&f.a,true,true,f.now));
    /* D: RX faults do not invent a collision/TX fault, but loss of RX during TX releases immediately. */
    warm_and_enable(&f);while(!f.a.tx_active)step(&f,60);
    tbx_j1708_rx_uart_fault(&f.a,TBX_RX_OVERRUN,f.now);
    assert(!f.a.tx_active && !f.s.armed && f.aborts && f.a.rx_guard.total.overruns==1);
    assert(f.a.collisions==0 && f.s.tx_failure_total==0 && f.a.rx_guard.decision.cause==TBX_GUARD_RX_DURING_TX);
    warm_and_enable(&f);while(!f.a.tx_active)step(&f,60);
    uint8_t wrong=0;tbx_j1708_active_note_rx(&f.a,&wrong,1,f.now+1000);
    assert(!f.a.tx_active && f.a.collisions==1); /* Existing immediate electrical release + retry policy. */
    /* E: three missing response candidates still disarm. */
    warm_and_enable(&f);f.no_reply=true;seconds(&f,30,60);
    assert(!f.s.armed && f.s.timeout_total>=3 && f.a.rx_guard.decision.last_failure==TBX_GUARD_REPLY_TIMEOUT);
    warm_and_enable(&f);f.driver_fault=true;seconds(&f,30,60);
    assert(!f.s.armed && f.s.tx_failure_total>=3 && f.a.rx_guard.decision.last_failure==TBX_GUARD_TX_DRIVER);
    assert(f.a.rx_guard.decision.pid && f.a.rx_guard.decision.spacing_ms==1200);
}
static void windows_and_fault_classification(void)
{
    fixture_t f;warm_and_enable(&f);
    /* An isolated UART fault while not transmitting is RX evidence, not a
     * fabricated collision; repeated loss of reliable RX still stops TX. */
    for(unsigned i=0;i<3;i++){
        assert(!f.a.tx_active);
        tbx_j1708_rx_uart_fault(&f.a,TBX_RX_UART_FRAME,f.now);
        seconds(&f,10,60);
    }
    assert(!f.s.armed && f.a.rx_guard.decision.cause==TBX_GUARD_RX_UART);
    assert(!f.a.collisions && !f.s.tx_failure_total && f.a.rx_guard.total.uart_frame==3);
    /* A delayed sampler cannot turn a long, incomparable interval into three
     * observations. The recent reference is relearned from contiguous windows. */
    warm_and_enable(&f);f.now+=30000000;
    for(unsigned i=0;i<50;i++)frame(&f,TBX_RX_CHECKSUM);
    frame(&f,TBX_RX_VALID);tbx_j1708_rx_tick(&f.a,f.now);
    assert(f.s.armed && !f.a.rx_guard.reference_count && !f.a.rx_guard.suspect_streak);
    seconds(&f,31,60);assert(f.s.armed && f.a.rx_guard.reference_count==3);
    uint64_t begin=UINT64_MAX,end=0;
    for(unsigned i=0;i<3;i++){
        tbx_rx_window_t w=f.a.rx_guard.reference[i];
        if(w.start_us<begin)begin=w.start_us;
        if(w.end_us>end)end=w.end_us;
    }
    assert(end-begin>=30000000 && end-begin<30100000);
    /* A real monotonic-time reversal must never be treated as a normal rate change. */
    tbx_j1708_rx_tick(&f.a,f.now-1);
    assert(!f.s.armed && f.a.rx_guard.decision.cause==TBX_GUARD_CLOCK);
    /* Time zero is a valid start; it must not be used as an empty reference marker. */
    initialize(&f);f.now=0;frame(&f,TBX_RX_VALID);seconds(&f,31,60);
    assert(f.a.rx_guard.reference_count==3);
    assert(f.a.rx_guard.reference[0].start_us==0);
}
static void spontaneous_candidates_and_old_counterexample(void)
{
    fixture_t f;warm_and_enable(&f);
    while(!f.a.tx_active)step(&f,60);
    f.now+=6000;tbx_j1708_active_note_rx(&f.a,f.tx,5,f.now);unsigned i=f.s.pending;assert(i<17);
    /* I: identical broadcast within a request window remains only a candidate. */
    f.now+=20000;frame(&f,TBX_RX_VALID);
    assert(tbx_j1708_active_note_response(&f.a,f.a.config.target_mid[i],f.s.pids[i].pid,f.now));
    assert(!f.a.physical_validated && f.a.last_response_mid==f.a.config.target_mid[i]);
    assert(!tbx_j1708_active_note_response(&f.a,f.a.config.target_mid[i],f.s.pids[i].pid,f.now));
    /* A labelled synthetic counterexample, not an inference of the actual incident. */
    initialize(&f);f.s.armed=true;f.a.work_mode=true;f.a.field.frames=599;f.a.field.bad_frames=6;
    f.a.field.watch_at_us=f.now;f.a.field.last_frame_us=f.now+10000000;
    assert(!healthy_0214(&f.a,f.now+10000000));assert(!f.s.armed);
    warm_and_enable(&f);for(unsigned n=0;n<6;n++)frame(&f,TBX_RX_CHECKSUM);seconds(&f,10,59);
    assert(f.s.armed);seconds(&f,30,60);assert(f.s.armed);
    puts("0.2.14 synthetic 599 valid/6 invalid window stops; 0.2.15 isolated burst does not. Incident branch itself remains unidentifiable from minute aggregates.");
}
int main(void)
{
    normal_conditions();persistent_errors_and_faults();windows_and_fault_classification();spontaneous_candidates_and_old_counterexample();
    puts("RX guard A-J: stable traffic, isolated noise, traffic/engine changes, persistent RX faults, TX faults, repeated timeouts, offline/reconnect, reboot, spontaneous candidates and stale calibration PASS");return 0;
}
