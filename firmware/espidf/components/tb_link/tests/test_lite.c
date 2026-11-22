#include "tb_link_telemetry_codec.h"
#include "tb_gps_outbox.h"
#include "cJSON.h"
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
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

static void test_pid183_window(void)
{
    tb_lite_fuel_meter_t meter = {0};
    tb_lite_fuel_observe(&meter, 1000, 20);
    tb_lite_fuel_observe(&meter, 3000, 80);
    for (uint64_t at = 5000; at <= 11000; at += 2000) tb_lite_fuel_observe(&meter, at, 80);
    assert(meter.covered_ms == 10000 && fabs(meter.litres - 680.0 / 3600.0) < 1e-10);
    tb_lite_fuel_observe(&meter, 16000, 80); /* 5 s without samples is uncovered. */
    tb_lite_fuel_observe(&meter, 17000, NAN);
    tb_lite_fuel_observe(&meter, 18000, 100);
    tb_lite_fuel_observe(&meter, 17000, 500); /* stale frame cannot move the meter back. */
    assert(meter.covered_ms == 10000 && meter.at_ms == 18000 && meter.last_lph == 100);

    tb_lite_snapshot_t s = {.uid="tb-c6-test", .boot_id=1, .local_seq=4, .uptime_ms=18000,
        .age_ms=100, .valid=TB_LITE_BIT(TB_LITE_ODOMETER) | TB_LITE_BIT(TB_LITE_PID250),
        .flow={.at_ms=meter.at_ms,.covered_ms=meter.covered_ms,.microlitres=(uint64_t)llround(meter.litres*1000000)},
        .pid250_at_ms=15500, .odometer_at_ms=17300, .pid250_mid=128, .odometer_mid=140};
    s.values[TB_LITE_ODOMETER] = 100010;
    s.values[TB_LITE_PID250] = 33000;
    char wire[TB_LITE_SNAPSHOT_MAX];
    size_t bytes=tb_lite_snapshot_encode(&s, wire, sizeof(wire));
    tb_lite_snapshot_t decoded;
    assert(bytes && tb_lite_snapshot_decode(wire, bytes, &decoded));
    assert(decoded.flow.microlitres == 188889 && decoded.pid250_mid == 128 && decoded.pid250_at_ms == 15500);
    const tb_lite_fuel_counter_t first = {.at_ms=1000};
    tb_lite_fuel_window_t window;
    assert(tb_lite_fuel_delta(&first, &decoded.flow, &window) && window.span_ms == 17000 && window.covered_ms == 10000);
    assert(fabs((double)window.microlitres * 3.6 / (double)window.covered_ms - 68) < 0.0001);
    assert(!tb_lite_fuel_delta(&decoded.flow, &first, &window));
    assert(!tb_lite_fuel_delta(&decoded.flow, &decoded.flow, &window));
    char record[TB_LITE_RECORD_MAX];
    const tb_lite_reference_t ref = {0};
    assert(tb_lite_operational(record, sizeof(record), 7, epoch, &decoded, &ref));
    assert(strstr(record, "\"f183\":[18000,10000,188889]") && strstr(record, "\"a250\":[128,140,2500,700]"));
    decoded.odometer_at_ms = 1000;
    assert(tb_lite_operational(record, sizeof(record), 8, epoch, &decoded, &ref));
    assert(!strstr(record, "\"a250\""));
    decoded.odometer_at_ms = s.odometer_at_ms;
    decoded.estimated = TB_LITE_BIT(TB_LITE_PID250);
    assert(tb_lite_operational(record, sizeof(record), 9, epoch, &decoded, &ref));
    assert(!strstr(record, "\"a250\""));
}

