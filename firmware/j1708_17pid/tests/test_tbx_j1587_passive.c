#include "../src/tbx_j1587_passive.h"
#include <stdio.h>
#include <assert.h>
#include <math.h>
static void numeric(uint8_t mid,uint16_t pid,const uint8_t *bytes,
                    uint8_t n,double expected,double tol) {
    tbx_j1587_passive_reading_t got;
    assert(tbx_j1587_decode_passive(mid,pid,bytes,n,&got));
    assert(got.mid==mid && got.pid==pid && got.payload_len==n);
    assert(got.has_numeric_value && got.quality==TBX_PASSIVE_EM_VALIDACAO);
    assert(fabs(got.converted_value-expected)<=tol);
}
int main(void) {
    unsigned n=0;
    const tbx_j1587_passive_spec_t *specs=tbx_j1587_passive_specs(&n);
    assert(specs && n==48);
    for(unsigned i=0;i<n;i++)
        for(unsigned j=i+1;j<n;j++) assert(specs[i].pid!=specs[j].pid);
    const uint8_t rpm[]={0x94,0x0f},volts[]={0x1b,0x02};
    const uint8_t oil[]={0x60},turbo[]={0x49},fuel[]={0x51,0x02};
    const uint8_t odo[]={0x01,0xf0,0x71,0x00},turbo2[]={0xf5,0x01};
    numeric(128,190,rpm,2,997.0,0.001);
    numeric(136,168,volts,2,26.95,0.001);
    numeric(128,100,oil,1,3.312,0.001);
    numeric(128,102,turbo,1,0.62926,0.001);
    numeric(128,183,fuel,2,593*0.059147059125,0.001);
    numeric(128,245,odo,4,7467009*0.1609344,0.001);
    numeric(128,440,turbo2,2,501*0.00125,0.001);
    tbx_j1587_passive_reading_t r;
    const uint8_t dtc[]={0x12,0xb5,0x01},na[]={0xff};
    assert(tbx_j1587_decode_passive(128,194,dtc,3,&r));
    assert(r.quality==TBX_PASSIVE_RAW_AGUARDANDO_ESCALA);
    assert(r.payload[0]==0x12 && r.payload[2]==0x01);
    assert(tbx_j1587_decode_passive(128,102,na,1,&r));
    assert(r.quality==TBX_PASSIVE_INDISPONIVEL && !r.has_numeric_value);
    assert(!tbx_j1587_decode_passive(128,190,na,1,&r));
    assert(r.quality==TBX_PASSIVE_COMPRIMENTO_INVALIDO);
    assert(!tbx_j1587_decode_passive(128,250,na,1,&r));
    puts("OK: 48 passive PIDs; numeric/raw/invalid/unavailable");
    return 0;
}
