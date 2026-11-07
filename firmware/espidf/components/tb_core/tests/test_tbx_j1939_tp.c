#include "tbx_j1939_tp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned emitted;
static tbx_j1939_tp_message_t last;
static void complete(const tbx_j1939_tp_message_t *m,void *ctx)
{
    assert(ctx==&emitted);last=*m;++emitted;
}
static uint32_t can(uint32_t pgn,unsigned sa,unsigned da)
{
    return UINT32_C(0x18000000)|(pgn<<8)|(da<<8)|sa;
}
static tbx_j1939_tp_result_t frame(tbx_j1939_tp_t *t,uint32_t pgn,unsigned sa,unsigned da,
                                    const uint8_t d[8],uint64_t ms)
{
    return tbx_j1939_tp_feed(t,can(pgn,sa,da),true,false,d,8,ms,complete,&emitted);
}
static tbx_j1939_tp_result_t bam(tbx_j1939_tp_t *t,unsigned sa,uint32_t pgn,unsigned length,uint64_t ms)
{
    uint8_t d[8]={0x20,(uint8_t)length,(uint8_t)(length>>8),(uint8_t)((length+6)/7),0xff,
        (uint8_t)pgn,(uint8_t)(pgn>>8),(uint8_t)(pgn>>16)};
    return frame(t,60416,sa,255,d,ms);
}
static tbx_j1939_tp_result_t packet(tbx_j1939_tp_t *t,unsigned sa,unsigned seq,
                                      const uint8_t *payload,unsigned size,uint64_t ms)
{
    uint8_t d[8];memset(d,0xff,8);d[0]=(uint8_t)seq;
    unsigned off=(seq-1)*7;
    if(off<size) {unsigned n=size-off;if(n>7)n=7;memcpy(d+1,payload+off,n);}
    return frame(t,60160,sa,255,d,ms);
}
int main(void)
{
    tbx_j1939_tp_t t;tbx_j1939_tp_init(&t);
    uint8_t engine[30],retarder[19];
    for(unsigned i=0;i<30;++i)engine[i]=(uint8_t)(i+1);
    for(unsigned i=0;i<19;++i)retarder[i]=(uint8_t)(100+i);
    assert(bam(&t,0,65251,30,100)==TBX_J1939_TP_STARTED);
    assert(bam(&t,15,65249,19,105)==TBX_J1939_TP_STARTED);
    for(unsigned seq=1;seq<=5;++seq) {
        tbx_j1939_tp_result_t r=packet(&t,0,seq,engine,30,100+seq*10);
        assert(r==(seq==5?TBX_J1939_TP_COMPLETE:TBX_J1939_TP_PROGRESS));
        if(seq<=3) {
            r=packet(&t,15,seq,retarder,19,105+seq*10);
            assert(r==(seq==3?TBX_J1939_TP_COMPLETE:TBX_J1939_TP_PROGRESS));
            if(seq==3) {assert(last.pgn==65249 && last.length==19 && last.sa==15);
                assert(memcmp(last.data,retarder,19)==0);assert(last.completed_ms==135);}
        }
    }
    assert(emitted==2 && t.completed==2 && last.pgn==65251 && last.length==30 && last.sa==0);
    assert(last.start_ms==100 && last.completed_ms==150 && last.packets==5 && last.da==255);
    assert(memcmp(last.data,engine,30)==0);
    /* Wrong/duplicate sequence cannot contaminate a subsequent completed message. */
    assert(bam(&t,0,65251,30,200)==TBX_J1939_TP_STARTED);
    assert(packet(&t,0,2,engine,30,210)==TBX_J1939_TP_REJECTED);
    assert(t.invalid_sequence==1);
    assert(packet(&t,0,1,engine,30,220)==TBX_J1939_TP_REJECTED);
    assert(t.orphan_packets==1 && emitted==2);
    bam(&t,0,65251,30,300);packet(&t,0,1,engine,30,310);
    assert(packet(&t,0,1,engine,30,320)==TBX_J1939_TP_REJECTED);
    assert(t.invalid_sequence==2);
    bam(&t,0,65251,30,400);
    assert(packet(&t,0,1,engine,30,1651)==TBX_J1939_TP_REJECTED);
    assert(t.timeouts==1);
    bam(&t,0,65251,30,2000);
    tbx_j1939_tp_expire(&t,1000);assert(t.timeouts==2);
    /* Packet and size limits, reserved BAM field and length agreement. */
    assert(bam(&t,0,65251,65,2100)==TBX_J1939_TP_REJECTED);
    assert(bam(&t,0,65251,8,2200)==TBX_J1939_TP_REJECTED);
    uint8_t d[8]={0x20,30,0,4,0xff,0xe3,0xfe,0};
    assert(frame(&t,60416,0,255,d,2300)==TBX_J1939_TP_REJECTED);
    d[3]=5;d[4]=0;
    assert(frame(&t,60416,0,255,d,2400)==TBX_J1939_TP_REJECTED);
    d[4]=0xff;
    assert(frame(&t,60416,0,254,d,2500)==TBX_J1939_TP_REJECTED);
    assert(t.invalid_destination==1);
    d[0]=0x10;
    assert(frame(&t,60416,0,255,d,2600)==TBX_J1939_TP_REJECTED);
    assert(t.unsupported_control==1);
    assert(bam(&t,0,65250,30,2700)==TBX_J1939_TP_IGNORED && t.unsupported_pgn==1);
    assert(bam(&t,0,65251,30,2800)==TBX_J1939_TP_STARTED);
    assert(tbx_j1939_tp_feed(&t,can(60160,0,255),true,false,d,7,2810,complete,&emitted)==TBX_J1939_TP_REJECTED);
    assert(packet(&t,0,1,engine,30,2820)==TBX_J1939_TP_REJECTED);
    /* A new source announcement supersedes an incomplete one, never concatenates. */
    bam(&t,0,65251,30,2900);packet(&t,0,1,engine,30,2910);
    unsigned restarts=t.restarted;
    bam(&t,0,65249,19,2920);assert(t.restarted==restarts+1);
    for(unsigned seq=1;seq<=3;++seq)packet(&t,0,seq,retarder,19,2920+seq*10);
    assert(last.pgn==65249 && memcmp(last.data,retarder,19)==0 && emitted==3);
    /* Four independent sessions; a fifth is refused without evicting the first. */
    tbx_j1939_tp_init(&t);
    for(unsigned sa=0;sa<4;++sa)assert(bam(&t,sa,65251,30,3000)==TBX_J1939_TP_STARTED);
    assert(bam(&t,4,65251,30,3000)==TBX_J1939_TP_REJECTED && t.capacity_rejected==1);
    for(unsigned seq=1;seq<=5;++seq)packet(&t,0,seq,engine,30,3000+seq);
    assert(last.sa==0 && memcmp(last.data,engine,30)==0);
    assert(bam(&t,4,65251,30,3010)==TBX_J1939_TP_STARTED);
    assert(tbx_j1939_tp_feed(&t,0x123,false,false,d,8,3020,complete,&emitted)==TBX_J1939_TP_IGNORED);
    assert(tbx_j1939_tp_feed(&t,can(60416,5,255),true,true,d,8,3020,complete,&emitted)==TBX_J1939_TP_IGNORED);
    assert(tbx_j1939_tp_feed(&t,UINT32_MAX,true,false,d,8,3020,complete,&emitted)==TBX_J1939_TP_IGNORED);
    tbx_j1939_tp_init(&t);
    uint8_t cm[8]={0x20,19,0,3,0xff,0xe1,0xfe,0};
    assert(tbx_j1939_tp_feed_capture(&t,can(60416,15,255),true,false,cm,8,4000,9100,complete,&emitted)==TBX_J1939_TP_STARTED);
    for(unsigned seq=1;seq<=3;++seq) {
        uint8_t dt[8];memset(dt,0xff,8);dt[0]=(uint8_t)seq;
        unsigned off=(seq-1)*7,n=19-off;if(n>7)n=7;memcpy(dt+1,retarder+off,n);
        tbx_j1939_tp_feed_capture(&t,can(60160,15,255),true,false,dt,8,4000,9100+seq,complete,&emitted);
    }
    assert(last.start_capture_id==9100 && last.completed_capture_id==9103);
    puts("J1939 TP.BAM: complete30/19byte, interleaving, bounds, seq, timeout, capacity PASS");
}