static void test_continuous_windows(void)
{
    const uint64_t wall = UINT64_C(1795370000000);
    tb_fuel_meter_t m = {0};
    tb_fuel_tick(&m, 0, wall);
    for (unsigned second = 0; second <= 10; ++second) tb_fuel_observe(&m, second * 1000, second < 9 ? 2 : 50);
    tb_fuel_window_t page[TB_FUEL_PAGE_SIZE];
    assert(tb_fuel_page(&m, 0, page, TB_FUEL_PAGE_SIZE) == 1);
    assert(page[0].slot == 1 && page[0].covered_ms == 10000 && page[0].readings == 10);
    assert(page[0].ul == 18889 && page[0].total_ul == 18889);
    assert(page[0].min_mlph == 2000 && page[0].max_mlph == 50000 && page[0].end_utc_ms == wall + 10000);
    assert(fabs(page[0].ul * 0.00036 - 6.8) < 0.0001);

    m = (tb_fuel_meter_t){0};
    for (unsigned second = 0; second <= 8; ++second) tb_fuel_observe(&m, second * 1000, second < 4 ? 10 : 80);
    tb_fuel_observe(&m, 9250, 36);
    tb_fuel_observe_counter(&m, 9800, 1200, 128, 3);
    tb_fuel_tick(&m, 10500, 0); /* Wait for a valid pair crossing 10 s. */
    assert(!tb_fuel_page(&m, 0, page, TB_FUEL_PAGE_SIZE));
    tb_fuel_observe(&m, 11250, 90);
    assert(tb_fuel_page(&m, 0, page, TB_FUEL_PAGE_SIZE) == 1);
    assert(page[0].covered_ms == 10000 && page[0].ul == 135278);
    assert(page[0].counter.at_ms == 9800 && page[0].counter.covered_ms == 9800);
    assert(page[0].counter.observed_ul == 133278 && m.open.covered_ms == 1250);
    tb_fuel_tick(&m, 34500, wall + 34500);
    assert(tb_fuel_page(&m, 1, page, TB_FUEL_PAGE_SIZE) == 2);
    assert(page[0].slot == 2 && page[0].ul == 12500 && page[0].covered_ms == 1250);
    assert(page[1].slot == 3 && !page[1].covered_ms && !page[1].ul);
    tb_fuel_observe(&m, 11000, 20); /* rewind */
    tb_fuel_observe(&m, 35000, -1);
    tb_fuel_observe(&m, 36000, NAN);
    tb_fuel_observe(&m, 37000, 9000);
    assert(m.invalid_count == 4);
    tb_fuel_observe(&m, 38000, 60);
    tb_fuel_observe(&m, 39000, 60);
    tb_fuel_observe(&m, 40500, 60);
    assert(tb_fuel_page(&m, 3, page, TB_FUEL_PAGE_SIZE) == 1 && page[0].covered_ms == 2000);

    tb_lite_snapshot_t snapshot = {.uid="tb-c6-test",.boot_id=29,.local_seq=0,.age_ms=UINT32_MAX,
        .uptime_ms=40500,.unix_ms=wall+40500,.fuel_v2=true,.fuel_count=1};
    snapshot.fuel_windows[0] = page[0];
    char wire[TB_LITE_SNAPSHOT_MAX], record[TB_LITE_RECORD_MAX];
    size_t n = tb_lite_snapshot_encode(&snapshot, wire, sizeof(wire));
    tb_lite_snapshot_t decoded;
    assert(n && tb_lite_snapshot_decode(wire, n, &decoded) && decoded.fuel_count == 1 && decoded.fuel_v2);
    tb_lite_cursor_t cursor = {.boot_id=29};
    uint64_t captured;
    const tb_lite_reference_t ref = {0};
    assert(tb_lite_candidate(&cursor, &decoded, wall+40500, &captured) && captured == wall+40500);
    assert(tb_lite_operational(record, sizeof(record), 20, captured, &decoded, &ref) && strstr(record, "\"w183\":[[4,"));
    assert(!strstr(record, "\"fuel_rate_lph\""));

    m = (tb_fuel_meter_t){0};
    tb_fuel_observe(&m, 1000, 36);
    tb_fuel_observe(&m, 2000, 36);
    tb_fuel_trip_mark(&m, 2500);
    tb_fuel_observe(&m, 4000, 36);
    tb_lite_fuel_counter_t trip;
    assert(tb_fuel_trip_total(&m, &trip) && trip.at_ms == 2500 && trip.covered_ms == 1500 && trip.microlitres == 15000);
    tb_fuel_trip_mark(&m, 0);
    assert(!tb_fuel_trip_total(&m, &trip));

    m = (tb_fuel_meter_t){0};
    for (unsigned second = 0; second <= 86400; ++second) {
        tb_fuel_observe(&m, (uint64_t)second*1000, 36);
        if (!(second % 10)) tb_fuel_tick(&m, (uint64_t)second*1000, wall+(uint64_t)second*1000);
    }
    assert(tb_fuel_page(&m, 8639, page, TB_FUEL_PAGE_SIZE) == 1 && page[0].total_ul == 864000000);
    assert(tb_fuel_page(&m, 0, page, TB_FUEL_PAGE_SIZE) == TB_FUEL_PAGE_SIZE && page[0].slot == 8640-TB_FUEL_HISTORY+1);
    printf("PID183 9 s a 2 L/h + 1 s a 50 L/h: 18889 uL, 6.80004 L/h; fronteira, silêncio, retomada, relógio, 24 h e %zu B de acumulador PASS\n", sizeof(m));
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
    test_pid183_window();
    test_continuous_windows();
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
