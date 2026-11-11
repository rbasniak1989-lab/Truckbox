/* Virtual clock/bus; these are synthetic frames, not electrical measurements. */
#include "tbx_j1708_active.h"
#include "tbx_j1708_policy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {tbx_j1708_active_t a;tbx_j1708_request_scheduler_t s;uint64_t now,last_send;unsigned sends,periodic_pids;bool fault,defer;uint8_t frame[5];}fixture_t;
static tbx_j1708_tx_result_t send_frame(const uint8_t *bytes,size_t n,void *ctx)
{
    fixture_t *f=ctx;assert(n==5);if(f->defer)return TBX_J1708_TX_DEFERRED;
    if(f->fault)return TBX_J1708_TX_FAULT;
    if(f->last_send)assert(f->now-f->last_send>=1200000);
    f->last_send=f->now;++f->sends;memcpy(f->frame,bytes,n);
    unsigned checksum=0;for(unsigned i=0;i<n;i++)checksum+=bytes[i];assert(!(checksum&255));
    return TBX_J1708_TX_STARTED;
}
static void release(void *ctx){(void)ctx;}
static void init(fixture_t *f)
{
    memset(f,0,sizeof(*f));f->now=1000000;
    tbx_j1708_active_ops_t ops={.begin_tx=send_frame,.abort_tx=release};
    tbx_j1708_active_init(&f->a,&f->s,&ops,f);
    tbx_j1708_active_config_t cfg={.local_mid=172,.priority=8,.inverted_de21_attested=true};
    for(unsigned i=0;i<17;i++)cfg.target_mid[i]=tbx_j1708_policy()[i].mid;
    assert(tbx_j1708_active_configure(&f->a,&cfg));
}
static void passive(fixture_t *f,bool periodic)
{
    tbx_j1708_field_note_frame(&f->a,128,true,f->now);
    if(periodic)for(unsigned i=0;i<(f->periodic_pids?f->periodic_pids:17u);i++)tbx_j1708_field_note_parameter(&f->a,f->a.config.target_mid[i],f->s.pids[i].pid,f->now);
}
static void step(fixture_t *f,bool periodic,bool reply)
{
    f->now+=10000;passive(f,periodic);
    if(f->a.tx_active){
        tbx_j1708_active_note_rx(&f->a,f->frame,5,f->now);
    }
    if(reply && f->s.pending<17 && !f->a.tx_active && f->now-f->a.tx_started_us>=210000){
        unsigned i=f->s.pending;
        tbx_j1708_field_note_parameter(&f->a,f->a.config.target_mid[i],f->s.pids[i].pid,f->now);
        assert(tbx_j1708_active_note_response(&f->a,f->a.config.target_mid[i],f->s.pids[i].pid,f->now));
    }
    tbx_j1708_active_tick(&f->a,f->now);
}
static void complete(fixture_t *f,bool periodic)
{
    assert(tbx_j1708_field_start(&f->a,true,true,f->now));
    for(unsigned i=0;i<60000 && tbx_j1708_field_running(&f->a.field);i++)step(f,periodic,true);
    assert(!tbx_j1708_field_running(&f->a.field));
}
static void successful_indirect_and_cadence(void)
{
    fixture_t f;init(&f);
    assert(!tbx_j1708_field_start(&f.a,false,true,f.now));assert(!tbx_j1708_field_start(&f.a,true,false,f.now));
    assert(!tbx_j1708_field_confirm(&f.a,f.now,true,f.now));
    complete(&f,false);assert(f.a.field.phase==TBX_FIELD_SATISFACTORY && !f.s.armed && f.sends==51);
    assert(!f.a.physical_validated && !f.a.field.confirmed && f.a.field.contrasting_pids==17);
    assert(!tbx_j1708_active_enable_work(&f.a,f.now));
    assert(!tbx_j1708_field_confirm(&f.a,f.a.field.started_us+1,true,f.now));
    assert(!tbx_j1708_field_confirm(&f.a,f.a.field.started_us,false,f.now));
    for(unsigned i=0;i<17;i++){
        tbx_j1708_field_pid_t *p=&f.a.field.pids[i];
        assert(p->sent==3 && p->echoes==3 && p->replies==3 && p->controls==3 && !p->control_hits);
        assert(p->latency_min_us==210000 && p->latency_max_us==210000);
    }
    assert(tbx_j1708_field_confirm(&f.a,f.a.field.started_us,true,f.now));
    assert(!f.a.physical_validated && tbx_j1708_active_work_ready(&f.a,f.now));
    assert(tbx_j1708_active_enable_work(&f.a,f.now));assert(f.s.periodic && f.s.min_spacing_ms>=1200);
    for(unsigned i=0;i<17;i++)assert(f.s.pids[i].period_ms==tbx_j1708_policy()[i].period_ms);
    /* Existing three consecutive response failures stop work TX. */
    for(unsigned i=0;i<10000 && f.s.armed;i++)step(&f,false,false);
    assert(!f.s.armed && f.s.safety_stopped && f.a.consecutive_failures>=3);
    fixture_t reboot;init(&reboot);assert(!reboot.a.field.confirmed && !reboot.s.armed && !reboot.a.physical_validated);
}
static void passive_coincidences_are_inconclusive(void)
{
    fixture_t f;init(&f);complete(&f,true);
    assert(f.a.field.phase==TBX_FIELD_INCONCLUSIVE && f.a.field.contrasting_pids==0);
    assert(!tbx_j1708_field_confirm(&f.a,f.a.field.started_us,true,f.now));
    assert(!tbx_j1708_active_enable_work(&f.a,f.now));
    for(unsigned i=0;i<17;i++)assert(f.a.field.pids[i].before && f.a.field.pids[i].after && f.a.field.pids[i].control_hits==3);
}
static void majority_contrast_boundary(void)
{
    fixture_t f;init(&f);f.periodic_pids=8;complete(&f,true);
    assert(f.a.field.contrasting_pids==9 && f.a.field.phase==TBX_FIELD_SATISFACTORY);
    init(&f);f.periodic_pids=9;complete(&f,true);
    assert(f.a.field.contrasting_pids==8 && f.a.field.phase==TBX_FIELD_INCONCLUSIVE);
}
static void errors_and_missing_evidence(void)
{
    fixture_t f;init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    f.now+=10000000;tbx_j1708_active_tick(&f.a,f.now);assert(f.a.field.phase==TBX_FIELD_FAILED && !f.s.armed);
    init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    tbx_j1708_field_note_frame(&f.a,172,true,f.now);assert(f.s.safety_stopped && !f.s.armed);
    init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));f.a.field.audit_lost=true;step(&f,false,true);assert(!f.s.armed);
    init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    for(unsigned i=0;i<5000 && !f.a.tx_active;i++)step(&f,false,false);
    assert(f.a.tx_active);uint8_t wrong=0;tbx_j1708_active_note_rx(&f.a,&wrong,1,f.now+1000);
    assert(f.a.collisions==1 && !f.s.armed && f.a.field.phase==TBX_FIELD_FAILED);
    init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    for(unsigned i=0;i<6000 && f.s.armed;i++)step(&f,false,false);
    assert(f.a.field.phase==TBX_FIELD_FAILED && f.s.timeout_total==1);
    init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    tbx_j1708_active_stop(&f.a,"VEHICLE_MOVEMENT_J1587");assert(f.a.field.phase==TBX_FIELD_FAILED && !f.s.armed);
    init(&f);complete(&f,false);f.now+=900000000;assert(!tbx_j1708_field_confirm(&f.a,f.a.field.started_us,true,f.now));
}
static void supervision_health_and_expiration(void)
{
    fixture_t f;init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    for(unsigned i=0;i<4;i++)tbx_j1708_field_note_frame(&f.a,128,false,f.now);
    for(unsigned i=0;i<1000 && f.s.armed;i++){
        if(i%100==0)passive(&f,false);
        f.now+=10000;tbx_j1708_active_tick(&f.a,f.now);
    }
    assert(f.a.field.phase==TBX_FIELD_BEFORE && f.s.armed); /* Isolated receiver noise is not a TX fault. */
    init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    f.now+=TBX_J1708_FIELD_LEASE_US;passive(&f,false);tbx_j1708_active_tick(&f.a,f.now);
    assert(!f.s.armed && f.a.lease_expirations==1);
    init(&f);complete(&f,false);tbx_j1708_active_stop(&f.a,"OPERATOR_STOP");
    assert(!tbx_j1708_field_confirm(&f.a,f.a.field.started_us,true,f.now));
    init(&f);complete(&f,false);assert(tbx_j1708_field_confirm(&f.a,f.a.field.started_us,true,f.now));
    assert(tbx_j1708_active_enable_work(&f.a,f.now));f.now+=10000000;tbx_j1708_active_tick(&f.a,f.now);
    assert(!f.s.armed && !f.a.work_mode && !f.a.field.confirmed);
}
static void wrong_mid_pid_and_control_are_not_responses(void)
{
    fixture_t f;init(&f);assert(tbx_j1708_field_start(&f.a,true,true,f.now));
    for(unsigned i=0;i<5000 && !f.a.tx_active;i++)step(&f,false,false);
    tbx_j1708_active_note_rx(&f.a,f.frame,5,f.now+6000);f.now+=10000;
    unsigned i=f.s.pending;assert(i<17);
    assert(!tbx_j1708_active_note_response(&f.a,255,f.s.pids[i].pid,f.now));
    assert(!tbx_j1708_active_note_response(&f.a,f.a.config.target_mid[i],255,f.now));assert(!f.a.field.pids[i].replies);
    assert(tbx_j1708_active_note_response(&f.a,f.a.config.target_mid[i],f.s.pids[i].pid,f.now));
    assert(!tbx_j1708_active_note_response(&f.a,f.a.config.target_mid[i],f.s.pids[i].pid,f.now));
    assert(f.a.field.pids[i].replies==1);
}
int main(void)
{
    successful_indirect_and_cadence();majority_contrast_boundary();supervision_health_and_expiration();passive_coincidences_are_inconclusive();errors_and_missing_evidence();wrong_mid_pid_and_control_are_not_responses();
    puts("Field J1708: baseline/control/3x17/after, measured latency/echo, indirect operator confirmation, ambiguity rejection, conflicts/faults/stop/reboot and existing travel cadence PASS");return 0;
}
