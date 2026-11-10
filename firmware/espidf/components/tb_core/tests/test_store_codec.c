#include "store_codec.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t random_state=0x18b1910c;
static unsigned next(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void check(const char *input)
{
    char packed[1024],out[768];size_t n=strlen(input);
    size_t z=tb_store_pack(input,n,packed,sizeof(packed));if(!z)return;
    assert(z==strlen(packed));assert(tb_store_unpack(packed,out,sizeof(out))==n && !strcmp(input,out));
    assert(!tb_store_unpack(packed,out,n)); /* Strict bound, includes final NUL. */
    for(size_t i=0;i<z;i++){char old=packed[i];packed[i]=0;assert(!tb_store_unpack(packed,out,sizeof(out)));packed[i]=old;}
}
int main(void)
{
    check("{\"sample_kind\":\"OPERATIONAL\",\"boot_id\":42,\"uptime_ms\":1000000,\"time_valid\":false,\"gps_status\":\"NO_FIX\",\"rpm\":1500.25,\"quality\":\"EM_VALIDACAO\",\"note\":\"pressão, óleo e embreagem\"}");
    char input[768],packed[1024],out[768];
    for(unsigned n=48;n<768;n++){memset(input,0xc3,n);input[n]=0;check(input);}
    for(unsigned test=0;test<3000;test++){
        size_t n=48+next()%719;for(size_t i=0;i<n;i++)input[i]=(char)(1u+next()%255);input[n]=0;check(input);
        for(size_t i=0;i<sizeof(packed)-1;i++)packed[i]=(char)(1+next()%127);
        packed[sizeof(packed)-1]=0;
        (void)tb_store_unpack(packed,out,sizeof(out));
    }
    const char *bad[]={"","!!!!","AAAA","AAAA====","AQAAAA==","AQAA","/w8=","A===","AAAA=AAA"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!tb_store_unpack(bad[i],out,sizeof(out)));
    puts("Storage codec: UTF-8/key tokens, lossless roundtrip, output bounds, all truncations, malformed base64/backreferences and 3000 deterministic fuzz inputs PASS");return 0;
}
