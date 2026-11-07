#include "tbx_j1587_bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned seen;
static tbx_j1587_observation_t last;
static void observe(const tbx_j1587_observation_t *o, void *context)
{
    (void)context;
    ++seen; last=*o;
    assert(o->mid>=128u && o->uptime_ms==1234u);
}
static void checksum(uint8_t *frame, size_t n)
{
    unsigned sum=0;
    for (size_t i=0;i<n-1u;i++) sum+=frame[i];
    frame[n-1u]=(uint8_t)(0u-sum);
}
static void decode_parameter(uint16_t pid, const uint8_t *data, uint8_t len)
{
    uint8_t frame[21]={128};
    size_t n=1;
    if (pid>=256u) frame[n++]=255;
    uint8_t wire=(uint8_t)pid;
    frame[n++]=wire;
    if (wire>=192u) frame[n++]=len;
    if (len) memcpy(frame+n,data,len);
    n+=len+1u;
    checksum(frame,n);
    seen=0;
    assert(tbx_j1587_bridge_decode(frame,n,1234,observe,NULL)==TBX_J1587_FRAME_OK);
    assert(seen==1u && last.pid==pid && last.raw_len==len);
    if (len) assert(memcmp(last.raw,data,len)==0);
}
static void all_catalogs(void)
{
    unsigned count;
    const tbx_j1587_passive_spec_t *spec=tbx_j1587_passive_specs(&count);
    assert(count==48u);
    for (unsigned i=0;i<count;i++) {
        uint8_t raw[4]={0};
        uint8_t len=spec[i].expected_len;
        if (spec[i].pid==162u || spec[i].pid==163u) {raw[0]=' ';raw[1]='A';}
        if (spec[i].pid==251u) len=3;
        if (spec[i].pid==252u) {len=3;raw[0]=120;raw[1]=10;raw[2]=41;}
        decode_parameter(spec[i].pid,raw,len);
        assert(!last.request_catalog);
        assert(last.quality==TBX_J1587_OBS_EM_VALIDACAO);
        assert(last.passive.interpreted);
    }
    tbx_j1708_request_scheduler_t scheduler;
    tbx_j1708_request_init(&scheduler);
    for (unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++) {
        uint16_t pid=scheduler.pids[i].pid;
        uint8_t raw[4]={0};
        uint8_t len=pid<128u?1u:(pid<192u?2u:4u);
        assert(scheduler.pids[i].enabled);
        decode_parameter(pid,raw,len);
        assert(last.request_catalog && last.quality==TBX_J1587_OBS_EM_VALIDACAO);
    }
    assert(!tbx_j1708_revb1_tx_available());
    assert(!tbx_j1708_revb1_arm(&scheduler,true,true));
    assert(!scheduler.armed && !scheduler.test_permitted);
    assert(strstr(tbx_j1708_revb1_tx_block_reason(),"DI=0"));
}
static void framing(void)
{
    /* Independent multi-PID message: field examples from29/10/2026. */
    uint8_t frame[]={128,110,0xa9,190,0x94,0x0f,194,3,0x12,0xb5,1,0};
    checksum(frame,sizeof(frame));
    seen=0;
    assert(tbx_j1587_bridge_decode(frame,sizeof(frame),1234,observe,NULL)==TBX_J1587_FRAME_OK);
    assert(seen==3 && last.pid==194 && last.passive.dtc_count==1);
    assert(last.passive.dtcs[0].sid && last.passive.dtcs[0].identifier==18);
    assert(last.passive.dtcs[0].fmi==5 && last.passive.dtcs[0].active);
    frame[1]^=1;
    seen=0;
    assert(tbx_j1587_bridge_decode(frame,sizeof(frame),1234,observe,NULL)==TBX_J1587_FRAME_CHECKSUM_INVALID);
    assert(seen==0);
    uint8_t truncated[]={128,110,0xa9,250,4,1,0};
    checksum(truncated,sizeof(truncated));
    assert(tbx_j1587_bridge_decode(truncated,sizeof(truncated),1234,observe,NULL)==TBX_J1587_FRAME_LAYOUT_INVALID);
    assert(seen==0); /* Reject complete malformed layout without prefix data. */
    uint8_t bad_page[]={128,110,0xa9,255,184,1,2,0};
    checksum(bad_page,sizeof(bad_page));
    assert(tbx_j1587_bridge_decode(bad_page,sizeof(bad_page),1234,observe,NULL)==TBX_J1587_FRAME_LAYOUT_INVALID);
    uint8_t nonj[]={100,84,1,0};checksum(nonj,sizeof(nonj));
    assert(tbx_j1587_bridge_decode(nonj,sizeof(nonj),1234,observe,NULL)==TBX_J1587_FRAME_NON_J1587_MID);
    assert(tbx_j1587_bridge_decode(NULL,0,1234,observe,NULL)==TBX_J1587_FRAME_LENGTH_INVALID);
    uint8_t extended[]={128,255,184,0xf5,1,0};checksum(extended,sizeof(extended));
    assert(tbx_j1587_bridge_decode(extended,sizeof(extended),1234,observe,NULL)==TBX_J1587_FRAME_OK);
    assert(last.pid==440 && last.has_numeric && fabs(last.value-0.62625)<1e-8);
}
static void invalid_and_unavailable(void)
{
    const uint8_t na[]={0xff,0xff,0xff,0xff};
    decode_parameter(110,na,1);
    assert(!last.has_numeric && last.quality==TBX_J1587_OBS_INDISPONIVEL);
    decode_parameter(175,na,2);
    assert(!last.has_numeric && last.quality==TBX_J1587_OBS_INDISPONIVEL);
    decode_parameter(250,na,4);
    assert(!last.has_numeric && last.quality==TBX_J1587_OBS_INDISPONIVEL);
    const uint8_t short_clock[]={0};
    decode_parameter(251,short_clock,1);
    assert(last.quality==TBX_J1587_OBS_COMPRIMENTO_INVALIDO);
    decode_parameter(252,short_clock,1);
    assert(last.quality==TBX_J1587_OBS_COMPRIMENTO_INVALIDO);
    const uint8_t invalid_date[]={124,2,41};
    decode_parameter(252,invalid_date,3);
    assert(last.quality==TBX_J1587_OBS_COMPRIMENTO_INVALIDO);
    const uint8_t raw[]={7};
    decode_parameter(1,raw,1);
    assert(last.quality==TBX_J1587_OBS_DESCONHECIDO && !last.has_numeric);
}
static void counters(void)
{
    const uint16_t pids[]={235,236,250};
    for (unsigned i=0;i<3;i++) {
        tbx_j1587_counter_checkpoint_t checkpoint={0};
        double delta=-1;
        uint8_t raw[]={100,0,0,0};
        decode_parameter(pids[i],raw,4);
        assert(!tbx_j1587_counter_delta(&checkpoint,&last,&delta));
        raw[0]=103; decode_parameter(pids[i],raw,4);
        assert(tbx_j1587_counter_delta(&checkpoint,&last,&delta));
        assert(fabs(delta-(i==0u?0.15:3*0.473176473))<1e-9);
        raw[0]=1; decode_parameter(pids[i],raw,4);
        assert(!tbx_j1587_counter_delta(&checkpoint,&last,&delta));
        raw[0]=2; decode_parameter(pids[i],raw,4);
        last.mid=130;
        assert(!tbx_j1587_counter_delta(&checkpoint,&last,&delta));
        raw[0]=3; decode_parameter(pids[i],raw,4);
        const uint8_t na[]={255,255,255,255};decode_parameter(pids[i],na,4);
        uint32_t before=checkpoint.raw;
        assert(!tbx_j1587_counter_delta(&checkpoint,&last,&delta) && checkpoint.raw==before);
    }
}
static void fuzz_bounds(void)
{
    uint32_t state=12345;
    for (unsigned i=0;i<20000;i++) {
        uint8_t frame[21];
        state=state*1664525u+1013904223u;
        size_t n=2u+state%20u;
        for(size_t j=0;j<n;j++){state=state*1664525u+1013904223u;frame[j]=(uint8_t)(state>>24);}
        checksum(frame,n);
        (void)tbx_j1587_bridge_decode(frame,n,1234,observe,NULL);
    }
}
int main(void)
{
    all_catalogs(); framing(); invalid_and_unavailable(); counters(); fuzz_bounds();
    puts("OK: J1587 bridge48+17, multiparameter/page2, checksum/bounds/NA, counter deltas, hardware TX inhibit");
    return 0;
}
