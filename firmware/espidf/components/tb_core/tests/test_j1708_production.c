#include "tbx_j1708_auto.h"
#include "tbx_j1708_policy.h"
#include "tbx_transmission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    tbx_j1708_active_t c;tbx_j1708_request_scheduler_t s;tbx_j1708_auto_t automatic;
    tbx_j1708_auto_env_t env;tbx_transmission_t transmission;
    uint64_t now,last_write;unsigned writes,aborts,credit;
    bool no_reply,bad_echo,busy;uint8_t tx[6],tx_length;
}fixture_t;
static tbx_j1708_tx_result_t write_tx(const uint8_t *bytes,size_t n,void *ctx)
{
    fixture_t *f=ctx;if(f->busy)return TBX_J1708_TX_DEFERRED;assert((n==5 || n==6) && bytes[0]==172 && bytes[n==6?2:1]==128);
    if(n==6){const uint8_t expected[]={0xac,0xff,0x80,0xb7,0x80,0x9e};assert(!memcmp(bytes,expected,6));}
    unsigned checksum=0;for(unsigned i=0;i<n;i++)checksum+=bytes[i];assert(!(checksum&255));
    if(f->writes)assert(f->now-f->last_write>=500000);
    f->last_write=f->now;++f->writes;memcpy(f->tx,bytes,n);f->tx_length=(uint8_t)n;return TBX_J1708_TX_STARTED;
}
static void abort_tx(void *ctx){++((fixture_t *)ctx)->aborts;}
static void initialize(fixture_t *f)
{
    memset(f,0,sizeof(*f));f->now=1000000;
    const tbx_j1708_active_ops_t ops={write_tx,abort_tx,NULL};tbx_j1708_active_init(&f->c,&f->s,&ops,f);
    f->env=(tbx_j1708_auto_env_t){.enabled=true,.persisted=true,.interface_ready=true,.local_mid=172,.topology_attested=true,.timeout_ms=450};
    for(unsigned i=0;i<17;i++){f->env.targets[i]=tbx_j1708_policy()[i].mid;f->env.periods[i]=tbx_j1708_production_period(tbx_j1708_policy()[i].pid,false);}
}
static void step(fixture_t *f)
{
    f->now+=10000;f->credit+=60;
    while(f->credit>=100){tbx_j1708_field_note_frame_kind(&f->c,128,TBX_RX_VALID,f->now);f->credit-=100;}
    if(f->c.tx_active){
        if(f->bad_echo){uint8_t wrong=0;tbx_j1708_active_note_rx(&f->c,&wrong,1,f->now);}
        else tbx_j1708_active_note_rx(&f->c,f->tx,f->tx_length,f->now);
    }
    if(!f->no_reply && f->s.pending<TBX_J1708_REQUEST_CAPACITY && !f->c.tx_active && f->now-f->c.tx_started_us>=210000){
        unsigned i=f->s.pending;
        assert(!tbx_j1708_active_note_response(&f->c,255,f->s.pids[i].pid,f->now));
        assert(tbx_j1708_active_note_response(&f->c,f->c.config.target_mid[i],f->s.pids[i].pid,f->now));
    }
    tbx_j1708_auto_tick(&f->automatic,&f->c,&f->env,f->transmission.launch,f->now);
    tbx_j1708_active_tick(&f->c,f->now);
}
static void seconds(fixture_t *f,unsigned n){for(unsigned i=0;i<n*100;i++)step(f);}
static unsigned index_of(fixture_t *f,unsigned pid){for(unsigned i=0;i<17;i++)if(f->s.pids[i].pid==pid)return i;assert(false);return 0;}
static void automatic_and_cadences(void)
{
    fixture_t f;initialize(&f);seconds(&f,29);assert(!f.writes);
    seconds(&f,80);assert(f.c.production_mode && f.c.work_mode && f.s.armed);
    assert(!f.c.physical_validated && !f.c.field.confirmed && f.c.lease_deadline_us==UINT64_MAX);
    for(unsigned i=0;i<17;i++){assert(f.s.pids[i].answered && f.s.pids[i].last_latency_ms==210);assert(f.s.pids[i].period_ms==f.env.periods[i]);}
    while(f.s.pending<17 || f.c.tx_active)step(&f);
    f.now+=UINT64_C(46800000000); /* 13 h, no renewed app/manual lease. */
    tbx_j1708_field_note_frame_kind(&f.c,128,TBX_RX_VALID,f.now);seconds(&f,1);
    assert(f.c.production_mode && !f.c.lease_expirations);
    unsigned p31=index_of(&f,31),p33=index_of(&f,33),before31=f.s.pids[p31].sent,before33=f.s.pids[p33].sent;
    f.transmission.launch=true;
    for(unsigned i=0;i<17;i++)f.env.periods[i]=tbx_j1708_production_period(f.s.pids[i].pid,true);
    seconds(&f,30);assert(f.s.pids[p31].sent-before31>=12 && f.s.pids[p33].sent-before33>=12);
    assert(!f.s.timeout_total && !f.c.collisions);
    f.env.enabled=false;seconds(&f,1);assert(!f.c.work_mode && !f.s.armed);unsigned writes=f.writes;
    seconds(&f,60);assert(f.writes==writes);
    /* Reboot reloads the persisted enabled policy, not a volatile test lease. */
    initialize(&f);seconds(&f,45);assert(f.c.production_mode && f.writes);
}
static void expansion_in_one_scheduler(void)
{
    fixture_t f;initialize(&f);f.env.extended=true;seconds(&f,180);
    assert(f.c.production_mode && f.s.answered_total && !f.s.timeout_total);
    for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)assert(f.s.pids[i].sent && f.s.pids[i].answered);
    assert(f.s.pids[26].pid==439 && f.c.config.target_mid[26]==128);
    /* Requested scopes remain candidate, never external electrical evidence. */
    assert(!f.c.physical_validated && !f.c.field.confirmed);
}
static void pid153_transient_timeout_does_not_stop_passive_or_work(void)
{
    fixture_t f;initialize(&f);f.env.extended=true;
    unsigned index=0;while(f.s.pids[index].pid!=153)++index;
    for(unsigned tick=0;f.s.pending!=index && tick<20000;tick++)step(&f);
    assert(f.s.pending==index);f.no_reply=true;
    while(f.s.pids[index].timed_out==0)step(&f);
    assert(f.c.work_mode && f.s.armed && !f.c.collisions);
    f.no_reply=false;seconds(&f,5);
    assert(f.s.pids[index].sent==2 && f.s.pids[index].answered==1 && f.s.pids[index].timed_out==1);
    assert(f.c.work_mode && f.s.armed && f.c.rx_guard.total.valid && !f.c.physical_validated);
    puts("PID153: one timeout + bounded successful candidate retry; unchanged450ms deadline, passive RX and production continue PASS");
}

