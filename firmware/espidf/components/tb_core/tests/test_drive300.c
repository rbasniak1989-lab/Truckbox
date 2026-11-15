/* 300km virtual drive: actual capture/decoder/active scheduler/fuel/NVS paths.
 * UART, UTC, NVS and remote ACK are simulated; no vehicle/network writes. */
#define main capture_regression_main
#include "test_capture_integration.c"
#undef main
#include "tbx_j1708_policy.h"
#include "accounting_json.h"
static FILE *drive_output;
static uint64_t elapsed_ms,boot_at_ms;
static uint32_t sent_total[27],answered_total[27],timeouts_total[27];
static unsigned boot_count,record_count,polling_boots;
static uint64_t last_reply_tx;
static double prior_distance,prior_ecu;
static bool skip_pending;
static const uint64_t utc_origin=1794700000000ULL;
static esp_err_t write_record(const char *text)
{
    cJSON *o=cJSON_Parse(text);assert(o);
    cJSON_AddNumberToObject(o,"replay_elapsed_ms",(double)elapsed_ms);
    char b[TB_RECORD_MAX];assert(cJSON_PrintPreallocated(o,b,sizeof(b),false));
    fprintf(drive_output,"%s\n",b);cJSON_Delete(o);++record_count;return ESP_OK;
}
static void wire(uint8_t mid,uint16_t pid,uint32_t raw)
{
    uint8_t frame[10];unsigned n=0,base=pid%256;
    frame[n++]=mid;if(pid>255)frame[n++]=255;frame[n++]=(uint8_t)base;
    unsigned count=base<128?1:base<192?2:4;
    if(base>=192)frame[n++]=(uint8_t)count;
    for(unsigned i=0;i<count;i++)frame[n++]=(uint8_t)(raw>>(8*i));
    uint8_t sum=0;for(unsigned i=0;i<n;i++)sum+=frame[i];frame[n++]=(uint8_t)(0u-sum);
    finish_j1708(frame,n,false);
}
static uint32_t raw_fuel(void)
{return 2000000u+(uint32_t)floor((304*.059147059125*(double)elapsed_ms/3600000.0)/.473176473);}
static uint32_t raw_odometer(void)
{return 7000000u+(uint32_t)floor((90.0*(double)elapsed_ms/3600000.0)/.1609344);}
static void start_controller(void)
{
    const tbx_j1708_active_ops_t ops={.begin_tx=proof_begin,.abort_tx=proof_abort};
    tbx_j1708_active_init(&s.active,&s.requests,&ops,NULL);
    s.j1708_ready=s.edge_ready=true;last_reply_tx=0;skip_pending=false;
}
static void save_totals(void)
{for(unsigned i=0;i<27;i++){sent_total[i]+=s.requests.pids[i].sent;answered_total[i]+=s.requests.pids[i].answered;timeouts_total[i]+=s.requests.pids[i].timed_out;}}
static void record(unsigned reset_reason)
{
    cJSON *o=cJSON_CreateObject();assert(o);
    cJSON_AddStringToObject(o,"sample_kind",reset_reason?"BOOT_DIAGNOSTIC":"OPERATIONAL");
    cJSON_AddNumberToObject(o,"boot_id",host_boot_id);cJSON_AddNumberToObject(o,"uptime_ms",(double)(elapsed_ms-boot_at_ms));
    cJSON_AddNumberToObject(o,"reset_reason",reset_reason);cJSON_AddNumberToObject(o,"odometer_km",s.distance_total);
    cJSON_AddNumberToObject(o,"trip_distance_km",s.distance_trip);cJSON_AddNumberToObject(o,"trip_fuel_l",s.fuel_trip);
    tb_accounting_json_compact(o,&s.fuel_account);
    double km,l,avg;if(tbx_fuel_average(&s.fuel_account,&km,&l,&avg)){
        assert(km>=0 && l>0 && avg>=0 && fabs(avg-km/l)<1e-9);
        cJSON_AddNumberToObject(o,"paired_distance_km",km);cJSON_AddNumberToObject(o,"paired_ecu_l",l);
        prior_ecu=l;
    }
    assert(s.distance_trip+1e-7>=prior_distance);prior_distance=s.distance_trip;
    char text[TB_RECORD_MAX];assert(cJSON_PrintPreallocated(o,text,sizeof(text),false));cJSON_Delete(o);assert(write_record(text)==ESP_OK);
}
static void restart(unsigned reason)
{
    save_totals();assert(persist_trip()==ESP_OK);
    double before_fuel=s.fuel_trip,before_distance=s.distance_trip;
    tbx_fuel_span_t span=s.fuel_account.span;
    memset(&s,0,sizeof(s));host_mutexes=host_held=0;host_boot_id++;boot_count++;
    boot_at_ms=elapsed_ms;host_now_us=0;host_wall_origin_ms=utc_origin+elapsed_ms;
    assert(host_start_restore()==ESP_OK);
    close_to(s.fuel_trip,before_fuel);close_to(s.distance_trip,before_distance);
    assert(!memcmp(&s.fuel_account.span,&span,sizeof(span)));
    assert(host_capture_config.j1708_local_mid==188 && host_auto_enabled);
    start_controller();record(reason);
}
int main(int argc,char **argv)
{
    assert(argc==2);drive_output=fopen(argv[1],"wb");assert(drive_output);
    boot_with_checkpoint(NULL,0);host_submit_hook=write_record;host_auto_enabled=true;
    host_wall_origin_ms=utc_origin;host_capture_config.j1708_enabled=true;
    host_capture_config.j1708_local_mid=188;host_capture_config.j1587_odometer_mid=host_capture_config.j1587_fuel250_mid=128;
    /* Protocol reference is used without falsely changing physical flags. */
    host_capture_config.j1587_odometer_validated=false;host_capture_config.j1587_fuel250_validated=false;
    start_controller();unsigned credit=0;uint32_t first250[4]={0},last117=0,last118=0,last250=0;
    for(elapsed_ms=10;elapsed_ms<=12000000;elapsed_ms+=10){
        host_now_us=(int64_t)(elapsed_ms-boot_at_ms)*1000;
        if(s.active.tx_active){
            uint8_t echo[6];unsigned n=s.active.tx_length;memcpy(echo,s.active.tx_frame,n);
            tbx_j1708_active_note_rx(&s.active,echo,n,(uint64_t)host_now_us);finish_j1708(echo,n,false);
        }
        if(s.requests.pending<27){
            unsigned p=s.requests.pending;uint16_t pid=s.requests.pids[p].pid;
            if(last_reply_tx!=s.active.tx_started_us){last_reply_tx=s.active.tx_started_us;skip_pending=pid==117 && s.requests.pids[p].sent%17==0;}
            if(!skip_pending && (uint64_t)host_now_us>=s.active.tx_started_us+210000){
                uint32_t value=pid==250?raw_fuel():pid==117?80:pid==118?82:pid>=192?2000000:pid<128?16:8000;
                wire(s.active.config.target_mid[p],pid,value);
                if(pid==250){if(!first250[boot_count]){first250[boot_count]=(uint32_t)(elapsed_ms-boot_at_ms);polling_boots++;}last250=(uint32_t)elapsed_ms;}
                if(pid==117)last117=(uint32_t)elapsed_ms;
                if(pid==118)last118=(uint32_t)elapsed_ms;
            }
        }
        credit+=60;
        while(credit>=100){wire(128,190,9000);credit-=100;}
        if(elapsed_ms%1000==0){wire(128,84,112);wire(128,183,304);wire(128,245,raw_odometer());}
        tbx_j1708_auto_env_t e={.enabled=true,.persisted=true,.interface_ready=true,.topology_attested=true,.local_mid=188,.timeout_ms=450,.extended=true};
        for(unsigned i=0;i<17;i++){e.targets[i]=tbx_j1708_policy()[i].mid;e.periods[i]=tbx_j1708_production_period(tbx_j1708_policy()[i].pid,false);}
        tbx_j1708_auto_tick(&s.automatic,&s.active,&e,false,(uint64_t)host_now_us);tbx_j1708_active_tick(&s.active,(uint64_t)host_now_us);
        if(elapsed_ms==100000 || elapsed_ms==6500000)
            assert(tb_capture_fuel_boundary(true,elapsed_ms==100000?"drive-trip-A":"drive-trip-B",utc_origin+elapsed_ms,0));
        if(elapsed_ms==5000000 || elapsed_ms==11500000){
            assert(tb_capture_fuel_boundary(false,elapsed_ms==5000000?"drive-trip-A":"drive-trip-B",utc_origin+elapsed_ms,0));
            assert(tbx_fuel_operation_litres(&s.fuel_account)>20 && tbx_fuel_operation_litres(&s.fuel_account)<26);
        }
        if(elapsed_ms%10000==0){assert(persist_trip()==ESP_OK);record(0);}
        if(elapsed_ms==3000000 || elapsed_ms==6000000 || elapsed_ms==9000000)restart(boot_count==0?1:boot_count==1?6:3);
        if(elapsed_ms>boot_at_ms+60000){
            assert(s.active.production_mode && s.requests.armed && !s.requests.safety_stopped);
            assert(elapsed_ms-last118<=11000 && elapsed_ms-last250<=11000);
            assert(elapsed_ms-last117<=21000); /* One intentionally missing reply never pauses other PIDs. */
        }
    }
    save_totals();assert(polling_boots==4);for(unsigned i=0;i<4;i++)assert(first250[i]>0 && first250[i]<45000);
    assert(s.distance_trip>299.5 && s.distance_trip<300.3 && s.fuel_trip>59 && s.fuel_trip<61);
    assert(prior_ecu>10 && prior_ecu<61 && s.fuel_account.counter_l>900000); /* Never lifetime ECU fuel as trip fuel. */
    unsigned critical[3]={6,7,10};for(unsigned i=0;i<3;i++)assert(sent_total[critical[i]]>1100 && answered_total[critical[i]]>1000);
    assert(timeouts_total[6]>0 && !timeouts_total[7] && !timeouts_total[10]);
    printf("Drive300: virtual_seconds=12000 nominal_km=300 measured_trip_km=%.8f canonical_l=%.8f confirmed_paired_l=%.8f core_reboots=%u resets=POWERON/TASK_WDT/OTA polling_boots=%u records=%u local_mid=188 PID117/118/250_sent=%u/%u/%u zero_global_request_stops PASS\n",
        s.distance_trip,s.fuel_trip,prior_ecu,boot_count,polling_boots,record_count,sent_total[6],sent_total[7],sent_total[10]);
    fclose(drive_output);host_submit_hook=NULL;return 0;
}
