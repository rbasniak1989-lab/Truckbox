#include "../src/tbx_j1587_decoder.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void check(uint16_t pid, const uint8_t *data, uint8_t len, double expected, double tol) {
    tbx_j1587_decoded_t d;
    assert(tbx_j1587_decode(128, pid, data, len, &d));
    assert(d.pid==pid && d.mid==128 && d.raw_length==len);
    assert(d.status==TBX_J1587_VALUE_EM_VALIDACAO);
    assert(d.numeric && fabs(d.value-expected)<=tol);
}
int main(void) {
    const uint8_t a[]={0xFA}, b[]={0xA0}, c[]={0x99}, d[]={0x5D},
       e[]={0x71}, f[]={0x81}, g[]={0xC8}, h[]={0xB8},
       i[]={0x20,0x02}, j[]={0xA1,0x0F}, k[]={0x10,0x0F,0,0},
       l[]={0x9A,0x03,0,0}, m[]={0x47,0xF5,0,0};
    check(26,a,1,100.0,0.0001);
    check(31,b,1,64.0,0.0001);
    check(32,c,1,61.2,0.0001);
    check(33,d,1,37.2,0.0001);
    check(37,e,1,7.7857,0.0001);
    check(68,f,1,64.5,0.0001);
    check(108,g,1,86.2,0.0001);
    check(117,h,1,7.6176,0.0001);
    check(118,h,1,7.6176,0.0001);
    check(158,i,2,27.2,0.0001);
    check(160,j,2,1000.25,0.0001);
    check(235,k,4,192.8,0.0001);
    check(236,l,4,922*0.473176473,0.000001);
    check(250,m,4,62791*0.473176473,0.000001);
    tbx_j1587_decoded_t d34,d62,d63,invalid;
    const uint8_t zero[]={0x00}, na[]={0xFF};
    assert(tbx_j1587_decode(130,34,zero,1,&d34) && d34.bitmapped);
    for(int n=0;n<4;n++) assert(d34.status_fields[n]==0);
    assert(tbx_j1587_decode(130,63,zero,1,&d63) && d63.bitmapped);
    assert(tbx_j1587_decode(144,62,na,1,&d62) && d62.bitmapped && d62.status==TBX_J1587_VALUE_INDISPONIVEL);
    assert(tbx_j1587_decode(128,26,na,1,&invalid) && invalid.status==TBX_J1587_VALUE_INDISPONIVEL);
    assert(!tbx_j1587_decode(128,250,na,1,&invalid) && invalid.status==TBX_J1587_VALUE_TAMANHO_INVALIDO);
    assert(!tbx_j1587_decode(128,999,zero,1,&invalid) && invalid.status==TBX_J1587_VALUE_PID_NAO_SUPORTADO);
    puts("OK: 17 PID scales, raw retention, status bits, unavailable and malformed frames");
    return 0;
}