static void launch_transition_with_pending_request(void)
{
    fixture_t f;initialize(&f);seconds(&f,45);unsigned i=index_of(&f,31);
    while(f.s.pending!=i)step(&f);
    assert(f.s.pids[i].period_ms==10000);
    f.transmission.launch=true;
    for(unsigned p=0;p<17;p++)f.env.periods[p]=tbx_j1708_production_period(f.s.pids[p].pid,true);
    unsigned writes=f.writes;
    tbx_j1708_auto_tick(&f.automatic,&f.c,&f.env,true,f.now);
    assert(f.s.pending==i && f.writes==writes);
    assert(f.s.pids[i].next_due_ms==f.s.pids[i].last_sent_ms+2000);
    unsigned sent=f.s.pids[i].sent;seconds(&f,3);assert(f.s.pids[i].sent>sent);
}
static void faults_recovery_and_interlocks(void)
{
    fixture_t f;initialize(&f);seconds(&f,60);assert(f.c.production_mode);
    f.no_reply=true;seconds(&f,20);assert(!f.s.armed && f.automatic.phase==TBX_AUTO_BACKOFF && f.s.timeout_total>=3);
    unsigned writes=f.writes;seconds(&f,20);assert(f.writes==writes);
    f.no_reply=false;seconds(&f,90);assert(f.c.production_mode && f.automatic.starts>=2);
    f.env.ota_busy=true;seconds(&f,1);assert(!f.s.armed && f.automatic.phase==TBX_AUTO_OTA);
    f.env.ota_busy=false;seconds(&f,45);assert(f.c.production_mode);
    f.bad_echo=true;seconds(&f,20);assert(!f.s.armed && f.c.collisions>=3 && f.automatic.phase==TBX_AUTO_BACKOFF);
    f.bad_echo=false;seconds(&f,120);assert(f.c.production_mode);
    f.busy=true;unsigned before_busy=f.writes;seconds(&f,20);
    assert(f.c.production_mode && f.writes==before_busy && f.s.deferred_total);
    f.busy=false;seconds(&f,10);assert(f.writes>before_busy);
    f.env.local_conflict=true;seconds(&f,2);assert(!f.s.armed && f.automatic.phase==TBX_AUTO_BLOCKED);
    writes=f.writes;seconds(&f,90);assert(f.writes==writes);
    initialize(&f);f.env.persisted=false;seconds(&f,90);assert(!f.writes);
    initialize(&f);f.env.local_mid=0;seconds(&f,90);assert(!f.writes);
    initialize(&f);f.env.interface_ready=false;seconds(&f,90);assert(!f.writes);
}
static void transmission_launch_and_candidates(void)
{
    tbx_transmission_t t={0};uint64_t now=1000000;
    tbx_transmission_observe(&t,TBX_TR_SPEED,0,now);tbx_transmission_observe(&t,TBX_TR_RPM,850,now);
    tbx_transmission_observe(&t,TBX_TR_PEDAL,0,now);tbx_transmission_tick(&t,now);assert(!t.launch);
    now+=1000;tbx_transmission_observe(&t,TBX_TR_PEDAL,20,now);
    tbx_transmission_observe(&t,TBX_TR_GEAR,1,now);tbx_transmission_observe(&t,TBX_TR_SELECTED,1,now);
    tbx_transmission_tick(&t,now);assert(t.launch);
    for(unsigned i=0;i<12;i++){
        now+=500000;tbx_transmission_observe(&t,TBX_TR_CLUTCH,i&1?20:30,now);
        tbx_transmission_observe(&t,TBX_TR_RPM,i&1?850:1200,now);tbx_transmission_observe(&t,TBX_TR_SPEED,i&1?0:0.5,now);
        tbx_transmission_observe(&t,TBX_TR_PEDAL,20,now);tbx_transmission_observe(&t,TBX_TR_GEAR,1,now);tbx_transmission_observe(&t,TBX_TR_SELECTED,1,now);
        tbx_transmission_tick(&t,now);
    }
    assert(t.candidate_flags&TBX_TR_COUPLING_DELAY);assert(t.candidate_flags&TBX_TR_ACTUATOR_OSCILLATION);
    assert(t.candidate_flags&TBX_TR_JUDDER_CANDIDATE);
    now+=1000;tbx_transmission_observe(&t,TBX_TR_RPM,1250,now);tbx_transmission_observe(&t,TBX_TR_SPEED,0,now);tbx_transmission_tick(&t,now);
    assert(t.candidate_flags&TBX_TR_POSSIBLE_SLIP_CANDIDATE);
    now+=1000;tbx_transmission_observe(&t,TBX_TR_SPEED,20,now);tbx_transmission_tick(&t,now);assert(!t.launch);
    /* The accelerated window is capped even with a continuously held pedal. */
    memset(&t,0,sizeof(t));now=1000000;
    for(unsigned i=0;i<32;i++){
        now+=1000000;tbx_transmission_observe(&t,TBX_TR_SPEED,0,now);tbx_transmission_observe(&t,TBX_TR_RPM,850,now);tbx_transmission_observe(&t,TBX_TR_PEDAL,20,now);
        tbx_transmission_tick(&t,now);if(i<30)assert(t.launch);
    }
    assert(!t.launch && t.launches==1);
    /* An unknown/stale speed never manufactures a launch or a clutch diagnosis. */
    memset(&t,0,sizeof(t));tbx_transmission_observe(&t,TBX_TR_RPM,1300,now);tbx_transmission_tick(&t,now);assert(!t.launch && !t.candidate_flags);
}
static void scheduler_supervision_and_stale_baseline(void)
{
    fixture_t f;initialize(&f);seconds(&f,60);
    while(f.s.pending<TBX_J1708_REQUEST_CAPACITY || f.c.tx_active)step(&f);
    unsigned sent=f.s.sent_total;
    /* Inject a live-task scheduler stalled by an impossible future TX deadline. */
    f.s.next_tx_valid=true;f.s.next_tx_ms=(uint32_t)(f.now/1000)+3600000;
    f.env.bus_busy=true;seconds(&f,90);assert(f.c.work_mode && !f.automatic.stalls);
    assert(!strcmp(f.automatic.reason,"BUS_BUSY_DEFERRED"));
    f.env.bus_busy=false;seconds(&f,50);assert(!f.s.armed && f.automatic.stalls==1 && f.automatic.phase==TBX_AUTO_BACKOFF);
    assert(!strcmp(f.automatic.last_stop_reason,"SCHEDULER_NO_TX_PROGRESS") && f.env.enabled);
    assert(f.s.sent_total==sent);seconds(&f,100);assert(f.s.sent_total>sent && f.c.work_mode);
    /* Persistent safety conflict never becomes an operator OFF and never
     * resumes just because Wi-Fi/internet would have recovered. */
    f.env.local_conflict=true;seconds(&f,1);sent=f.s.sent_total;seconds(&f,600);
    assert(f.env.enabled && f.automatic.phase==TBX_AUTO_BLOCKED && f.s.sent_total==sent);
    initialize(&f);seconds(&f,60);assert(f.writes); /* Reboot after a transient stop re-evaluates fresh RX. */
    puts("Polling supervisor: real TX stall -> controlled backoff/restart; genuine MID hold blocks, desired policy stays ON PASS");
}
int main(void)
{
    scheduler_supervision_and_stale_baseline();automatic_and_cadences();expansion_in_one_scheduler();pid153_transient_timeout_does_not_stop_passive_or_work();launch_transition_with_pending_request();faults_recovery_and_interlocks();transmission_launch_and_candidates();
    puts("Production J1708: automatic boot/reboot, original17 + 10 candidates including page2 PID439, normal/launch priority, <=2/s, one pending, matching candidates/latency, timeout/echo backoff and healthy recovery, OTA/MID/NVS interlocks and provisional clutch candidates PASS");
    return 0;
}
