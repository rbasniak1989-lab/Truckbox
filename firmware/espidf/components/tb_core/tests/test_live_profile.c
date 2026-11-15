#define TB_TELEMETRY_HOST_TEST
#define CONFIG_TB_J1708_ONLY 1
#include "../telemetry.c"
#include <assert.h>
int main(void)
{
    assert(tb_telemetry_start()==ESP_OK);
    assert(TELEMETRY_LATEST==128 && sizeof(((latest_t *)0)->raw)==17);
    const uint8_t payload[]={0x12,0xb5,1,0x12,0xb5,2,0x12,0xb5,3,0x12,0xb5,4,0x12,0xb5,5,2,0x35};
    packet_t p={.length=21,.supplied_length=21,.captured_us=host_now_us,.capture_id=1};
    p.bytes[0]=128;p.bytes[1]=194;p.bytes[2]=17;memcpy(p.bytes+3,payload,sizeof(payload));
    unsigned sum=0;for(unsigned i=0;i<20;i++)sum+=p.bytes[i];p.bytes[20]=(uint8_t)(0u-sum);
    process_packet(&p);assert(s.latest_count==1);
    assert(s.latest[0].raw_length==17 && s.latest[0].full_raw_length==17 && !memcmp(s.latest[0].raw,payload,17));
    printf("J1708_ONLY cache:128 entries, complete maximum17-byte PID payload retained; latest_t=%zuB, no capacity reduction PASS\n",sizeof(latest_t));
    vQueueDelete(s.queue);return 0;
}
