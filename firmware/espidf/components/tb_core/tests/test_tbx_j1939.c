#include "tbx_j1939.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static tbx_j1939_observation_t got[TBX_J1939_SIGNAL_COUNT];
static size_t emitted;
static void collect(const tbx_j1939_observation_t *o, void *ctx)
{
    (void)ctx;
    assert(emitted<TBX_J1939_SIGNAL_COUNT);
    got[emitted++]=*o;
}
static uint32_t can(uint32_t pgn,unsigned sa) { return UINT32_C(0x18000000)|(pgn<<8)|sa; }
static void run(tbx_j1939_t *s,uint32_t pgn,unsigned sa,const uint8_t *d,size_t n,uint64_t ms)
{
    emitted=0;
    assert(tbx_j1939_decode(s,can(pgn,sa),true,false,d,n,ms,collect,NULL)==emitted);
}
static tbx_j1939_observation_t *id(unsigned id)
{
    for(size_t i=0;i<emitted;++i) if(got[i].signal->id==id) return &got[i];
    assert(!"missing catalog observation");return NULL;
}
static void close_to(double a,double b) { assert(fabs(a-b)<1e-8); }
static void numeric(tbx_j1939_t *s,unsigned catalog_id,uint32_t pgn,unsigned off,unsigned bytes,
                    uint32_t raw,double expected)
{
    uint8_t d[8]={0};
    for(unsigned i=0;i<bytes;++i)d[off+i]=(uint8_t)(raw>>(8u*i));
    if(pgn==65258)d[0]=0x1f;
    run(s,pgn,0,d,8,100);
    assert(id(catalog_id)->has_value && id(catalog_id)->quality==TBX_J1939_OK);
    close_to(id(catalog_id)->value,expected);
    assert(memcmp(id(catalog_id)->raw,d,8)==0);
    assert(id(catalog_id)->captured_ms==100);
    for(unsigned i=0;i<bytes;++i)d[off+i]=0xff;
    run(s,pgn,0,d,8,200);
    assert(!id(catalog_id)->has_value && id(catalog_id)->quality==TBX_J1939_NA);
    d[off+bytes-1]=0xfe;
    run(s,pgn,0,d,8,300);
    assert(!id(catalog_id)->has_value && id(catalog_id)->quality==TBX_J1939_ERROR);
    d[off+bytes-1]=0xfb;
    run(s,pgn,0,d,8,400);
    assert(!id(catalog_id)->has_value && id(catalog_id)->quality==TBX_J1939_RESERVED);
    run(s,pgn,0,d,off+bytes-1,500);
    assert(!id(catalog_id)->has_value && id(catalog_id)->quality==TBX_J1939_BAD_LENGTH);
}
int main(void)
{
    tbx_j1939_t s;tbx_j1939_init(&s);
    size_t count;const tbx_j1939_signal_t *catalog=tbx_j1939_catalog(&count);
    assert(count==76);
    unsigned implemented=0,valid=0,partial=0,pending=0,unavailable=0,unknown=0;
    bool pgn_seen[65536]={0};unsigned pgn_count=0;
    for(size_t i=0;i<count;++i) {
        assert(catalog[i].id==i+1);
        assert(catalog[i].name && catalog[i].evidence);
        implemented+=catalog[i].implemented;
        switch(catalog[i].validation) {
        case TBX_J1939_LOG_VALIDATED:++valid;break;
        case TBX_J1939_IN_VALIDATION:++partial;break;
        case TBX_J1939_PENDING:++pending;break;
        case TBX_J1939_UNAVAILABLE:++unavailable;break;
        case TBX_J1939_UNDECODED:++unknown;break;
        }
        if(!pgn_seen[catalog[i].pgn]) {pgn_seen[catalog[i].pgn]=true;++pgn_count;}
        uint8_t d[8]={0};run(&s,catalog[i].pgn,0,d,8,1);
        assert(id(catalog[i].id)->signal==&catalog[i]);
        if(!catalog[i].implemented && catalog[i].id!=38 && catalog[i].id!=39 && catalog[i].id!=75) {
            assert(!id(catalog[i].id)->has_value);
            assert(id(catalog[i].id)->quality==TBX_J1939_NO_FORMULA || id(catalog[i].id)->raw_only);
        }
    }
    assert(implemented==55 && valid==61 && partial==9 && pending==2 && unavailable==3 && unknown==1);
    assert(pgn_count==29);
    numeric(&s,1,61444,3,2,8000,1000);
    numeric(&s,2,61444,2,1,175,50);
    numeric(&s,6,61443,1,1,150,60);
    numeric(&s,14,65265,1,2,12800,50);
    numeric(&s,17,65217,0,4,200000,1000);
    numeric(&s,19,65258,1,2,20000,10000);
    numeric(&s,20,65258,1,2,20000,10000);
    numeric(&s,21,65295,5,1,40,40000);
    numeric(&s,24,65262,0,1,120,80);
    numeric(&s,25,65262,2,2,11616,90);
    numeric(&s,27,65272,4,2,11296,80);
    numeric(&s,28,65269,3,2,9536,25);
    numeric(&s,29,65270,1,1,100,2);
    numeric(&s,30,65270,2,1,100,60);
    numeric(&s,33,65263,0,1,125,5);
    numeric(&s,34,65263,3,1,100,4);
    numeric(&s,35,65263,2,1,125,50);
    numeric(&s,40,61442,5,2,8000,1000);
    numeric(&s,41,61442,1,2,4000,500);
    numeric(&s,42,61442,3,1,25,10);
    numeric(&s,44,61445,0,1,137,12);
    numeric(&s,45,61445,3,1,137,12);
    numeric(&s,46,61445,1,2,1000,1);
    uint8_t d[8]={0,0,175,0x40,0x1f,0,0,0};run(&s,61444,0,d,8,1000);
    assert(id(3)->has_value);close_to(id(3)->value,0.5*2643*1000/9550);
    assert(id(3)->reference_torque_nm==2643 && id(3)->signal->derived);
    d[2]=0xff;run(&s,61444,0,d,8,1001);assert(!id(3)->has_value);
    d[0]=0xf3;run(&s,65286,3,d,8,1002);assert(strcmp(id(47)->text,"A")==0);
    d[0]=0;run(&s,65286,3,d,8,1002);assert(id(47)->quality==TBX_J1939_UNKNOWN_STATE);
    run(&s,65321,3,(const uint8_t *)" AN E+00",8,1003);assert(id(50)->has_value && id(50)->value==1);
    run(&s,65321,3,(const uint8_t *)"garbage!",8,1004);assert(!id(50)->has_value);
    d[0]=0xdf;d[1]=0x60;run(&s,65323,3,d,8,1005);
    assert(id(59)->has_value && id(59)->value==1);assert(id(57)->value==3);
    d[0]=0;d[1]=0xff;run(&s,65323,3,d,8,1006);
    assert(!id(59)->has_value && id(57)->quality==TBX_J1939_NA);
    /* Duplicate channels are independent observations of one bogie, not 20 tonnes. */
    memset(d,0,8);d[0]=0x1f;d[1]=0x20;d[2]=0x4e;run(&s,65258,3,d,8,1006);close_to(id(19)->value,10000);
    d[0]=0x2f;run(&s,65258,3,d,8,1007);close_to(id(19)->value,10000);
    d[0]=0x3f;run(&s,65258,3,d,8,1008);assert(!id(19)->has_value);
    /* Trip starts with a real baseline; resets and source changes do not add km. */
    tbx_j1939_init(&s);memset(d,0,8);d[0]=100;
    run(&s,65217,0,d,8,1000);assert(id(18)->quality==TBX_J1939_BASELINE && !id(18)->has_value);
    d[0]=101;run(&s,65217,0,d,8,2000);close_to(id(18)->value,0.005);
    run(&s,65217,1,d,8,2000);assert(!id(18)->has_value);
    d[0]=1;run(&s,65217,0,d,8,3000);assert(id(18)->quality==TBX_J1939_COUNTER_RESET);
    d[0]=2;run(&s,65217,0,d,8,4000);close_to(id(18)->value,0.010);
    run(&s,65217,0,d,8,40000);assert(id(18)->quality==TBX_J1939_BASELINE);
    d[0]=200;run(&s,65217,0,d,8,41000);assert(id(18)->quality==TBX_J1939_RANGE);
    /* PDU1 destination is not part of PGN; preserve actual PDU2 aliases as raw. */
    assert(tbx_j1939_pgn(UINT32_C(0x18eaab07))==59904);
    assert(tbx_j1939_pgn(can(65265,241))==65265);
    const uint32_t aliases[]={61456,65118,65150,65214,65246,65278,65311,65343};
    for(size_t i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i) {run(&s,aliases[i],241,d,8,0);assert(!emitted);}
    assert(tbx_j1939_capture_alias(61456,0) && tbx_j1939_capture_alias(61456,5));
    assert(!tbx_j1939_capture_alias(61456,6));
    assert(tbx_j1939_capture_alias(65343,41) && !tbx_j1939_capture_alias(65343,42));
    assert(tbx_j1939_capture_alias(65246,193) && !tbx_j1939_capture_alias(65246,194));
    emitted=0;assert(!tbx_j1939_decode(&s,0x123,false,false,d,8,0,collect,NULL));
    assert(!tbx_j1939_decode(&s,can(61444,0),true,true,d,8,0,collect,NULL));
    assert(!tbx_j1939_decode(&s,can(61444,0),true,false,d,9,0,collect,NULL));
    assert(!tbx_j1939_decode(&s,UINT32_MAX,true,false,d,8,0,collect,NULL));
    puts("J1939: 76 catalog rows / 29 PGNs; 55 evidenced mappings, 15 partial/raw, 6 pending/unavailable; SAE status, source isolation and delta tests PASS");
    return 0;
}
