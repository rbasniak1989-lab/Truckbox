#include "tb_link_telemetry_codec.h"
#include "tb_gps_outbox.h"
#include "cJSON.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint64_t epoch = TB_LITE_EPOCH_MS + UINT64_C(90900000000);

static void *no_alloc(size_t size) { (void)size; return NULL; }

static void test_snapshot(void)
{
    tb_lite_snapshot_t in = {.uid="tb-c6-test",.boot_id=17,.local_seq=20,.uptime_ms=9000,.unix_ms=epoch,
                             .age_ms=200,.valid=TB_LITE_BIT(TB_LITE_SPEED) | TB_LITE_BIT(TB_LITE_PID250),
                             .stale=TB_LITE_BIT(TB_LITE_RPM),.estimated=TB_LITE_BIT(TB_LITE_PID250)};
    in.values[TB_LITE_SPEED]=76.5;
    in.values[TB_LITE_PID250]=10000;
    in.reset_reason=3;
    in.reset_reason_valid=true;
    char wire[TB_LITE_SNAPSHOT_MAX];
    size_t bytes=tb_lite_snapshot_encode(&in,wire,sizeof(wire));
    assert(bytes>0);
    tb_lite_snapshot_t s;
    assert(tb_lite_snapshot_decode(wire,bytes,&s));
    assert(s.valid==in.valid && s.stale==in.stale && s.estimated==in.estimated);
    assert(s.reset_reason_valid && s.reset_reason == 3);
    assert(s.values[TB_LITE_SPEED] == 76.5 && !strstr(wire,"\"rpm\":"));
    cJSON_Hooks fail = {no_alloc,free};
    cJSON_InitHooks(&fail);
    assert(!tb_lite_snapshot_decode(wire,bytes,&s));
    cJSON_InitHooks(NULL);

    tb_lite_cursor_t cursor = {0};
    uint64_t captured;
    assert(tb_lite_candidate(&cursor,&in,0,&captured) && captured==epoch-200);
    assert(tb_lite_candidate(&cursor,&in,epoch,&captured) && captured==epoch-200);
    cursor=(tb_lite_cursor_t){in.boot_id,in.local_seq,captured};
    assert(!tb_lite_candidate(&cursor,&in,epoch,&captured));
    assert(!tb_lite_candidate(&cursor,&in,epoch+120000,&captured));
    in.boot_id=1;in.local_seq=1;in.unix_ms=epoch+120000;
    assert(tb_lite_candidate(&cursor,&in,epoch+120000,&captured));
    in.unix_ms=0;
    assert(tb_lite_candidate(&cursor,&in,epoch+120000,&captured));
    assert(tb_lite_candidate(&cursor,&in,0,&captured) && !captured);
    in.valid=0;
    assert(!tb_lite_candidate(&cursor,&in,epoch+120000,&captured));
}

static void test_fuel_and_capacity(void)
{
    tb_lite_snapshot_t s = {.uid="tb-c6-test",.boot_id=1,.local_seq=1,.uptime_ms=120000,.valid=TB_LITE_ALL};
    for (unsigned i=0;i<TB_LITE_FIELDS;i++) s.values[i] = 123456.12345;
    s.values[TB_LITE_ODOMETER]=10000;
    s.values[TB_LITE_PID250]=1000;
    tb_lite_reference_t ref = {0};
    assert(tb_lite_reference_update(&ref,&s,epoch));
    assert(tb_lite_reference_valid(&ref));
    assert(!tb_lite_reference_update(&ref,&s,epoch+60000));
    char record[TB_LITE_RECORD_MAX];
    s.values[TB_LITE_ODOMETER]+=5;
    s.values[TB_LITE_PID250]+=2;
    assert(tb_lite_operational(record,sizeof(record),TB_LITE_SAFE_SEQ,epoch+120000,&s,&ref));
    assert(strstr(record,"\"trip_fuel_l\":2") && strstr(record,"CONFIRMED_ECU_PID250") && strstr(record,"\"trip_average_kml\":2.5"));
    s.valid &= ~TB_LITE_BIT(TB_LITE_PID250);
    assert(tb_lite_operational(record,sizeof(record),123,epoch+120000,&s,&ref));
    assert(!strstr(record,"\"trip_fuel_l\"") && strstr(record,"ESTIMATE_FROM_RATE_PID183"));
    s.valid |= TB_LITE_BIT(TB_LITE_PID250);
    s.values[TB_LITE_PID250]=900;
    assert(tb_lite_operational(record,sizeof(record),123,epoch+120000,&s,&ref));
    assert(!strstr(record,"\"trip_fuel_l\""));
    assert(!tb_lite_operational(record,20,123,epoch,&s,&ref));
    assert(tb_lite_operational(record,sizeof(record),123,0,&s,&ref));
    assert(strstr(record,"\"sample_kind\":\"DECODED\"") && strstr(record,"\"time_valid\":false"));
    assert(!strstr(record,"\"trip_fuel_l\""));
}

