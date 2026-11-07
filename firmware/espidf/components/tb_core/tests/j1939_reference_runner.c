#include "tbx_j1939.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void emit(const tbx_j1939_observation_t *o,void *ctx)
{
    (void)ctx;
    printf("%u\tvalue\t%d\t%d\t%.17g\t%s\t%d\t%u\n",o->signal->id,o->quality,o->has_value,o->value,o->text,o->validation,o->raw_only);
    for(unsigned i=0;i<o->component_count;++i){const tbx_j1939_component_t *c=&o->components[i];
        printf("%u\t%s\t%d\t%d\t%.17g\t\t%d\t%u\n",o->signal->id,c->name,c->quality,c->has_value,c->value,o->validation,o->raw_only);
    }
}
int main(int argc,char **argv)
{
    if(argc!=4)return 2;
    unsigned long pgn=strtoul(argv[1],NULL,10),sa=strtoul(argv[2],NULL,10);
    size_t n=strlen(argv[3]);if(n%2 || n>TBX_J1939_PAYLOAD_MAX*2u)return 2;
    uint8_t d[TBX_J1939_PAYLOAD_MAX];
    for(size_t i=0;i<n/2;++i){unsigned v;if(sscanf(argv[3]+i*2,"%2x",&v)!=1)return 2;d[i]=(uint8_t)v;}
    tbx_j1939_t s;tbx_j1939_init(&s);
    if(n>16)tbx_j1939_decode_payload(&s,(uint32_t)pgn,(uint8_t)sa,d,n/2,1000,emit,NULL);
    else tbx_j1939_decode(&s,UINT32_C(0x18000000)|((uint32_t)pgn<<8)|(uint8_t)sa,true,false,d,n/2,1000,emit,NULL);
}
