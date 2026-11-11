/* Synthetic truck, real production decoder/cache/serializers. No vehicle or
 * Cloud connection. 60 validated J1708 frames/s for a 2h virtual drive. */
#define TB_TELEMETRY_HOST_TEST
#define CONFIG_TB_J1708_ONLY 1
#include "../telemetry.c"
#include "../tbx_j1708_active.h"
#include "../tbx_j1708_auto.h"
#include "tbx_j1708_policy.h"
#include <assert.h>
#include <time.h>
#include "../accounting_json.h"
#include "../tbx_trip.h"
#define ESP_LOGW(tag,...) ((void)(tag))
static const char *TAG="offline";
static struct {tbx_transmission_t transmission;tbx_j1708_auto_t automatic;uint64_t transmission_snapshot_at;uint32_t transmission_last_flags,transmission_last_launches;tbx_j1708_active_t active;tbx_j1708_request_scheduler_t requests;uint32_t request_event_dropped;
    uint32_t can_frames,j1708_frames,j1708_checksum_errors,uart_overruns;
    uint64_t speed_at,rpm_at,fuel_at,total_at;
    double speed,rpm,fuel_rate,distance_total,distance_trip,fuel_trip;
    bool distance_trip_valid,fuel_trip_valid,speed_j1587,rpm_j1587,fuel_j1587,fuel_derived;
    void *lock;tbx_trip_t trip;tbx_fuel_account_t fuel_account;
} capture_fixture;
#define xSemaphoreTake(lock,wait) ((void)(lock),(void)(wait))
#define xSemaphoreGive(lock) ((void)(lock))
static bool tb_core_append_gps(cJSON *o){return tb_core_append_gps_at(o,host_now_us);}
static unsigned esp_reset_reason(void){return 1;}
static unsigned esp_get_minimum_free_heap_size(void){return 100000;}
static const char *tb_version(void){return "0.2.16";}
#define tb_config_j1708_auto_enabled() true

static int simulated_gettimeofday(struct timeval *tv,void *zone)
{
    (void)zone;const char *clock=getenv("TB_OFFLINE_CLOCK");
    if(!clock || !strcmp(clock,"0"))return -1;
    tv->tv_sec=(time_t)(1794301200+host_now_us/1000000u);tv->tv_usec=(suseconds_t)(host_now_us%1000000u);return 0;
}
#define gettimeofday(tv,tz) simulated_gettimeofday(tv,tz)
#include "offline_capture_under_test.h"
#undef gettimeofday
static FILE *output;
static unsigned exported;static bool seen_pid[512];
static void drain(void){for(unsigned i=0;i<host_record_count;i++){fprintf(output,"%s\n",host_records[i]);++exported;
        cJSON *row=cJSON_Parse(host_records[i]);assert(row);
        const char *buckets[]={"j1587_sensors","j1587_raw"};
        for(unsigned k=0;k<2;k++){const cJSON *entry;const cJSON *bucket=cJSON_GetObjectItemCaseSensitive(row,buckets[k]);cJSON_ArrayForEach(entry,bucket){unsigned pid=(unsigned)strtoul(entry->string,NULL,10);if(pid<512)seen_pid[pid]=true;}}
        cJSON_Delete(row);}host_record_count=0;}