static void test_ack_and_spool(void)
{
    char dir[]="/tmp/tblite.XXXXXX";
    assert(mkdtemp(dir));
    char path[128], backup[132];
    snprintf(path,sizeof(path),"%s/telemetry.log",dir);
    snprintf(backup,sizeof(backup),"%s.bak",path);
    tb_gps_outbox_t box;
    const char sample[]="{\"seq\":1000000,\"sample_kind\":\"OPERATIONAL\"}";
    const char health[]="{\"seq\":1000001,\"sample_kind\":\"HEALTH\"}";
    assert(!tb_telemetry_outbox_init(&box,path,4096));
    assert(!tb_gps_outbox_append(&box,sample));
    assert(!tb_gps_outbox_append(&box,health));
    char record[TB_LITE_RECORD_MAX];
    uint64_t seq;bool is_health;
    const char ack[]="{\"ok\":true,\"accepted\":1,\"ack_seq\":1000000}";
    const char wrong[]="{\"ok\":true,\"accepted\":1,\"ack_seq\":1000001}";
    assert(!tb_gps_outbox_peek(&box,record,sizeof(record)) && tb_lite_record_meta(record,&seq,&is_health,NULL) && !is_health);
    assert(!tb_lite_ack_valid(wrong,strlen(wrong),200,seq,1,0));
    assert(!tb_lite_ack_valid(ack,strlen(ack),503,seq,1,0));
    const char ambiguous[]="{\"ok\":true,\"accepted\":1,\"ack_seq\":1000000,\"ack_seq\":1000001}";
    assert(!tb_lite_ack_valid(ambiguous,strlen(ambiguous),200,seq,1,0));
    assert(!tb_lite_ack_valid("{\"ok\":true}",11,200,seq,1,0));
    tb_gps_outbox_retry(&box);
    assert(!tb_telemetry_outbox_init(&box,path,4096) && tb_gps_outbox_pending(&box)==2);
    assert(!tb_gps_outbox_peek(&box,record,sizeof(record)) && !strcmp(sample,record));
    assert(tb_lite_ack_valid(ack,strlen(ack),200,seq,1,0));
    assert(!tb_gps_outbox_ack(&box));
    assert(tb_gps_outbox_ack(&box)==EINVAL);
    FILE *tail=fopen(path,"ab");assert(tail);fputs("P torn",tail);fclose(tail);
    assert(!tb_telemetry_outbox_init(&box,path,4096) && box.recovered_tail_bytes==6);
    assert(!rename(path,backup)); /* interrupted compaction before replacement */
    assert(!tb_telemetry_outbox_init(&box,path,4096) && tb_gps_outbox_pending(&box)==1);
    assert(!tb_gps_outbox_peek(&box,record,sizeof(record)) && tb_lite_record_meta(record,&seq,&is_health,NULL) && is_health);
    assert(!tb_lite_ack_valid(ack,strlen(ack),200,seq,0,1));
    const char health_ack[]="{\"ok\":true,\"accepted\":0,\"health\":1,\"ack_seq\":1000001}";
    assert(tb_lite_ack_valid(health_ack,strlen(health_ack),200,seq,0,1));
    assert(!tb_gps_outbox_ack(&box));
    for (unsigned i=0;i<500;i++) { /* accelerate a multi-hour enqueue/ACK cycle */
        snprintf(record,sizeof(record),"{\"seq\":%u,\"sample_kind\":\"OPERATIONAL\"}",1000002u+i);
        assert(!tb_gps_outbox_append(&box,record));
        assert(!tb_gps_outbox_peek(&box,record,sizeof(record)));
        assert(!tb_gps_outbox_ack(&box));
    }
    assert(!tb_telemetry_outbox_init(&box,path,4096) && tb_gps_outbox_pending(&box)==0);
    unsigned added=0;
    while (!tb_gps_outbox_append(&box,sample)) ++added;
    assert(added>0 && tb_gps_outbox_pending(&box)==added && box.rejected_records==1);
    assert(tb_gps_outbox_init(&box,path,4096)==EINVAL); /* GPS format cannot own the telemetry path */
    assert(!tb_telemetry_outbox_init(&box,path,4096) && tb_gps_outbox_pending(&box)==added);
    assert(!unlink(path) && !rmdir(dir));
}

int main(void)
{
    test_snapshot();
    test_fuel_and_capacity();
    test_ack_and_spool();
    uint64_t first,end,next,last=0;
    assert(!tb_lite_sequence_range(0,0,&first,&end));
    for(unsigned i=0;i<2000;i++) {
        assert(tb_lite_sequence_range(1000000,last,&first,&end) && end>=first && first>last);
        last=end; /* reboot: skip IDs committed to the reservation but not used */
    }
    assert(tb_lite_sequence_range(last+500,last,&next,&end) && next==last+500);
    assert(!tb_lite_sequence_range(1,TB_LITE_SAFE_SEQ-1,&next,&end));
    puts("Link Lite: bounded snapshot/low memory, clock+boot changes, PID250 fallback, outage/retry, exact ACK, restart/torn tail, compaction, quota and sequence ranges passed");
}
