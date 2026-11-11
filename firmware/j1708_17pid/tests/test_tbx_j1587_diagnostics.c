#include "../src/tbx_j1587_diagnostics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static tbx_dtc_t codes[120];static size_t code_count;
static void collect(const tbx_dtc_t *d,void *unused){(void)unused;codes[code_count++]=*d;}
static void parse(const uint8_t *p,size_t n){code_count=0;size_t count=0;assert(tbx_j1587_dtc_parse(p,n,collect,NULL,&count));assert(count==code_count);}
static unsigned assembled;static uint8_t assembled_mid;static uint8_t assembled_bytes[239];
static void complete(uint8_t mid,const uint8_t *p,size_t n,uint64_t first,uint64_t last,void *unused)
{(void)unused;assert(first<=last && n==18);++assembled;assembled_mid=mid;memcpy(assembled_bytes,p,n);}
int main(void)
{
    /* NEXIQ value examples preserved in PASSIVE.md, and Cloud seq58610/58611,
     * 11 Nov2026. These exclude the variable-length prefix by design. */
    const uint8_t a[]={0x12,0xb5,1},b[]={2,0x35},c[]={0x25,0xa1,1},inactive[]={0x25,0xe1,0};
    parse(a,sizeof(a));assert(codes[0].identifier==18 && codes[0].sid && codes[0].fmi==5 && codes[0].active && codes[0].has_occurrences && codes[0].occurrences==1);
    parse(b,sizeof(b));assert(codes[0].identifier==2 && codes[0].sid && codes[0].active && !codes[0].has_occurrences);
    parse(c,sizeof(c));assert(codes[0].identifier==37 && !codes[0].sid && codes[0].fmi==1 && codes[0].active);
    parse(inactive,sizeof(inactive));assert(!codes[0].active && codes[0].has_occurrences && codes[0].occurrences==0);
    const uint8_t multi[]={0x12,0xb5,1,2,0x35,0x25,0xe1,0};parse(multi,sizeof(multi));assert(code_count==3);
    const uint8_t page2[]={0xb7,0x05,0x10,0x15};parse(page2,sizeof(page2));
    assert(codes[0].identifier==439 && codes[0].page2 && !codes[0].sid);
    assert(codes[1].identifier==272 && codes[1].page2 && codes[1].sid);
    assert(tbx_j1587_dtc_parse(NULL,0,NULL,NULL,NULL));
    assert(!tbx_j1587_dtc_parse(a,2,NULL,NULL,NULL));assert(!tbx_j1587_dtc_parse(a,1,NULL,NULL,NULL));
    uint8_t many[238];for(unsigned i=0;i<119;i++){many[2*i]=(uint8_t)i;many[2*i+1]=0x25;}
    parse(many,sizeof(many));assert(code_count==119);
    tbx_dtc_history_t h={0};tbx_dtc_time_t at={306,1000,0};
    assert(tbx_dtc_history_observe(&h,128,a,3,at));assert(tbx_dtc_history_observe(&h,136,b,2,at));
    assert(tbx_dtc_history_observe(&h,130,c,3,at));assert(h.entries[0].mid==128 && h.entries[1].mid==136 && h.entries[2].mid==130);
    for(unsigned i=0;i<3;i++)h.entries[i].dirty=h.entries[i].notify=false;
    ++at.uptime_ms;assert(tbx_dtc_history_observe(&h,128,a,3,at));assert(!h.entries[0].dirty && !h.entries[0].notify);
    assert(tbx_dtc_history_observe(&h,130,inactive,3,at));assert(h.entries[2].transitions==1 && !h.entries[2].code.active && h.entries[2].code.has_occurrences && h.entries[2].code.occurrences==0);
    assert(tbx_dtc_history_observe(&h,128,NULL,0,at));assert(h.entries[0].code.active);
    assert(!tbx_dtc_history_observe(&h,128,a,2,at));assert(h.entries[0].code.active && h.invalid_messages==1);
    tbx_dtc_history_entry_t before=h.entries[0];assert(tbx_dtc_history_observe(&h,144,many,sizeof(many),at));
    assert(h.capacity_rejected && !memcmp(&before,&h.entries[0],sizeof(before))); /* never replace another fault */
    tbx_dtc_sections_t sections={0};
    uint8_t first[]={194,0x10,18,0x12,0xb5,1,2,0x35,0x25,0xe1,0,1,0x21,2,0x22,3,0x23};
    uint8_t last[]={194,0x11,4,0x24,5,0x25};
    assert(tbx_dtc_section_feed(&sections,128,first,sizeof(first),1000,complete,NULL));
    assert(tbx_dtc_section_feed(&sections,130,first,sizeof(first),1001,complete,NULL));
    assert(tbx_dtc_section_feed(&sections,128,last,sizeof(last),1100,complete,NULL));assert(assembled==1 && assembled_mid==128);
    assert(tbx_dtc_section_feed(&sections,130,last,sizeof(last),1101,complete,NULL));assert(assembled==2 && assembled_mid==130);
    assert(!tbx_dtc_section_feed(&sections,136,last,sizeof(last),1200,complete,NULL));
    assert(tbx_dtc_section_feed(&sections,128,first,sizeof(first),2000,complete,NULL));
    tbx_dtc_section_expire(&sections,8001);assert(sections.timeouts==1);
    first[1]=0;assert(!tbx_dtc_section_feed(&sections,128,first,sizeof(first),9000,complete,NULL));
    puts("PID194: actual A/B/C/D, page2 PID/SID, 119-code parser, unknowns, states/history/no absent-clear, bounded no-overwrite and concurrent PID192 assembly PASS");
    return 0;
}