static void synthetic_frame(uint16_t pid,uint8_t mid,uint32_t raw)
{
    packet_t p={.captured_us=host_now_us,.capture_id=++s.next_capture};
    p.bytes[p.length++]=mid;
    unsigned base=pid;
    if(pid>255){p.bytes[p.length++]=255;base=pid-256;}
    p.bytes[p.length++]=(uint8_t)base;
    unsigned count=base<128?1:base<192?2:4;
    if(pid==194)count=2;
    if(pid==251 || pid==252)count=3;
    if(base>=192)p.bytes[p.length++]=(uint8_t)count;
    for(unsigned j=0;j<count;j++)p.bytes[p.length++]=(uint8_t)(raw>>(8*j));
    unsigned sum=0;for(unsigned j=0;j<p.length;j++)sum+=p.bytes[j];
    p.bytes[p.length++]=(uint8_t)(0u-sum);p.supplied_length=p.length;
    process_packet(&p);
}
static cJSON *identity(const char *kind){cJSON *o=cJSON_CreateObject();add_sample_identity(o,kind);return o;}
static bool send_query(uint16_t pid,void *unused)
{
    (void)unused;unsigned i=0;while(tbx_j1708_policy()[i].pid!=pid)++i;
    uint8_t *tx=capture_fixture.active.tx_frame;
    tx[0]=242;tx[1]=128;tx[2]=(uint8_t)pid;tx[3]=tbx_j1708_policy()[i].mid;
    tx[4]=(uint8_t)(0u-tx[0]-tx[1]-tx[2]-tx[3]);
    if(capture_fixture.active.production_mode)return true; /* Real producer suppresses per-query success records. */
    request_event_t event={.uptime_us=host_now_us,.tx_start_us=host_now_us,
        .pid=pid,.local_mid=242,.target_mid=tx[3],.work=true,.field=true,.field_started_us=1};
    memcpy(event.frame,tx,5);strcpy(event.event,"REQUEST");strcpy(event.detail,"PID128_TARGETED_REQUEST_QUEUED");
    request_store_event(&event);return true;
}
static void response(void)
{
    unsigned i=capture_fixture.requests.pending;if(i>=17)return;
    uint16_t pid=capture_fixture.requests.pids[i].pid;
    uint8_t mid=tbx_j1708_policy()[i].mid;
    unsigned count=pid<128?1:pid<192?2:4;
    uint32_t raw=pid>=192?600000u+(uint32_t)(host_now_us/UINT64_C(20000000)):pid<128?113u:12000u;
    uint8_t bytes[4];for(unsigned j=0;j<4;j++)bytes[j]=(uint8_t)(raw>>(8*j));
    tbx_j1587_decoded_t decoded;assert(tbx_j1587_decode(mid,pid,bytes,(uint8_t)count,&decoded));
    tbx_j1587_observation_t obs={.pid=pid,.mid=mid,.uptime_ms=host_now_us/1000,.raw=bytes,.raw_len=(uint8_t)count,
        .has_numeric=decoded.numeric,.value=decoded.value,.unit=decoded.unit,.quality=TBX_J1587_OBS_EM_VALIDACAO};
    assert(tbx_j1708_request_note_response(&capture_fixture.requests,pid,(uint32_t)(host_now_us/1000)));
    tb_j1708_store_matched_response(&obs,host_now_us/1000);
    synthetic_frame(pid,mid,raw);
}
int main(int argc,char **argv)
{
    assert(argc==2);output=fopen(argv[1],"wb");assert(output);
    host_telemetry_config.full_decode_enabled=true; /* J1708_ONLY must still forbid continuous RAW. */
    host_have_gps=false;
    assert(tb_telemetry_start()==ESP_OK);
    assert(!tb_telemetry_can(0x18F00400,true,false,0,NULL,1));
    tbx_j1708_request_init(&capture_fixture.requests);
    tbx_j1708_request_permit_stationary_test(&capture_fixture.requests,true);
    assert(tbx_j1708_request_arm(&capture_fixture.requests));
    tbx_j1708_request_set_spacing(&capture_fixture.requests,500);
    tbx_j1708_request_start_periodic(&capture_fixture.requests,1000);
    capture_fixture.active.config.local_mid=172;capture_fixture.active.production_mode=true;
    capture_fixture.automatic.phase=TBX_AUTO_RUNNING;capture_fixture.automatic.reason="PERSISTENT_POLICY_OPERATIONAL_INDIRECT";
    uint32_t periods[17];for(unsigned i=0;i<17;i++)periods[i]=tbx_j1708_production_period(tbx_j1708_policy()[i].pid,false);
    tbx_j1708_request_production(&capture_fixture.requests,periods,false,1000,true);
    for(unsigned i=0;i<17;i++)capture_fixture.active.config.target_mid[i]=tbx_j1708_policy()[i].mid;
    unsigned catalog_size;const tbx_j1587_passive_spec_t *catalog=tbx_j1587_passive_specs(&catalog_size);
    for(unsigned sec=1;sec<=7200;sec++){
        bool launch=(sec%120u)<30u;capture_fixture.transmission.launch=launch;
        for(unsigned i=0;i<17;i++)periods[i]=tbx_j1708_production_period(tbx_j1708_policy()[i].pid,launch);
        tbx_j1708_request_production(&capture_fixture.requests,periods,launch,sec*1000u,false);
        for(unsigned f=0;f<60;f++){
            host_now_us=(uint64_t)sec*1000000+(uint64_t)f*16000;
            uint16_t pid=catalog[(sec*60u+f)%catalog_size].pid;
            uint32_t raw=pid<128?113u:pid>=192?600000u+sec/10u:12000u+sec%100u;
            if(pid==251)raw=0x0C1E00;
            if(pid==252)raw=0x290B04;
            synthetic_frame(pid,pid==177 || pid==162 || pid==163?130:128,raw);
            if(capture_fixture.requests.pending<17 &&
               host_now_us/1000-capture_fixture.requests.pending_since_ms>=200)response();
            tbx_j1708_request_tick(&capture_fixture.requests,(uint32_t)(host_now_us/1000),send_query,NULL);
        }
        if(sec%10==0){
            capture_fixture.speed_at=capture_fixture.rpm_at=capture_fixture.fuel_at=capture_fixture.total_at=host_now_us/1000;
            capture_fixture.speed=64.37376;capture_fixture.rpm=1025.25;capture_fixture.fuel_rate=30.16500015375;
            capture_fixture.distance_total=100000+sec*64.37376/3600.0;capture_fixture.distance_trip=sec*64.37376/3600.0;
            capture_fixture.fuel_trip=sec*30.16500015375/3600.0;
            capture_fixture.distance_trip_valid=capture_fixture.fuel_trip_valid=true;
            capture_fixture.speed_j1587=capture_fixture.rpm_j1587=capture_fixture.fuel_j1587=true;
            capture_fixture.trip.quality="DELTA_ODO_J1587_VALIDADO";
            capture_fixture.fuel_account=(tbx_fuel_account_t){.integrated_l=sec*.0075,.estimated_l=.15,.gaps=2,.unknown_gap=true,.quality="INTEGRADO_VAZAO_J1587_EM_VALIDACAO"};
            offline_operational(host_now_us/1000);
            offline_transmission(host_now_us/1000);
            size_t cursor=0;
            while(cursor<27){cJSON *fast=identity("J1708_FAST");
                cJSON_AddStringToObject(fast,"source","j1708");cJSON_AddStringToObject(fast,"origin","PASSIVE_RX");
                size_t seen=tb_telemetry_append_j1587_fast(fast,&cursor);
                if(seen)queue_sample(fast);else{cJSON_Delete(fast);break;}}
        }
        if(sec%60==0){
            capture_fixture.j1708_frames=(uint32_t)s.processed;capture_fixture.active.work_mode=true;
            cJSON *health=offline_health(host_now_us/1000);assert(health);queue_sample(health);
            size_t cursor=0;
            for(unsigned part=0;part<64;part++){
                cJSON *slow=identity("J1708_FAST");cJSON_AddStringToObject(slow,"source","j1708");cJSON_AddNumberToObject(slow,"interval_s",60);
                size_t seen=tb_telemetry_append_j1587_slow(slow,&cursor);
                if(seen)queue_sample(slow);else{cJSON_Delete(slow);break;}
            }
        }
        drain();
    }
    assert(capture_fixture.requests.sent_total>4500 && capture_fixture.requests.sent_total==capture_fixture.requests.answered_total);
    for(unsigned i=0;i<17;i++){unsigned expected=7200000u/tbx_j1708_production_period(capture_fixture.requests.pids[i].pid,false);assert(capture_fixture.requests.pids[i].sent+1>=expected);}
    for(unsigned i=0;i<catalog_size;i++)assert(catalog[i].pid<512 && seen_pid[catalog[i].pid]);
    for(unsigned i=0;i<17;i++)assert(seen_pid[tbx_j1708_policy()[i].pid]);
    assert(s.latest_evictions==0);
    assert(s.processed>=432000 && s.store_accepted==0 && s.serialization_errors==0);
    fprintf(stderr,"Offline source: clock=%s virtual_seconds=7200 frames=%llu records=%u requests=%u response_candidates=%u latest=%zu RAW_writes=%llu\n",
        getenv("TB_OFFLINE_CLOCK")?getenv("TB_OFFLINE_CLOCK"):"0",(unsigned long long)s.processed,exported,capture_fixture.requests.sent_total,capture_fixture.requests.answered_total,s.latest_count,(unsigned long long)s.store_accepted);
    vQueueDelete(s.queue);fclose(output);return 0;
}
