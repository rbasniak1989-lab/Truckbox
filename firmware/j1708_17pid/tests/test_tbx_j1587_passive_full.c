#include "../src/tbx_j1587_passive_full.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    uint16_t pid;
    uint8_t mid, len;
    uint8_t raw[4];
    tbx_j1587_kind_t kind;
} sample_t;
static const sample_t samples[] = {
    {36,130,1,{0x00},TBX_J1587_KIND_SCALAR},
    {43,144,1,{0x07},TBX_J1587_KIND_STATES},
    {44,130,1,{0xF4},TBX_J1587_KIND_STATES},
    {46,144,1,{0x70},TBX_J1587_KIND_SCALAR},
    {49,136,1,{0x01},TBX_J1587_KIND_STATES},
    {70,144,1,{0x7F},TBX_J1587_KIND_STATES},
    {71,144,1,{0x70},TBX_J1587_KIND_STATES},
    {83,128,1,{0x7F},TBX_J1587_KIND_STATES},
    {85,128,1,{0x01},TBX_J1587_KIND_STATES},
    {86,144,1,{0x00},TBX_J1587_KIND_SCALAR},
    {89,144,1,{0x00},TBX_J1587_KIND_STATES},
    {97,128,1,{0x00},TBX_J1587_KIND_STATES},
    {105,128,1,{0x54},TBX_J1587_KIND_SCALAR},
    {106,128,1,{0x4E},TBX_J1587_KIND_SCALAR},
    {110,128,1,{0xA9},TBX_J1587_KIND_SCALAR},
    {111,128,1,{0xC8},TBX_J1587_KIND_SCALAR},
    {122,128,1,{0x00},TBX_J1587_KIND_SCALAR},
    {128,144,2,{0xF9,0x80},TBX_J1587_KIND_REQUEST},
    {150,144,2,{0xCF,0xFF},TBX_J1587_KIND_STATES},
    {151,136,2,{0x3F,0xFF},TBX_J1587_KIND_STATES},
    {155,128,2,{0xFD,0xCB},TBX_J1587_KIND_STATES},
    {162,130,2,{0x20,0x41},TBX_J1587_KIND_TEXT},
    {163,130,2,{0x31,0x32},TBX_J1587_KIND_TEXT},
    {171,140,2,{0xE8,0x00},TBX_J1587_KIND_SCALAR},
    {174,128,2,{0x9D,0x02},TBX_J1587_KIND_SCALAR},
    {175,128,2,{0x0E,0x03},TBX_J1587_KIND_SCALAR},
    {177,130,2,{0x87,0x02},TBX_J1587_KIND_SCALAR},
    {194,128,3,{0x12,0xB5,0x01},TBX_J1587_KIND_DTCS},
    {249,128,4,{0x2B,0x1A,0x01,0x00},TBX_J1587_KIND_SCALAR},
    {251,140,3,{0x00,0x0B,0x00},TBX_J1587_KIND_CLOCK},
    {252,140,3,{0x78,0x0A,0x29},TBX_J1587_KIND_DATE}
};
static void approx(double value,double expected) {
    assert(fabs(value-expected)<0.02);
}
int main(void) {
    assert(sizeof(samples)/sizeof(samples[0])==31);
    tbx_j1587_full_reading_t r;
    for(unsigned i=0;i<31;i++) {
        const sample_t *s=&samples[i];
        assert(tbx_j1587_decode_passive_full(s->mid,s->pid,s->raw,s->len,&r));
        assert(r.interpreted);
        assert(r.kind==s->kind);
        assert(r.base.quality==TBX_PASSIVE_EM_VALIDACAO);
        assert(r.base.mid==s->mid && r.base.pid==s->pid);
        assert(r.base.payload_len==s->len);
        assert(memcmp(r.base.payload,s->raw,s->len)==0);
    }
    const uint8_t wear[]={0};
    assert(tbx_j1587_decode_passive_full(130,36,wear,1,&r));
    approx(r.numeric_value,0);
    const uint8_t wet[]={0x70};
    assert(tbx_j1587_decode_passive_full(144,46,wet,1,&r));
    approx(r.numeric_value,7.7168);
    const uint8_t temp[]={0xA9};
    assert(tbx_j1587_decode_passive_full(128,110,temp,1,&r));
    approx(r.numeric_value,76.11111111);
    const uint8_t intake[]={0x4E};
    assert(tbx_j1587_decode_passive_full(128,106,intake,1,&r));
    approx(r.numeric_value,134.472);
    const uint8_t ambient[]={0xE8,0};
    assert(tbx_j1587_decode_passive_full(140,171,ambient,2,&r));
    approx(r.numeric_value,14.4444444);
    const uint8_t oil[]={0x0E,0x03};
    assert(tbx_j1587_decode_passive_full(128,175,oil,2,&r));
    approx(r.numeric_value,90.83333333);
    const uint8_t gears[]={0x20,0x41};
    assert(tbx_j1587_decode_passive_full(130,162,gears,2,&r));
    assert(strcmp(r.text,"A")==0);
    const uint8_t gears2[]={0x31,0x32};
    assert(tbx_j1587_decode_passive_full(130,163,gears2,2,&r));
    assert(strcmp(r.text,"12")==0);
    const uint8_t dtc[]={0x12,0xB5,0x01};
    assert(tbx_j1587_decode_passive_full(128,194,dtc,3,&r));
    assert(r.dtc_count==1 && r.dtcs[0].identifier==18);
    assert(r.dtcs[0].sid && !r.dtcs[0].page2);
    assert(r.dtcs[0].fmi==5 && r.dtcs[0].active);
    assert(r.dtcs[0].has_occurrences && r.dtcs[0].occurrences==1);
    assert(tbx_j1587_decode_passive_full(128,194,NULL,0,&r));
    assert(r.dtc_count==0);
    const uint8_t bad_dtc[]={0x12,0xB5};
    assert(!tbx_j1587_decode_passive_full(128,194,bad_dtc,2,&r));
    assert(r.base.quality==TBX_PASSIVE_COMPRIMENTO_INVALIDO);
    const uint8_t req[]={0xF9,0x80};
    assert(tbx_j1587_decode_passive_full(144,128,req,2,&r));
    assert(r.requested_pid==249 && r.target_mid==128);
    const uint8_t time[]={0x00,0x0B,0x00};
    assert(tbx_j1587_decode_passive_full(140,251,time,3,&r));
    assert(r.hour==0 && r.minute==11 && r.second==0);
    const uint8_t date[]={0x78,0x0A,0x29};
    assert(tbx_j1587_decode_passive_full(140,252,date,3,&r));
    assert(r.day==30 && r.month==10 && r.year==2026);
    const uint8_t two[]={0xFF,0xFF};
    assert(!tbx_j1587_decode_passive_full(130,163,two,1,&r));
    assert(r.base.quality==TBX_PASSIVE_COMPRIMENTO_INVALIDO);
    puts("OK: 31/31 passive PID decoder paths, physical units, masks, codes and time");
    return 0;
}
