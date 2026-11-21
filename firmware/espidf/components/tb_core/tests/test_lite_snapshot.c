#define TB_TELEMETRY_HOST_TEST
#define CONFIG_TB_J1708_ONLY 1
#define CONFIG_TB_CORE_LITE 1
#include "../telemetry.c"
#include <assert.h>

static void *no_heap(size_t size)
{
    (void)size;
    return NULL;
}

int main(void)
{
    assert(tb_telemetry_start()==ESP_OK);
    host_fail_store=true;
    const uint8_t speed[] = {128,84,80,(uint8_t)(0u-128u-84u-80u)};
    assert(tb_telemetry_j1708(speed,sizeof(speed),host_now_us,false));
    packet_t packet;
    assert(xQueueReceive(s.queue,&packet,0));
    process_packet(&packet);
    assert(host_record_count == 0);

    cJSON_Hooks unavailable = {no_heap,free};
    cJSON_InitHooks(&unavailable);
    tb_lite_snapshot_t status = {0};
    tb_telemetry_lite_snapshot(&status);
    assert(status.boot_id == 42 && status.local_seq == 1);
    assert(status.valid == TB_LITE_BIT(TB_LITE_SPEED) && status.age_ms == 0);
    assert(status.values[TB_LITE_SPEED] > 0);
    char output[TB_LITE_SNAPSHOT_MAX];
    assert(tb_lite_snapshot_encode(&status,output,sizeof(output)) > 0);
    assert(strstr(output,"speed_kmh") && !strstr(output,"rpm\":"));
    assert(tb_lite_snapshot_encode(&status,output,33) == 0);

    host_now_us+=6000000;
    status=(tb_lite_snapshot_t){0};
    tb_telemetry_lite_snapshot(&status);
    assert(!status.valid && status.stale == TB_LITE_BIT(TB_LITE_SPEED));
    assert(tb_lite_snapshot_encode(&status,output,sizeof(output)) > 0 && !strstr(output,"speed_kmh"));
    cJSON_InitHooks(NULL);
    vQueueDelete(s.queue);
    puts("Core Lite snapshot reads decoder cache with persistence unavailable, no NVS or cJSON allocations; stale data omitted");
}
