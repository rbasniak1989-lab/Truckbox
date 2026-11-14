#include "core_internal.h"
#include "protocols.h"
#include "telemetry.h"
#include "diagnostics.h"
#include "ecu_counters.h"
#include "telemetry_profile.h"
#include "tbx_j1587_bridge.h"
#include "tbx_j1708_active.h"
#include "tbx_j1708_auto.h"
#include "tbx_transmission.h"
#include "tbx_j1708_policy.h"
#include "tbx_trip.h"
#include "tbx_fuel.h"
#include "tbx_fuel_account.h"
#include "trip_checkpoint.h"
#include "accounting_json.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/twai.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

static const char *TAG = "tb_capture";
static struct {
    SemaphoreHandle_t lock, trip_persist_lock;
    nvs_handle_t nvs;
    bool nvs_open, can_ready, j1708_ready;
    char can_error[64], j1708_error[64];
    uint32_t can_frames, can_rtr, can_errors, j1708_frames, j1708_checksum_errors, j1708_oversize, uart_overruns;
    uint64_t speed_at, rpm_at, fuel_at, total_at;
    uint64_t speed_can_at, rpm_can_at, fuel_can_at, total_can_at, distance_counter_at;
    bool distance_counter_valid;
    uint8_t distance_counter_sa;
    uint32_t distance_counter_raw;
    tbx_j1708_request_scheduler_t requests;
    tbx_j1708_active_t active;
    tbx_j1708_auto_t automatic;
    tbx_transmission_t transmission;
    bool operator_hold,disable_pending;
    uint64_t context_at,transmission_snapshot_at;
    uint32_t transmission_last_flags,transmission_last_launches;
    uint8_t observed_mid[32];
    uint8_t trial_reply[21],trial_reply_size;
    uint32_t frames_last_minute;
    bool ignition_known,ignition_on;uint64_t ignition_at;
    volatile bool emergency_stop;
    tbx_trip_t trip;
    tbx_fuel_account_t fuel_account;
    uint32_t checkpoint_generation;
    double speed, rpm, fuel_rate, distance_total;
    double distance_trip, fuel_trip;
    bool distance_trip_valid, fuel_trip_valid;
    QueueHandle_t uart_events, request_events;
    volatile uint32_t rx_edge_us, rx_edges;
    bool edge_ready, edge_enabled, rx_partial;
    volatile bool rx_task_busy;
    uint32_t request_event_dropped, uart_start_faults;
    uint64_t last_tx_uart_us;
    bool speed_j1587, rpm_j1587, fuel_j1587, total_j1587;
    uint8_t fuel_address;
    tbx_fuel_t fuel_model;
    uint64_t fuel_model_at;
    bool fuel_derived;
    char j1708_last[TB_RECORD_MAX];
    uint64_t j1708_at;
} s;

static void request_abort_tx(void *unused)
{
    (void)unused;
    /* Program the GPIO latch first, then detach the UART matrix. With DI
     * fixed LOW this releases the dominant driver even during a byte. */
    gpio_set_level(GPIO_NUM_21, 0);
    esp_rom_gpio_connect_out_signal(GPIO_NUM_21, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
    if (s.j1708_ready) (void)uart_set_rx_full_threshold(UART_NUM_1, 64);
}

/* IRAM, no allocation and no UART access: observe actual falling edges even
 * when a character is incomplete and the UART ring buffer is still empty.
 * 32-bit subtraction below handles esp_timer low-word wrap conservatively. */
static void IRAM_ATTR request_rx_edge(void *unused)
{
    (void)unused;
    s.rx_edge_us=(uint32_t)esp_timer_get_time();
    ++s.rx_edges;
}

static bool request_bus_idle(uint32_t now_us)
{
    size_t rx_buffered=0;
    return s.edge_ready && !s.rx_partial && !s.rx_task_busy &&
        (uint32_t)(now_us-s.rx_edge_us)>=s.active.idle_guard_us &&
        gpio_get_level(GPIO_NUM_20)!=0 &&
        uart_ll_get_rxfifo_len(UART_LL_GET_HW(UART_NUM_1))==0 &&
        uxQueueMessagesWaiting(s.uart_events)==0 &&
        uart_get_buffered_data_len(UART_NUM_1,&rx_buffered)==ESP_OK && rx_buffered==0;
}

static tbx_j1708_tx_result_t request_begin_tx(const uint8_t *bytes, size_t length, void *unused)
{
    (void)unused;
    if(s.emergency_stop || !s.j1708_ready || !bytes || (length!=5 && (length!=6 || bytes[1]!=255 || bytes[2]!=128)))
        return TBX_J1708_TX_FAULT;
    if(!request_bus_idle((uint32_t)esp_timer_get_time()) ||
       uart_wait_tx_done(UART_NUM_1,0)!=ESP_OK) return TBX_J1708_TX_DEFERRED;
    const uint32_t edges=s.rx_edges;
    esp_err_t err=uart_set_rx_full_threshold(UART_NUM_1,1);
    if(err==ESP_OK)err=uart_set_line_inverse(UART_NUM_1,UART_SIGNAL_TXD_INV);
    if(err==ESP_OK)err=uart_set_pin(UART_NUM_1,21,20,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    if(err!=ESP_OK){++s.uart_start_faults;request_abort_tx(NULL);return TBX_J1708_TX_FAULT;}
    /* Routing/configuration can take time. Recheck BOTH hardware and software
     * RX after preparation; never discard RX to make room for a request. */
    if(s.emergency_stop || s.rx_edges!=edges ||
       !request_bus_idle((uint32_t)esp_timer_get_time())){
        request_abort_tx(NULL);
        return s.emergency_stop?TBX_J1708_TX_FAULT:TBX_J1708_TX_DEFERRED;
    }
    s.last_tx_uart_us=(uint64_t)esp_timer_get_time();
    if(uart_tx_chars(UART_NUM_1,(const char *)bytes,length)==(int)length)
        return TBX_J1708_TX_STARTED;
    ++s.uart_start_faults;
    request_abort_tx(NULL);
    return TBX_J1708_TX_FAULT;
}

typedef struct {
    uint64_t uptime_us, tx_start_us;
    uint32_t edges;
    uint16_t pid;
    uint8_t local_mid,target_mid,frame[6],frame_size,response[21],response_size;
    uint64_t field_started_us;
    union {tbx_j1708_field_pid_t field_pid;tbx_j1708_field_health_t field_health;tbx_rx_decision_t guard_decision;};
    bool field,field_result,guard;
    char event[24],detail[112];
    bool work;
} request_event_t;

/* Persist each request and its value result; aggregate routine echo/candidate
 * events in HEALTH counters instead of writing redundant extra records.
 * Never hide TIMEOUT, TX_ERROR, LINK_ERROR, AUTO_DISARMED or STOPPED. */
static bool request_event_should_persist(const char *name, bool work_mode)
{
    if (!name || !work_mode) return true;
    return strcmp(name,"TX_ECHO") != 0 &&
           strcmp(name,"RESPONSE_CANDIDATE") != 0;
}

static void request_event(const char *event, uint16_t pid, const char *detail, void *unused)
{
    (void)unused;
    if(s.active.production_mode && (!strcmp(event,"REQUEST") || !strcmp(event,"TX_ECHO") || !strcmp(event,"RESPONSE_CANDIDATE")))return;
    if (!request_event_should_persist(event,s.active.work_mode)) return;
    /* RX/TX callbacks only queue fixed-size evidence. JSON, storage and logging
     * run outside the RX/TX lock and never delay the byte echo deadline. */
    if(s.active.work_mode && (!strcmp(event,"TX_ECHO") || !strcmp(event,"RESPONSE_CANDIDATE")))return;
    request_event_t item={.work=s.active.work_mode,.uptime_us=(uint64_t)esp_timer_get_time(),
        .tx_start_us=s.last_tx_uart_us,.edges=s.rx_edges,.pid=pid,
        .field=s.active.field.started_us!=0 && !s.active.physical_validated,.field_started_us=s.active.field.started_us,
        .field_result=!strcmp(event,"FIELD_PID_RESULT"),.guard=!strcmp(event,"GUARD_DECISION")};
    snprintf(item.event,sizeof(item.event),"%s",event);
    snprintf(item.detail,sizeof(item.detail),"%s",detail?detail:"");
    if(s.active.configured){
        item.local_mid=s.active.config.local_mid;
        for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)
            if(s.requests.pids[i].pid==pid){
                item.target_mid=s.active.config.target_mid[i];
                if(i<TBX_J1708_REQUEST_COUNT)item.field_pid=s.active.field.pids[i];
            }
        if(item.field && !pid){
            const tbx_j1708_field_t *f=&s.active.field;
            item.field_health=(tbx_j1708_field_health_t){.before_frames=f->before_frames,.after_frames=f->after_frames,
                .before_errors=f->before_errors,.after_errors=f->after_errors,.before_duration_us=f->before_duration_us,.after_duration_us=f->after_duration_us};
        }
        if(item.guard){item.guard_decision=s.active.rx_guard.decision;item.pid=item.guard_decision.pid;item.target_mid=item.guard_decision.target_mid;}
        memcpy(item.frame,s.active.tx_frame,s.active.tx_length);item.frame_size=s.active.tx_length;
    }
    if(!s.request_events || xQueueSend(s.request_events,&item,0)!=pdTRUE){
        __atomic_fetch_add(&s.request_event_dropped,1,__ATOMIC_RELAXED);
        if(item.field)__atomic_store_n(&s.active.field.audit_lost,true,__ATOMIC_RELAXED);
    }
}

static bool guard_values(cJSON *o,const char *name,const uint64_t *values,unsigned count)
{
    cJSON *a=cJSON_CreateArray();if(!a)return false;
    for(unsigned i=0;i<count;i++)if(!tb_json_add_array(a,cJSON_CreateNumber((double)values[i]))){cJSON_Delete(a);return false;}
    return tb_json_add_object(o,name,a);
}
static bool guard_record(cJSON *o,const tbx_rx_decision_t *d)
{
    const tbx_rx_counts_t *w=&d->window.count,*r=&d->reference.count,*t=&d->totals;
    const uint64_t window[]={d->window.start_us/1000,d->window.end_us/1000,w->valid,w->checksum,w->framing,w->overruns,w->uart_frame,w->read_errors};
    const uint64_t ref[]={d->reference.start_us/1000,d->reference.end_us/1000,r->valid,r->checksum,r->framing};
    const uint64_t totals[]={t->valid,t->checksum,t->framing,t->overruns,t->uart_frame,t->read_errors,d->sent,d->echoes,d->candidates,d->timeouts,d->collisions,d->echo_timeouts,d->tx_errors};
    const uint64_t intervals[]={d->current_lower_ppm,d->reference_upper_ppm};
    const uint64_t policy[]={TBX_RX_WINDOW_US/1000,TBX_RX_PERSISTENCE,99,TBX_RX_REFERENCE_MAX_US/1000,TBX_RX_PERSISTENCE,d->timeout_ms,d->spacing_ms,TBX_J1708_ACTIVE_ECHO_TIMEOUT_US/1000};
    const uint64_t modes[]={d->before_flags,d->after_flags};
    bool ok=cJSON_AddNumberToObject(o,"guard_v",1) &&
        cJSON_AddStringToObject(o,"source","LIVE_PROTECTION_DECISION") &&
        cJSON_AddStringToObject(o,"reason",tbx_j1708_guard_cause_name(d->cause)) &&
        cJSON_AddBoolToObject(o,"stopped",d->stopped) &&
        cJSON_AddNumberToObject(o,"predicates",d->predicates) && cJSON_AddNumberToObject(o,"streak",d->streak) && cJSON_AddNumberToObject(o,"fails",d->request_failures) &&
        cJSON_AddBoolToObject(o,"candidate_only",true) &&
        cJSON_AddBoolToObject(o,"crc_hit",(d->predicates&TBX_GUARD_CRC_ELEVATED)!=0) &&
        cJSON_AddBoolToObject(o,"frame_hit",(d->predicates&TBX_GUARD_FRAME_ELEVATED)!=0) &&
        cJSON_AddBoolToObject(o,"persist_hit",(d->predicates&TBX_GUARD_PERSISTENT)!=0);
    ok=ok && guard_values(o,"w",window,8) && guard_values(o,"ref",ref,5) && guard_values(o,"totals",totals,13) &&
        ((d->predicates&TBX_GUARD_REFERENCE_READY)?guard_values(o,"ci_ppm",intervals,2):cJSON_AddNullToObject(o,"ci_ppm")!=NULL) && guard_values(o,"policy",policy,8) && guard_values(o,"mode",modes,2);
    if(d->pid)ok=ok && cJSON_AddNumberToObject(o,"pid",d->pid) && cJSON_AddNumberToObject(o,"target_mid",d->target_mid);
    return ok;
}
static void request_store_guard(const request_event_t *item)
{
    cJSON *o=cJSON_CreateObject();const tbx_rx_decision_t *d=&item->guard_decision;
    char id[64];snprintf(id,sizeof(id),"%" PRIu32 ":%" PRIu64,tb_boot_id(),d->at_us);
    bool ok=o && cJSON_AddStringToObject(o,"sample_kind","J1708_REQUEST_EVENT") &&
        cJSON_AddStringToObject(o,"event","GUARD_DECISION") && cJSON_AddNumberToObject(o,"boot_id",tb_boot_id()) &&
        cJSON_AddNumberToObject(o,"uptime_ms",(double)(d->at_us/1000)) && cJSON_AddStringToObject(o,"guard_id",id) && cJSON_AddNumberToObject(o,"mid",item->local_mid) && guard_record(o,d);
    char json[TB_RECORD_MAX];
    ok=ok && cJSON_PrintPreallocated(o,json,sizeof(json),false) && tb_store_submit(json)==ESP_OK;
    cJSON_Delete(o);
    if(!ok){__atomic_fetch_add(&s.request_event_dropped,1,__ATOMIC_RELAXED);if(item->field)__atomic_store_n(&s.active.field.audit_lost,true,__ATOMIC_RELAXED);}
}

static void request_store_event(const request_event_t *item)
{
    if(item->guard){request_store_guard(item);return;}
    cJSON *row = cJSON_CreateObject();
    if (!row){__atomic_fetch_add(&s.request_event_dropped,1,__ATOMIC_RELAXED);if(item->field)__atomic_store_n(&s.active.field.audit_lost,true,__ATOMIC_RELAXED);return;}
    cJSON_AddStringToObject(row, "sample_kind", "J1708_REQUEST_EVENT");
    cJSON_AddNumberToObject(row, "schema_version", 2);
    cJSON_AddNumberToObject(row, "boot_id", tb_boot_id());
    cJSON_AddNumberToObject(row, "uptime_ms", item->uptime_us / 1000);
    bool tx_record=!strcmp(item->event,"TIMEOUT") || !strcmp(item->event,"MID_CONFLICT_FRAME") || !strcmp(item->event,"REQUEST") || !strcmp(item->event,"TX_ECHO") || !strcmp(item->event,"RESPONSE_CANDIDATE") || item->response_size;
    if(tx_record)cJSON_AddNumberToObject(row,"tx_start_us",(double)item->tx_start_us);
    if(!item->work && !item->field)cJSON_AddNumberToObject(row,"rx_edges",item->edges);
    cJSON_AddBoolToObject(row, "time_valid", false);
    cJSON_AddStringToObject(row, "time_source", "boot_monotonic");
    cJSON_AddStringToObject(row, "event", item->event);
    if(!item->field && (!item->work || strcmp(item->event,"REQUEST")))cJSON_AddStringToObject(row,"detail",item->detail);
    if(item->field){
        char session[64];snprintf(session,sizeof(session),"%" PRIu32 ":%" PRIu64,tb_boot_id(),item->field_started_us);
        cJSON_AddStringToObject(row,"field_session",session);
        cJSON_AddStringToObject(row,"classification","OPERATIONAL_INDIRECT");
        if(!tx_record)cJSON_AddStringToObject(row,"detail",item->detail);
    }
    if(item->field && !item->pid){
        const tbx_j1708_field_health_t *v=&item->field_health;
        if(v->before_duration_us){cJSON_AddStringToObject(row,"baseline_scope","HISTORICAL_VALIDATION");cJSON_AddStringToObject(row,"baseline_error_metric","ALL_INVALID_FRAMES");}
        if(v->before_duration_us){cJSON_AddNumberToObject(row,"before_frames",v->before_frames);cJSON_AddNumberToObject(row,"before_checksum_errors",v->before_errors);cJSON_AddNumberToObject(row,"before_duration_ms",v->before_duration_us/1000.0);}
        if(v->after_duration_us){cJSON_AddNumberToObject(row,"after_frames",v->after_frames);cJSON_AddNumberToObject(row,"after_checksum_errors",v->after_errors);cJSON_AddNumberToObject(row,"after_duration_ms",v->after_duration_us/1000.0);}
    }
    if(item->field_result){
        const tbx_j1708_field_pid_t *v=&item->field_pid;
        cJSON_AddNumberToObject(row,"before",v->before);cJSON_AddNumberToObject(row,"after",v->after);
        cJSON_AddNumberToObject(row,"controls",v->controls);cJSON_AddNumberToObject(row,"control_hits",v->control_hits);
        cJSON_AddNumberToObject(row,"sent",v->sent);cJSON_AddNumberToObject(row,"echoes",v->echoes);cJSON_AddNumberToObject(row,"replies",v->replies);
        cJSON_AddNumberToObject(row,"latency_min_us",v->latency_min_us);cJSON_AddNumberToObject(row,"latency_max_us",v->latency_max_us);
    }
    if(item->response_size){
        char response[43];for(unsigned i=0;i<item->response_size;i++)snprintf(response+2*i,3,"%02X",item->response[i]);
        cJSON_AddStringToObject(row,"response_frame",response);cJSON_AddBoolToObject(row,"checksum_valid",true);
    }
    if(!item->field || item->pid)cJSON_AddNumberToObject(row,"pid",item->pid);
    cJSON_AddNumberToObject(row,"local_mid",item->local_mid);
    if(!item->field || item->pid)cJSON_AddNumberToObject(row,"target_mid",item->target_mid);
    char raw[13]={0};
    for(unsigned i=0;i<item->frame_size;i++)snprintf(raw+2*i,3,"%02X",item->frame[i]);
    if(tx_record)cJSON_AddStringToObject(row,"tx_frame",raw);
    cJSON_AddStringToObject(row, "validation", "EM_VALIDACAO");
    bool complete=true;
    if(item->field){
        static const char *const required[]={"sample_kind","schema_version","boot_id","uptime_ms","time_valid","time_source","event","local_mid","validation","field_session","classification"};
        for(unsigned i=0;i<sizeof(required)/sizeof(*required);i++)if(!cJSON_GetObjectItemCaseSensitive(row,required[i]))complete=false;
        if(item->pid && (!cJSON_GetObjectItemCaseSensitive(row,"pid") || !cJSON_GetObjectItemCaseSensitive(row,"target_mid")))complete=false;
        if(!item->pid){
            if(item->field_health.before_duration_us && (!cJSON_GetObjectItemCaseSensitive(row,"before_frames") || !cJSON_GetObjectItemCaseSensitive(row,"before_checksum_errors") || !cJSON_GetObjectItemCaseSensitive(row,"before_duration_ms")))complete=false;
            if(item->field_health.after_duration_us && (!cJSON_GetObjectItemCaseSensitive(row,"after_frames") || !cJSON_GetObjectItemCaseSensitive(row,"after_checksum_errors") || !cJSON_GetObjectItemCaseSensitive(row,"after_duration_ms")))complete=false;
        }
        if(item->field_result){
            static const char *const measured[]={"before","after","controls","control_hits","sent","echoes","replies","latency_min_us","latency_max_us"};
            for(unsigned i=0;i<sizeof(measured)/sizeof(*measured);i++)if(!cJSON_GetObjectItemCaseSensitive(row,measured[i]))complete=false;
        }
        if(!tx_record && !cJSON_GetObjectItemCaseSensitive(row,"detail"))complete=false;
        if(tx_record && (!cJSON_GetObjectItemCaseSensitive(row,"tx_frame") || !cJSON_GetObjectItemCaseSensitive(row,"tx_start_us")))complete=false;
        if(item->response_size && (!cJSON_GetObjectItemCaseSensitive(row,"response_frame") || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(row,"checksum_valid"))))complete=false;
    }
    char *json = complete?cJSON_PrintUnformatted(row):NULL;
    if (!json || strlen(json)>=TB_RECORD_MAX || tb_store_submit(json)!=ESP_OK){
        __atomic_fetch_add(&s.request_event_dropped,1,__ATOMIC_RELAXED);
        if(item->field)__atomic_store_n(&s.active.field.audit_lost,true,__ATOMIC_RELAXED);
    }
    cJSON_free(json); cJSON_Delete(row);
}

static void request_task(void *unused)
{
    (void)unused;
    for (;;) {
        bool ota_busy = tb_web_ota_busy();
        xSemaphoreTake(s.lock, portMAX_DELAY);
        uint64_t now = esp_timer_get_time();
        const tb_config_t *cfg=tb_config();
        if(now-s.context_at>=UINT64_C(250000)){
            s.context_at=now;
            tb_telemetry_transmission_can(&s.transmission);
            if(s.speed_at)tbx_transmission_observe(&s.transmission,TBX_TR_SPEED,s.speed,s.speed_at*1000);
            if(s.rpm_at)tbx_transmission_observe(&s.transmission,TBX_TR_RPM,s.rpm,s.rpm_at*1000);
            tbx_transmission_tick(&s.transmission,now);
        }
        bool enabled=tb_config_j1708_auto_enabled() && cfg->capture_enabled && cfg->j1708_enabled;
        if(enabled && s.edge_ready && !s.edge_enabled){s.rx_edge_us=(uint32_t)now;s.edge_enabled=gpio_intr_enable(GPIO_NUM_20)==ESP_OK;}
        tbx_j1708_auto_env_t env={.enabled=enabled,.persisted=tb_config_j1708_auto_persisted(),
            .interface_ready=s.j1708_ready && s.edge_ready && s.edge_enabled,.local_mid=cfg->j1708_local_mid,
            .extended=cfg->j1708_expansion_enabled,.topology_attested=cfg->j1708_topology_attested,.ota_busy=ota_busy,.operator_hold=s.operator_hold,.timeout_ms=cfg->j1708_timeout_ms};
        env.local_conflict=env.local_mid && (s.observed_mid[env.local_mid>>3]&(1u<<(env.local_mid&7u)));
        memcpy(env.targets,cfg->j1708_targets,sizeof(env.targets));
        memcpy(env.periods,s.transmission.launch?cfg->j1708_launch_intervals_ms:cfg->j1708_auto_intervals_ms,sizeof(env.periods));
        tbx_j1708_auto_tick(&s.automatic,&s.active,&env,s.transmission.launch,now);
        if(enabled && env.local_conflict && !s.operator_hold){s.operator_hold=true;s.disable_pending=true;}
        s.emergency_stop=s.operator_hold || !s.active.scheduler->armed || ota_busy;
        if (ota_busy && s.requests.armed) tbx_j1708_active_stop(&s.active, "OTA_BUSY");
        if(tbx_j1708_field_running(&s.active.field) &&
           (!s.speed_at || now/1000<s.speed_at || now/1000-s.speed_at>5000 || s.speed>0.1))
            tbx_j1708_field_fail(&s.active,"FIELD_REQUIRES_FRESH_STATIONARY_SPEED");
        if(s.requests.armed && !request_bus_idle((uint32_t)now))
            tbx_j1708_active_note_rx(&s.active,NULL,0,now);
        /* One scheduler: capture jobs only advance while production is armed. */
        bool fresh_speed=s.speed_at && now/1000>=s.speed_at && now/1000-s.speed_at<=5000;
        bool ecu_awake=s.active.rx_guard.seen_valid && now>=s.active.rx_guard.last_valid_us && now-s.active.rx_guard.last_valid_us<=5000000;
        bool ignition=(s.ignition_known && now/1000>=s.ignition_at && now/1000-s.ignition_at<=5000 && s.ignition_on) ||
            (s.rpm_at && now/1000>=s.rpm_at && now/1000-s.rpm_at<=3000 && s.rpm>200);
        tb_ecu_counters_tick(&s.requests,fresh_speed,s.speed,ecu_awake && ignition,now/1000,cfg->j1708_stop_dwell_s,cfg->j1708_parked_capture_s);
        tbx_j1708_active_tick(&s.active, now);
        if(!s.requests.armed && !enabled && s.edge_enabled){gpio_intr_disable(GPIO_NUM_20);s.edge_enabled=false;}
        bool persist_disable=s.disable_pending;s.disable_pending=false;
        xSemaphoreGive(s.lock);
        if(persist_disable && tb_config_set_j1708_auto(false)!=ESP_OK)ESP_LOGE(TAG,"MID conflict: TX held; persistent disable failed");
        request_event_t item;
        for(unsigned i=0;i<2 && xQueueReceive(s.request_events,&item,0)==pdTRUE;i++)
            request_store_event(&item);
        vTaskDelay(1);
    }
}

typedef struct { uint32_t version; double distance, fuel; bool distance_valid, fuel_valid; } trip_blob_v1_t;
typedef struct { uint32_t version; double distance, fuel; bool distance_valid, fuel_valid;
    bool counter_valid; uint8_t counter_sa; uint32_t counter_raw; } trip_blob_t;

static void persist_trip(void)
{
    if (!s.nvs_open || !s.trip_persist_lock) return;
    xSemaphoreTake(s.trip_persist_lock, portMAX_DELAY);
    trip_blob_t previous={0};size_t previous_size=sizeof(previous);
    uint32_t before_crc=nvs_get_blob(s.nvs,"trip",&previous,&previous_size)==ESP_OK?tb_store_crc32(&previous,previous_size):0;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    /* Keep the legacy scalar key for explicit rollback to 0.2.11. */
    if(!tb_config()->j1708_enabled || !s.fuel_j1587){
        s.fuel_account.observed_l=s.fuel_trip;s.fuel_account.valid=s.fuel_trip_valid;
    }
    tb_trip_checkpoint_t checkpoint;
    tb_trip_checkpoint_make(&checkpoint,++s.checkpoint_generation,&s.trip,&s.fuel_account);
    trip_blob_t blob = { .version = 2, .distance=s.distance_trip, .fuel=s.fuel_trip,
        .distance_valid=s.distance_trip_valid,.fuel_valid=s.fuel_trip_valid,
        .counter_valid=s.distance_counter_valid,.counter_sa=s.distance_counter_sa,.counter_raw=s.distance_counter_raw };
    checkpoint.legacy_before_crc=before_crc;checkpoint.legacy_after_crc=tb_store_crc32(&blob,sizeof(blob));
    checkpoint.crc=tb_store_crc32(&checkpoint,offsetof(tb_trip_checkpoint_t,crc));
    xSemaphoreGive(s.lock);
    esp_err_t err=nvs_set_blob(s.nvs,(checkpoint.generation&1)?"trip3a":"trip3b",&checkpoint,sizeof(checkpoint));
    if(err==ESP_OK)err=nvs_commit(s.nvs);
    if(err==ESP_OK)err=nvs_set_blob(s.nvs,"trip",&blob,sizeof(blob));
    if(err==ESP_OK)err=nvs_commit(s.nvs);
    if(err!=ESP_OK)ESP_LOGW(TAG,"Trip checkpoint: %s",esp_err_to_name(err));
    xSemaphoreGive(s.trip_persist_lock);
}

static void mirror_trip_locked(void)
{
    s.distance_trip = s.trip.total_km; s.distance_trip_valid = s.trip.has_distance;
    s.distance_counter_valid = s.trip.counter_valid; s.distance_counter_sa = s.trip.counter_sa;
    s.distance_counter_raw = s.trip.counter_raw; s.distance_counter_at = s.trip.counter_at_ms;
}

#ifndef CONFIG_TB_J1708_ONLY
static uint32_t pgn_of(uint32_t id)
{
    uint32_t pgn = (id >> 8) & 0x3ffff;
    if (((id >> 16) & 0xff) < 240) pgn &= 0x3ff00;
    return pgn;
}

static void update_signals(const twai_message_t *frame)
{
    if (!frame->extd || frame->rtr) return;
    uint32_t pgn = pgn_of(frame->identifier);
    uint64_t now = esp_timer_get_time() / 1000;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    if (pgn == 61444 && frame->data_length_code >= 5) {
        uint16_t raw = frame->data[3] | ((uint16_t)frame->data[4] << 8);
        if (raw <= 0xfaff) { s.rpm = raw * 0.125; s.rpm_at = now; s.rpm_j1587 = false; s.rpm_can_at = now; }
        double rate, delta;
        if (tbx_fuel_ingest(&s.fuel_model, frame->identifier & 255, raw, frame->data[2], now, &rate, &delta)) {
            s.fuel_trip += delta; s.fuel_trip_valid = true;
            s.fuel_rate = rate; s.fuel_at = s.fuel_model_at = now;
            s.fuel_j1587 = false; s.fuel_derived = true; s.fuel_can_at = now;
            s.fuel_address = frame->identifier & 255;
        }
    } else if (pgn == 65265 && frame->data_length_code >= 3) {
        uint16_t raw = frame->data[1] | ((uint16_t)frame->data[2] << 8);
        if (raw <= 0xfaff) {
            double speed = raw / 256.0;
            if (speed > 0.1 && s.requests.armed && !s.active.work_mode)
                tbx_j1708_active_stop(&s.active, "VEHICLE_MOVEMENT_J1939");
            tbx_trip_result_t result = tbx_trip_speed(&s.trip, TBX_TRIP_J1939, frame->identifier & 255, speed, now);
            mirror_trip_locked();
            if (result != TBX_TRIP_SOURCE_IGNORED && result != TBX_TRIP_INVALID) {
            s.speed = speed; s.speed_at = now; s.speed_j1587 = false; s.speed_can_at = now;
            }
        }
    } else if (pgn == 65266 && frame->data_length_code >= 2 &&
        (!s.fuel_model_at || now - s.fuel_model_at > 5000)) {
        uint16_t raw = frame->data[0] | ((uint16_t)frame->data[1] << 8);
        if (raw <= 0xfaff) {
            double rate = raw * 0.05;
            if (s.fuel_at && !s.fuel_derived && !s.fuel_j1587 && s.fuel_address == (frame->identifier & 255) && now > s.fuel_at && now - s.fuel_at <= 3000) {
                s.fuel_trip += (s.fuel_rate + rate) * 0.5 * (now - s.fuel_at) / 3600000.0;
            }
            s.fuel_trip_valid = true;
            s.fuel_rate = rate; s.fuel_at = now; s.fuel_j1587 = false; s.fuel_can_at = now; s.fuel_address = frame->identifier & 255; s.fuel_derived = false;
        }
    } else if (pgn == 65217 && frame->data_length_code >= 4) {
        uint32_t raw = frame->data[0] | ((uint32_t)frame->data[1] << 8) |
            ((uint32_t)frame->data[2] << 16) | ((uint32_t)frame->data[3] << 24);
        uint8_t sa = frame->identifier & 255;
        if (raw <= UINT32_C(0xfaffffff)) {
            tbx_trip_result_t result = tbx_trip_counter(&s.trip, sa, raw, now);
            mirror_trip_locked();
            if (result != TBX_TRIP_SOURCE_IGNORED && result != TBX_TRIP_INVALID) {
            s.distance_total = raw * 0.005; s.total_at = now; s.total_j1587 = false; s.total_can_at = now;
            }
        }
    } else if (pgn == 65248 && frame->data_length_code >= 8 &&
        (!s.distance_counter_at || now - s.distance_counter_at > 30000)) {
        uint32_t raw = frame->data[4] | ((uint32_t)frame->data[5] << 8) |
            ((uint32_t)frame->data[6] << 16) | ((uint32_t)frame->data[7] << 24);
        if (raw <= UINT32_C(0xfaffffff)) { s.distance_total = raw * 0.125; s.total_at = now; s.total_j1587 = false; s.total_can_at = now; }
    }
    xSemaphoreGive(s.lock);
}

static void can_task(void *unused)
{
    twai_message_t frame;
    for (;;) {
        esp_err_t err = twai_receive(&frame, pdMS_TO_TICKS(1000));
        if (err == ESP_ERR_TIMEOUT) continue;
        if (err != ESP_OK) { ++s.can_errors; vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        ++s.can_frames;
        uint64_t captured_us = esp_timer_get_time();
        /* Always decode PGNs, even when per-frame RAW persistence is off. */
        tb_telemetry_can(frame.identifier, frame.extd, frame.rtr, frame.data_length_code, frame.data, captured_us);
        if (frame.rtr) ++s.can_rtr;
        /* Raw frames go to the bounded legacy TCP stream. Cloud samples are 1 Hz snapshots. */
        if (!frame.rtr) tb_live_can(frame.identifier, frame.extd, frame.data_length_code, frame.data);
        update_signals(&frame);
    }
}

#endif

static void tb_j1708_store_matched_response(const tbx_j1587_observation_t *value, uint64_t now)
{
            /* Production coalesces successful results into timestamped sensor
             * snapshots/HEALTH, not two flash records for each frequent query. */
            if(s.active.production_mode)return;
            /* Persist each matching MID/PID observation — not just summary
             * counters. This is a response CANDIDATE, never causal proof. */
            cJSON *row=cJSON_CreateObject();
            if(row){
                cJSON_AddStringToObject(row,"sample_kind","J1708_REQUEST_EVENT");
                cJSON_AddStringToObject(row,"event","RESPONSE_VALUE");
                cJSON_AddNumberToObject(row,"schema_version",2);
                cJSON_AddNumberToObject(row,"boot_id",tb_boot_id());
                cJSON_AddNumberToObject(row,"uptime_ms",(double)now);
                cJSON_AddBoolToObject(row,"time_valid",false);
                cJSON_AddStringToObject(row,"time_source","boot_monotonic");
                cJSON_AddStringToObject(row,"classification","RESPONSE_CANDIDATE");
                char tx[13]={0};for(unsigned j=0;j<s.active.tx_length;j++)snprintf(tx+2*j,3,"%02X",s.active.tx_frame[j]);
                cJSON_AddStringToObject(row,"tx_frame",tx);
                cJSON_AddNumberToObject(row,"pid",value->pid);
                cJSON_AddNumberToObject(row,"mid",value->mid);
                cJSON_AddNumberToObject(row,"local_mid",s.active.config.local_mid);
                cJSON_AddStringToObject(row,"quality",tbx_j1587_observation_quality_name(value->quality));

                if(value->unit)cJSON_AddStringToObject(row,"unit",value->unit);
                if(value->has_numeric && isfinite(value->value))
                    cJSON_AddNumberToObject(row,"value",value->value);
                char rawhex[43]={0};
                const size_t rawlen=value->raw_len<=21u?value->raw_len:21u;
                for(size_t k=0;k<rawlen;k++)snprintf(rawhex+2u*k,3u,"%02X",value->raw[k]);
                cJSON_AddStringToObject(row,"parameter_hex",rawhex);
                for(unsigned k=0;k<TBX_J1708_REQUEST_CAPACITY;k++)
                    if(s.requests.pids[k].pid==value->pid){
                        cJSON_AddNumberToObject(row,"target_mid",s.active.config.target_mid[k]);
                        const uint32_t sent_ms=s.requests.pids[k].last_sent_ms;
                        if(now>=sent_ms)cJSON_AddNumberToObject(row,"latency_ms",(double)(now-sent_ms));
                        break;
                    }
                char *json=cJSON_PrintUnformatted(row);
                if(json && strlen(json)<TB_RECORD_MAX)(void)tb_store_submit(json);
                free(json);cJSON_Delete(row);
            }
}

/* Called with s.lock held after checksum and complete J1587 layout validation.
 * J1939 remains primary while fresh; both observations are retained by the
 * separate telemetry worker regardless of dashboard source preference. */
static void update_j1587_signal_locked(const tbx_j1587_observation_t *value, void *unused)
{
    (void)unused;
    uint64_t now = value->uptime_ms;
    if (value->quality == TBX_J1587_OBS_EM_VALIDACAO || value->quality == TBX_J1587_OBS_INDISPONIVEL) {
        tbx_j1708_field_note_parameter(&s.active,value->mid,value->pid,now*1000);
        const bool matched=tbx_j1708_active_note_response(&s.active,value->mid,value->pid,now*1000);
        tb_ecu_counters_observe(value,matched);
        if (matched)
            tb_j1708_store_matched_response(value,now);
    }
    if(value->pid==43 && value->raw_len==1 && (value->mid==128 || value->mid==144)){
        unsigned run=(value->raw[0]>>2)&3;s.ignition_known=run<=1;s.ignition_on=run==1;s.ignition_at=now;
    }
    unsigned tr_signal=value->pid==91?TBX_TR_PEDAL:value->pid==162?TBX_TR_SELECTED:value->pid==163?TBX_TR_GEAR:value->pid==33?TBX_TR_CLUTCH:value->pid==160?TBX_TR_SHAFT:TBX_TR_INPUTS;
    if (value->quality != TBX_J1587_OBS_EM_VALIDACAO || !value->has_numeric || !isfinite(value->value)){
        if(tr_signal<TBX_TR_INPUTS && now*1000>=s.transmission.input[tr_signal].at_us){s.transmission.input[tr_signal].valid=false;s.transmission.input[tr_signal].at_us=now*1000;}
        return;
    }
    if(tr_signal<TBX_TR_INPUTS)tbx_transmission_observe(&s.transmission,tr_signal,value->value,now*1000);
    if (value->pid == 84 && value->value > 0.1 && s.requests.armed && !s.active.work_mode)
        tbx_j1708_active_stop(&s.active, "VEHICLE_MOVEMENT_J1587");
    if (value->pid == 84 && (!s.speed_can_at || now - s.speed_can_at > 5000)) {
        tbx_trip_result_t result = tbx_trip_speed(&s.trip, TBX_TRIP_J1587, value->mid, value->value, now);
        mirror_trip_locked();
        if (result == TBX_TRIP_SOURCE_IGNORED || result == TBX_TRIP_INVALID) return;
        s.speed = value->value; s.speed_at = now; s.speed_j1587 = true;
    } else if (value->pid == 190 && (!s.rpm_can_at || now - s.rpm_can_at > 5000)) {
        s.rpm = value->value; s.rpm_at = now; s.rpm_j1587 = true;
    } else if (value->pid == 183 && (!s.fuel_can_at || now - s.fuel_can_at > 5000)) {
        if(!s.fuel_j1587){s.fuel_account.observed_l=s.fuel_trip;s.fuel_account.valid=s.fuel_trip_valid;s.fuel_account.rate_valid=false;}
        tbx_fuel_account_rate(&s.fuel_account,value->mid,value->value,
            s.speed_at && now>=s.speed_at && now-s.speed_at<=5000?s.speed:NAN,now);
        s.fuel_trip=s.fuel_account.observed_l;s.fuel_trip_valid=s.fuel_account.valid;
        s.fuel_rate=value->value;s.fuel_at=now;s.fuel_j1587=true;s.fuel_address=value->mid;s.fuel_derived=false;
    } else if (value->pid == 245 && (!s.total_can_at || now - s.total_can_at > 30000) &&
        (!tb_config()->j1587_odometer_validated || value->mid==tb_config()->j1587_odometer_mid)) {
        s.distance_total=value->value;s.total_at=now;s.total_j1587=true;
        if(tb_config()->j1587_odometer_validated && value->raw_len==4){
            uint32_t raw=(uint32_t)value->raw[0]|(uint32_t)value->raw[1]<<8|(uint32_t)value->raw[2]<<16|(uint32_t)value->raw[3]<<24;
            tbx_trip_result_t result=tbx_trip_odometer(&s.trip,value->mid,raw,now);mirror_trip_locked();
            if(result==TBX_TRIP_COUNTER_ADDED || result==TBX_TRIP_BASELINE)
                tbx_fuel_account_distance(&s.fuel_account,s.distance_trip);
        }
    } else if(value->pid==250 && tb_config()->j1587_fuel250_validated && value->mid==tb_config()->j1587_fuel250_mid && value->raw_len==4){
        uint32_t raw=(uint32_t)value->raw[0]|(uint32_t)value->raw[1]<<8|(uint32_t)value->raw[2]<<16|(uint32_t)value->raw[3]<<24;
        (void)tbx_fuel_account_counter(&s.fuel_account,value->mid,raw,now);
        s.fuel_trip=s.fuel_account.observed_l;s.fuel_trip_valid=s.fuel_account.valid;

    }
}

static void finish_j1708(uint8_t *frame, size_t size, bool oversize)
{
    if (!size) return;
    uint64_t captured_us = esp_timer_get_time();
    xSemaphoreTake(s.lock, portMAX_DELAY);
    bool echo = !oversize && tbx_j1708_active_is_tx_echo(&s.active, frame, size, captured_us);
    if(!echo){
        tbx_rx_kind_t kind=(oversize || size>21 || size<2 || frame[0]<128)?TBX_RX_FRAMING:
            tb_protocol_j1708_checksum_valid(frame,size)?TBX_RX_VALID:TBX_RX_CHECKSUM;
        tbx_j1708_field_note_frame_kind(&s.active,frame[0],kind,captured_us);
    }
    if (!echo && !oversize && size <= 21 && tb_protocol_j1708_checksum_valid(frame, size) && frame[0] >= 128) {
        s.observed_mid[frame[0] >> 3] |= (uint8_t)(1u << (frame[0] & 7u));
        if (s.active.configured && frame[0] == s.active.config.local_mid) {
            s.active.physical_validated=false;
            if(!s.requests.safety_stopped || s.requests.armed || s.active.tx_active){
                s.active.rx_guard.last_failure=TBX_GUARD_MID_CONFLICT;
                if(s.active.ops.event){
                    char raw[43],detail[112];
                    for(size_t i=0;i<size;i++)snprintf(raw+2*i,3,"%02X",frame[i]);
                    snprintf(detail,sizeof(detail),"rx=%s bytes=%u echo=%u/%u tx_age_us=%llu",raw,(unsigned)size,
                        (unsigned)s.active.echo_position,(unsigned)s.active.tx_length,
                        (unsigned long long)(captured_us>=s.active.tx_started_us?captured_us-s.active.tx_started_us:0));
                    s.active.ops.event("MID_CONFLICT_FRAME",s.active.tx_pid,detail,s.active.context);
                }
                s.requests.safety_stopped=true;tbx_j1708_active_stop(&s.active,"LOCAL_MID_CONFLICT");
            }
        }
    }
    xSemaphoreGive(s.lock);
    if (echo) {
        if (tb_config()->full_decode_enabled) tb_telemetry_j1708_echo(frame, size, captured_us, oversize);
        return;
    }
    /* Decode all passive and solicited J1587 PIDs regardless of storage policy. */
    tb_telemetry_j1708(frame, size, captured_us, oversize);
    if (oversize || size > 21) {
        ++s.j1708_oversize;
        /* Bad PASSIVE frames must not masquerade as TX/echo failure. */
        return;
    }
    if (!tb_protocol_j1708_checksum_valid(frame, size)) {
        ++s.j1708_checksum_errors;
        /* Keep this in the passive checksum counter, not in TX collision
         * protection. Confirmed UART/echo failures still stop immediately. */
        return;
    }
    char record[TB_RECORD_MAX];
    if (tb_protocol_j1708_json(record, sizeof(record), captured_us, frame, size)) {
        ++s.j1708_frames;
        uint64_t now = captured_us / 1000;
        xSemaphoreTake(s.lock, portMAX_DELAY);
        snprintf(s.j1708_last, sizeof(s.j1708_last), "%s", record);
        s.j1708_at = now;
        uint32_t before=s.requests.answered_total;
        (void)tbx_j1587_bridge_decode(frame, size, now, update_j1587_signal_locked, NULL);
        if(s.requests.answered_total>before){
            memcpy(s.trial_reply,frame,size);s.trial_reply_size=(uint8_t)size;
            if(tbx_j1708_field_running(&s.active.field)){
                request_event_t item={.field=true,.field_started_us=s.active.field.started_us,.uptime_us=captured_us,
                    .tx_start_us=s.last_tx_uart_us,.pid=s.active.last_response_pid,.local_mid=s.active.config.local_mid,
                    .target_mid=s.active.last_response_mid,.response_size=(uint8_t)size};
                snprintf(item.event,sizeof(item.event),"RESPONSE_FRAME");memcpy(item.frame,s.active.tx_frame,s.active.tx_length);item.frame_size=s.active.tx_length;memcpy(item.response,frame,size);
                if(!s.request_events || xQueueSend(s.request_events,&item,0)!=pdTRUE){
                    __atomic_fetch_add(&s.request_event_dropped,1,__ATOMIC_RELAXED);__atomic_store_n(&s.active.field.audit_lost,true,__ATOMIC_RELAXED);
                }
            }
        }
        xSemaphoreGive(s.lock);
    }
}

static void add_sample_identity(cJSON *sample, const char *kind)
{
    cJSON_AddStringToObject(sample, "sample_kind", kind);
    cJSON_AddNumberToObject(sample, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(sample, "boot_id", tb_boot_id());
    cJSON_AddNumberToObject(sample, "schema_version", 2);
    cJSON_AddBoolToObject(sample, "time_valid", false);
    cJSON_AddStringToObject(sample, "time_source", "boot_monotonic");
    struct timeval utc;
    if (gettimeofday(&utc, NULL) == 0 && utc.tv_sec >= 1704067200 &&
        utc.tv_usec >= 0 && utc.tv_usec < 1000000) {
        struct tm broken;
        char date[20], stamp[25];
        if (gmtime_r(&utc.tv_sec, &broken) &&
            strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &broken) == 19) {
            unsigned milliseconds = (unsigned)(utc.tv_usec / 1000) % 1000U;
            int length = snprintf(stamp, sizeof(stamp), "%.19s.%03uZ", date, milliseconds);
            if (length == 24) {
                cJSON_AddStringToObject(sample, "ts", stamp);
                cJSON_ReplaceItemInObject(sample, "time_valid", cJSON_CreateBool(true));
                cJSON_ReplaceItemInObject(sample, "time_source", cJSON_CreateString("system_clock"));
            }
        }
    }
}

static void queue_sample(cJSON *sample)
{
    if (!sample) return;
    char *json = cJSON_PrintUnformatted(sample);
    if (json && strlen(json) < TB_RECORD_MAX) tb_store_submit(json);
    else ESP_LOGW(TAG, "Telemetry snapshot exceeds record capacity");
    free(json); cJSON_Delete(sample);
}

/* One small, durable boot diagnostic, even if another reset happens before
 * the first minute's HEALTH snapshot. Storage is already initialized by core.c.
 * This does not switch the J1708 transmitter on or modify CAN configuration. */
static void record_boot_diagnostic(void)
{
    cJSON *sample=cJSON_CreateObject();
    if(!sample)return;
    add_sample_identity(sample,"BOOT_DIAGNOSTIC");
    char image_sha[65];const esp_app_desc_t *description=esp_app_get_description();
    for(unsigned i=0;i<32;i++)snprintf(image_sha+2*i,3,"%02x",description->app_elf_sha256[i]);
    cJSON_AddStringToObject(sample,"firmware",tb_version());
    cJSON_AddStringToObject(sample,"app_elf_sha256",image_sha);
    cJSON_AddNumberToObject(sample,"reset_reason",(double)esp_reset_reason());
    cJSON_AddNumberToObject(sample,"heap_free",esp_get_free_heap_size());
    cJSON_AddNumberToObject(sample,"heap_minimum",esp_get_minimum_free_heap_size());
    queue_sample(sample);
}

/* Five compact per-PID rows/minute; one rotation covers27 PIDs without a
 * continuous raw stream. [PID,MID,sent,candidates,timeouts,latency,period]. */
static void request_metric_snapshot(void)
{
    static unsigned cursor;
    tbx_j1708_request_stat_t readings[5];uint8_t mids[5];
    xSemaphoreTake(s.lock,portMAX_DELAY);
    for(unsigned i=0;i<5;i++){
        unsigned index=(cursor+i)%TBX_J1708_REQUEST_CAPACITY;readings[i]=s.requests.pids[index];
        mids[i]=s.active.configured?s.active.config.target_mid[index]:(index<17?tbx_j1708_policy()[index].mid:tbx_j1708_candidate_policy()[index-17].mid);
    }
    cursor=(cursor+5)%TBX_J1708_REQUEST_CAPACITY;xSemaphoreGive(s.lock);
    cJSON *row=cJSON_CreateObject();if(!row)return;add_sample_identity(row,"J1708_REQUEST_EVENT");
    cJSON_AddStringToObject(row,"event","POLLING_PID_STATS");
    cJSON_AddStringToObject(row,"classification","RESPONSE_CANDIDATE");
    cJSON *stats=cJSON_AddArrayToObject(row,"stats");bool complete=stats!=NULL;
    for(unsigned i=0;complete && i<5;i++){
        const tbx_j1708_request_stat_t *p=&readings[i];cJSON *a=cJSON_CreateArray();if(!a){complete=false;break;}
        complete=tb_json_add_array(a,cJSON_CreateNumber(p->pid)) && tb_json_add_array(a,cJSON_CreateNumber(mids[i])) &&
            tb_json_add_array(a,cJSON_CreateNumber(p->sent)) && tb_json_add_array(a,cJSON_CreateNumber(p->answered)) &&
            tb_json_add_array(a,cJSON_CreateNumber(p->timed_out)) &&
            tb_json_add_array(a,p->answered?cJSON_CreateNumber(p->last_latency_ms):cJSON_CreateNull()) &&
            tb_json_add_array(a,cJSON_CreateNumber(p->period_ms));
        if(complete)complete=tb_json_add_array(stats,a);else cJSON_Delete(a);
    }
    complete=complete && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(row,"sample_kind")) && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(row,"event")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"boot_id")) && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"uptime_ms"));
    if(complete)queue_sample(row);else cJSON_Delete(row);
}
static void volvo_operating_snapshot(void)
{
    cJSON *row=tb_telemetry_volvo_json();if(!row)return;
    cJSON_DeleteItemFromObjectCaseSensitive(row,"ok");add_sample_identity(row,"J1708_REQUEST_EVENT");
    bool complete=cJSON_AddStringToObject(row,"event","VOLVO_OPERATING") && cJSON_IsString(cJSON_GetObjectItemCaseSensitive(row,"sample_kind")) &&
        cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"boot_id")) && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(row,"uptime_ms"));
    if(complete)queue_sample(row);else cJSON_Delete(row);
}

static void snapshot_task(void *unused)
{
    TickType_t wake = xTaskGetTickCount();
    uint64_t window_start = esp_timer_get_time() / 1000;
    uint32_t old_can = 0, old_j1708 = 0, old_checksum = 0, old_overruns = 0;
    uint32_t old_request_sent = 0, old_request_answered = 0, old_request_timeouts = 0;
    unsigned seconds = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        uint64_t now = esp_timer_get_time() / 1000;
        tb_diagnostics_flush();
        tb_ecu_counters_flush();
        /* OPERATIONAL at 10 s in LEGACY; HEALTH remains at 60 s. */
        if (tb_operational_due(tb_profile_raw(tb_config()->full_decode_enabled), seconds)) {
            cJSON *sample = cJSON_CreateObject();
            if (sample) {
                add_sample_identity(sample, "OPERATIONAL");
                bool vehicle_data = false;
                xSemaphoreTake(s.lock, portMAX_DELAY);
                if (s.speed_at && now - s.speed_at <= 5000) {
                    cJSON_AddNumberToObject(sample, "speed_kmh", s.speed);
                    cJSON_AddStringToObject(sample, "speed_source", s.speed_j1587 ? "j1587" : "j1939");
                    vehicle_data = true;
                }
                if (s.rpm_at && now - s.rpm_at <= 5000) {
                    cJSON_AddNumberToObject(sample, "rpm", s.rpm);
                    cJSON_AddStringToObject(sample, "rpm_source", s.rpm_j1587 ? "j1587" : "j1939"); vehicle_data = true;
                }
                if (s.fuel_at && now - s.fuel_at <= 5000) {
                    cJSON_AddNumberToObject(sample, "fuel_rate_lph", s.fuel_rate);
                    cJSON_AddStringToObject(sample, "fuel_source", s.fuel_j1587 ? "j1587" : "j1939"); vehicle_data = true;
                }
                if (s.total_at && now - s.total_at <= 30000) { cJSON_AddNumberToObject(sample, "odometer_km", s.distance_total); vehicle_data = true; }
                if (s.distance_trip_valid) cJSON_AddNumberToObject(sample, "trip_distance_km", s.distance_trip);
                if (s.fuel_trip_valid) cJSON_AddNumberToObject(sample, "trip_fuel_l", s.fuel_trip);
                cJSON_AddStringToObject(sample,"distance_quality",s.trip.quality);
                tb_accounting_json(sample,&s.fuel_account);
                if (s.fuel_derived) {
                    cJSON_AddBoolToObject(sample, "fuel_derived", true);
                    cJSON_AddStringToObject(sample, "fuel_model", "MASTER_V1_2643Nm_C0_0.9720031611625598");
                }
                xSemaphoreGive(s.lock);
                if (tb_core_append_gps(sample)) vehicle_data = true;
                else { cJSON_AddNullToObject(sample, "gps"); cJSON_AddStringToObject(sample, "gps_status", "NO_FIX"); cJSON_AddBoolToObject(sample, "has_fix", false); }
                /* Add provisional J1587 scalar aliases only when the original
                 * record has space and the J1939 field is not already present. */
                if (tb_telemetry_append_j1587_operational(sample))
                    vehicle_data = true;
                /* Do not replace operational cloud state with empty bench/diagnostic rows. */
                if (vehicle_data) queue_sample(sample); else cJSON_Delete(sample);
            }
        }
        /* One J1708_FAST snapshot every 10 seconds, including when the full
         * decoder is enabled. The 27 passive PIDs are split into small flash
         * records only when necessary; NO per-frame RAW logging is enabled. */
        if ((seconds + 1U) % 10U == 0U) {
            size_t next_pid = 0;
            for (unsigned part = 0; next_pid < 27U && part < 8U; ++part) {
                cJSON *fast = cJSON_CreateObject();
                if (!fast) break;
                add_sample_identity(fast, "J1708_FAST");
                cJSON_AddStringToObject(fast, "source", "j1708");
                cJSON_AddStringToObject(fast, "origin", "PASSIVE_RX");
                size_t seen = tb_telemetry_append_j1587_fast(fast, &next_pid);
                if (seen) queue_sample(fast);
                else cJSON_Delete(fast);
            }
        }
        if((seconds+1U)%10U==0U && tb_config_j1708_auto_enabled()){
            tbx_transmission_t tr;
            xSemaphoreTake(s.lock,portMAX_DELAY);tr=s.transmission;xSemaphoreGive(s.lock);
            /* Normal overview once/minute; launch/event overview at 10 s.
             * Values are cached with exact ages, never fake synchronous sensors. */
            if(tr.launch || tr.candidate_flags!=s.transmission_last_flags || tr.launches!=s.transmission_last_launches || !s.transmission_snapshot_at || now-s.transmission_snapshot_at>=60000){
                s.transmission_snapshot_at=now;s.transmission_last_flags=tr.candidate_flags;s.transmission_last_launches=tr.launches;size_t cursor=0;
                for(unsigned part=0;part<8;part++){
                    cJSON *row=cJSON_CreateObject();if(!row)break;
                    add_sample_identity(row,"J1708_REQUEST_EVENT");
                    cJSON_AddStringToObject(row,"event","TRANSMISSION_SNAPSHOT");
                    cJSON_AddNumberToObject(row,"snapshot_ms",now);
                    cJSON_AddNumberToObject(row,"flags",tr.candidate_flags);
                    cJSON_AddNumberToObject(row,"launch_id",tr.launches);
                    cJSON_AddBoolToObject(row,"launch",tr.launch);
                    cJSON_AddBoolToObject(row,"slip_confirmed",false);
                    cJSON_AddStringToObject(row,"quality","EM_VALIDACAO");
                    size_t count=tb_telemetry_transmission_snapshot(row,&cursor,now*1000);
                    if(count)queue_sample(row);else{cJSON_Delete(row);break;}
                }
            }
        }
        if (++seconds < 60) continue;
        seconds = 0;
        request_metric_snapshot();volvo_operating_snapshot();
        cJSON *health = cJSON_CreateObject();
        if (!health) continue;
        add_sample_identity(health, "HEALTH");
        cJSON_AddNumberToObject(health, "window_seconds", (double)(now - window_start) / 1000.0);
        const cJSON *stamp = cJSON_GetObjectItemCaseSensitive(health, "ts");
        if (cJSON_IsString(stamp)) cJSON_AddStringToObject(health, "window_end", stamp->valuestring);
        cJSON_AddNumberToObject(health, "window_start_uptime_ms", (double)window_start);
        cJSON_AddNumberToObject(health, "window_end_uptime_ms", (double)now);
        cJSON *metrics = cJSON_AddObjectToObject(health, "metrics");
        cJSON_AddNumberToObject(metrics, "can_frames", s.can_frames - old_can);
        cJSON_AddNumberToObject(metrics, "j1708_frames", s.j1708_frames - old_j1708);
        cJSON_AddNumberToObject(metrics, "j1708_checksum_errors", s.j1708_checksum_errors - old_checksum);
        cJSON_AddNumberToObject(metrics, "uart_overruns", s.uart_overruns - old_overruns);
        cJSON *context = cJSON_AddObjectToObject(health, "context");
        cJSON_AddStringToObject(context, "role", "core");
        cJSON_AddStringToObject(context, "firmware", tb_version());
        cJSON_AddStringToObject(context, "telemetry_profile",
            tb_profile_raw(tb_config()->full_decode_enabled) ? "COMPLETO" : "LEGACY");
        cJSON_AddStringToObject(context, "j1708_framing", "uart_idle_10bits");
        cJSON_AddBoolToObject(context, "j1708_work_mode", s.active.work_mode);
        cJSON_AddBoolToObject(context,"j1708_automatic",tb_config_j1708_auto_enabled());
        cJSON_AddNumberToObject(context,"reset_reason",(double)esp_reset_reason());
        cJSON_AddNumberToObject(metrics,"heap_minimum",esp_get_minimum_free_heap_size());
        cJSON_AddNumberToObject(metrics,"request_event_dropped",s.request_event_dropped);
        cJSON_AddNumberToObject(metrics, "j1708_request_sent", s.requests.sent_total - old_request_sent);
        cJSON_AddNumberToObject(metrics, "j1708_request_answered", s.requests.answered_total - old_request_answered);
        cJSON_AddNumberToObject(metrics, "j1708_request_timeouts", s.requests.timeout_total - old_request_timeouts);
        old_request_sent = s.requests.sent_total;
        old_request_answered = s.requests.answered_total;
        old_request_timeouts = s.requests.timeout_total;
        if(tb_config_j1708_auto_enabled()){
            cJSON *status=cJSON_CreateObject();
            if(status){
                add_sample_identity(status,"J1708_REQUEST_EVENT");cJSON_AddStringToObject(status,"event","AUTOMATIC_STATUS");
                xSemaphoreTake(s.lock,portMAX_DELAY);
                tbx_j1708_auto_t automatic=s.automatic;bool launch=s.transmission.launch;uint32_t flags=s.transmission.candidate_flags;
                uint16_t last_pid=s.active.last_sent_pid,response_pid=s.active.last_response_pid;
                uint8_t response_mid=s.active.last_response_mid;uint64_t response_us=s.active.last_response_us;
                uint32_t latency=0;for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)if(s.requests.pids[i].pid==response_pid)latency=s.requests.pids[i].last_latency_ms;
                xSemaphoreGive(s.lock);
                cJSON_AddStringToObject(status,"state",tbx_j1708_auto_phase_name(automatic.phase));
                cJSON_AddStringToObject(status,"reason",automatic.reason?automatic.reason:"WAITING_PASSIVE_RX");
                cJSON_AddBoolToObject(status,"launch",launch);cJSON_AddNumberToObject(status,"candidate_flags",flags);
                cJSON_AddNumberToObject(status,"starts",automatic.starts);cJSON_AddNumberToObject(status,"pauses",automatic.pauses);
                if(last_pid)cJSON_AddNumberToObject(status,"last_pid",last_pid);else cJSON_AddNullToObject(status,"last_pid");
                if(response_us){
                    cJSON_AddNumberToObject(status,"response_pid",response_pid);cJSON_AddNumberToObject(status,"response_mid",response_mid);
                    cJSON_AddNumberToObject(status,"response_age_ms",((uint64_t)esp_timer_get_time()-response_us)/1000);
                    cJSON_AddNumberToObject(status,"latency_ms",latency);
                }
                queue_sample(status);
            }
        }
        /* Append optional sensors last so this routine removes readings when
         * the complete HEALTH approaches the 768-byte storage limit. */
        size_t slow_cursor=0;
        for(unsigned part=0;part<64U;part++){
            cJSON *slow=cJSON_CreateObject();if(!slow)break;
            add_sample_identity(slow,"J1708_FAST");
            cJSON_AddStringToObject(slow,"source","j1708");
            cJSON_AddStringToObject(slow,"origin","PASSIVE_RX");
            cJSON_AddNumberToObject(slow,"interval_s",60);
            size_t seen=tb_telemetry_append_j1587_slow(slow,&slow_cursor);
            if(seen)queue_sample(slow);else{cJSON_Delete(slow);break;}
        }
        s.frames_last_minute=s.j1708_frames-old_j1708;
        old_can = s.can_frames; old_j1708 = s.j1708_frames;
        old_checksum = s.j1708_checksum_errors; old_overruns = s.uart_overruns;
        window_start = now;
        queue_sample(health);
    }
}

static void j1708_task(void *unused)
{
    (void)unused;
    uint8_t frame[64], chunk[64];
    size_t length = 0;
    bool oversize = false;
    uart_event_t event;
    for (;;) {
        if (xQueueReceive(s.uart_events, &event, pdMS_TO_TICKS(20)) != pdTRUE) {
            /* At RX threshold1 the ISR can drain the last echo byte before
             * hardware timeout fires. Recover partial aborted echoes without
             * flushing the UART or waiting forever for the next ECU message. */
            if(length){
                size_t buffered=0;
                xSemaphoreTake(s.lock,portMAX_DELAY);
                bool quiet=!s.active.tx_active &&
                    (uint32_t)((uint32_t)esp_timer_get_time()-s.rx_edge_us)>=5000u &&
                    gpio_get_level(GPIO_NUM_20)!=0 &&
                    uart_ll_get_rxfifo_len(UART_LL_GET_HW(UART_NUM_1))==0 &&
                    uart_get_buffered_data_len(UART_NUM_1,&buffered)==ESP_OK && !buffered;
                xSemaphoreGive(s.lock);
                if(quiet){
                    finish_j1708(frame,length,oversize);length=0;oversize=false;
                    xSemaphoreTake(s.lock,portMAX_DELAY);s.rx_partial=false;xSemaphoreGive(s.lock);
                }
            }
            continue;
        }
        s.rx_task_busy=true;
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            ++s.uart_overruns; length = 0; oversize = false;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            s.rx_partial=false;
            tbx_j1708_rx_uart_fault(&s.active,TBX_RX_OVERRUN,(uint64_t)esp_timer_get_time());
            xSemaphoreGive(s.lock);
            uart_flush_input(UART_NUM_1); xQueueReset(s.uart_events); s.rx_task_busy=false;continue;
        }
        if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) {
            ++s.uart_overruns; length = 0; oversize = false;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            s.rx_partial=false;
            tbx_j1708_rx_uart_fault(&s.active,TBX_RX_UART_FRAME,(uint64_t)esp_timer_get_time());
            xSemaphoreGive(s.lock);
            s.rx_task_busy=false;
            continue;
        }
        if (event.type != UART_DATA) {s.rx_task_busy=false;continue;}
        size_t remaining = event.size;
        while (remaining) {
            size_t requested = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
            int got = uart_read_bytes(UART_NUM_1, chunk, requested, pdMS_TO_TICKS(20));
            if (got <= 0) {
                ++s.uart_overruns;oversize=true;
                xSemaphoreTake(s.lock,portMAX_DELAY);
                tbx_j1708_rx_uart_fault(&s.active,TBX_RX_READ_ERROR,(uint64_t)esp_timer_get_time());
                xSemaphoreGive(s.lock);break;
            }
            remaining -= got;
            xSemaphoreTake(s.lock, portMAX_DELAY);
            s.rx_partial=true;
            tbx_j1708_active_note_rx(&s.active, chunk, got, esp_timer_get_time());
            xSemaphoreGive(s.lock);
            for (int i = 0; i < got; ++i) {
                if (length < sizeof(frame)) frame[length++] = chunk[i];
                else oversize = true;
                if (length >= 5 && length <= sizeof(s.active.tx_frame) && !oversize) {
                    xSemaphoreTake(s.lock, portMAX_DELAY);
                    bool echo = tbx_j1708_active_is_tx_echo(&s.active, frame, length, esp_timer_get_time());
                    xSemaphoreGive(s.lock);
                    if (echo) {
                        finish_j1708(frame,length,false);length=0;
                        xSemaphoreTake(s.lock,portMAX_DELAY);s.rx_partial=false;xSemaphoreGive(s.lock);
                    }
                }
            }
        }
        /*
         * Hardware timeout is one 8N1 character (10 bit times), not two bits.
         * This is receive-only idle framing, verified by the message checksum.
         * Busy/noisy/coalesced traffic is reported as oversize/checksum errors.
         */
        if (event.timeout_flag) {
            finish_j1708(frame,length,oversize);length=0;oversize=false;
            xSemaphoreTake(s.lock,portMAX_DELAY);s.rx_partial=false;xSemaphoreGive(s.lock);
        }
        s.rx_task_busy=false;
    }
}

static void trip_checkpoint_task(void *unused)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (tb_web_ota_busy()) continue;
        persist_trip();
        tb_diagnostics_checkpoint();
        tb_ecu_counters_checkpoint(false);
    }
}

esp_err_t tb_capture_start(void)
{
    s.lock = xSemaphoreCreateMutex();
    s.trip_persist_lock = xSemaphoreCreateMutex();
    const tbx_j1708_active_ops_t active_ops = { .begin_tx = request_begin_tx,
        .abort_tx = request_abort_tx, .event = request_event };
    tbx_j1708_active_init(&s.active, &s.requests, &active_ops, NULL);
    s.emergency_stop = true;
    s.request_events=xQueueCreate(16,sizeof(request_event_t));
    if (!s.lock || !s.trip_persist_lock || !s.request_events) return ESP_ERR_NO_MEM;
    s.nvs_open = nvs_open("tb_trip", NVS_READWRITE, &s.nvs) == ESP_OK;
    uint32_t legacy_crc=0;
    if (s.nvs_open) {
        trip_blob_t blob = {0};
        size_t size = sizeof(blob);
        if (nvs_get_blob(s.nvs, "trip", &blob, &size) == ESP_OK &&
            ((size == sizeof(trip_blob_v1_t) && blob.version == 1) || (size == sizeof(blob) && blob.version == 2)) &&
            isfinite(blob.distance) && blob.distance >= 0 && isfinite(blob.fuel) && blob.fuel >= 0) {
            legacy_crc=tb_store_crc32(&blob,size);
            s.distance_trip = blob.distance; s.fuel_trip = blob.fuel;
            s.distance_trip_valid = blob.distance_valid; s.fuel_trip_valid = blob.fuel_valid;
            if (blob.version == 2) { s.distance_counter_valid = blob.counter_valid;
                s.distance_counter_sa = blob.counter_sa; s.distance_counter_raw = blob.counter_raw; }
        }
    }
    tbx_trip_init(&s.trip, s.distance_trip, s.distance_trip_valid);
    if (s.distance_counter_valid) tbx_trip_restore_counter(&s.trip, s.distance_counter_sa, s.distance_counter_raw);
    tbx_fuel_account_init(&s.fuel_account,s.fuel_trip,s.fuel_trip_valid);
    if(s.nvs_open){
        tb_trip_checkpoint_t candidates[2];bool valid[2]={false,false};
        for(unsigned i=0;i<2;i++){size_t size=sizeof(candidates[i]);
            valid[i]=nvs_get_blob(s.nvs,i?"trip3b":"trip3a",&candidates[i],&size)==ESP_OK &&
                size==sizeof(candidates[i]) && tb_trip_checkpoint_valid(&candidates[i]);}
        if(valid[0] || valid[1]){
            unsigned pick=valid[1] && (!valid[0] || candidates[1].generation>candidates[0].generation)?1:0;
            s.checkpoint_generation=candidates[pick].generation;
            if(legacy_crc==candidates[pick].legacy_before_crc || legacy_crc==candidates[pick].legacy_after_crc){
                tb_trip_checkpoint_restore(&candidates[pick],&s.trip,&s.fuel_account);mirror_trip_locked();
                s.fuel_trip=s.fuel_account.observed_l;s.fuel_trip_valid=s.fuel_account.valid;
            }else{
                /* Legacy firmware advanced/reset its own checkpoint after a
                 * rollback. Import it, never resurrect stale v3 anchors. */
                s.trip.quality="RECUPERADO_CHECKPOINT_LEGADO_ALTERADO";
                s.fuel_account.quality="RECUPERADO_CHECKPOINT_LEGADO_ALTERADO";
            }
        }
    }
    if(s.fuel_account.direct_valid && s.fuel_account.direct_mid!=tb_config()->j1587_fuel250_mid){
        s.fuel_account.direct_valid=false;s.fuel_account.quality="NOVA_ORIGEM_AGUARDANDO_MARCO_PID250";
    }
    /* Record the power/reset cause once at boot; per-minute HEALTH repeats
     * it so the Cloud can distinguish a crash from an uplink backlog. */
    record_boot_diagnostic();
    /* Decode selected J1939 and J1587 in both profiles. LEGACY disables
     * per-frame RAW/DECODED persistence, not the decoder or sensor cache.
     * Active J1708 requests stay explicitly authorized and parked-only. */
    esp_err_t telemetry_err = tb_telemetry_start();
    if (telemetry_err != ESP_OK) return telemetry_err;
    /* GPIO1 is deliberately never an output on the uncorrected B1 watchdog. */
    gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
    if (tb_config()->repaired_power_gates) {
        gpio_set_level(GPIO_NUM_16, 0); gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
    }
    if (!tb_config()->capture_enabled) {
        if (xTaskCreate(snapshot_task, "tb_snapshot", 6144, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        return ESP_OK;
    }
    esp_err_t err=ESP_OK;
#ifndef CONFIG_TB_J1708_ONLY
    if(tb_config()->can_enabled){
    gpio_set_level(GPIO_NUM_2, 0); gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_4, GPIO_NUM_18, TWAI_MODE_LISTEN_ONLY);
    general.tx_queue_len = 0; general.rx_queue_len = 128;
    general.intr_flags = ESP_INTR_FLAG_IRAM;
    twai_timing_config_t timing = tb_config()->can_baud == 500000 ? (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS() :
        (twai_timing_config_t)TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    err = twai_driver_install(&general, &timing, &filter);
    if (err == ESP_OK) err = twai_start();
    if (err == ESP_ERR_NO_MEM) return err;
    if (err == ESP_OK) {
        if (xTaskCreate(can_task, "tb_can", 5120, NULL, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        s.can_ready = true;
    } else snprintf(s.can_error, sizeof(s.can_error), "%s", esp_err_to_name(err));

    }
#endif
    if(tb_config()->j1708_enabled){
    gpio_set_level(GPIO_NUM_21, 0); gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
    uart_config_t uart = { .baud_rate = 9600, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT };
    err = uart_driver_install(UART_NUM_1, 2048, 0, 24, &s.uart_events, 0);
    if (err == ESP_OK) err = uart_param_config(UART_NUM_1, &uart);
    if (err == ESP_OK) err = uart_set_pin(UART_NUM_1, 5, 20, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = uart_set_rx_timeout(UART_NUM_1, 1);
    if (err == ESP_OK) err = uart_set_rx_full_threshold(UART_NUM_1, 64);
    if (err == ESP_ERR_NO_MEM) return err;
    if (err == ESP_OK) {
        if (xTaskCreate(j1708_task, "tb_j1708", 5120, NULL, 8, NULL) != pdPASS) return ESP_ERR_NO_MEM;
        s.j1708_ready = true;
        esp_err_t edge_err=gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if(edge_err==ESP_OK){
            edge_err=gpio_set_intr_type(GPIO_NUM_20,GPIO_INTR_NEGEDGE);
            if(edge_err==ESP_OK)edge_err=gpio_isr_handler_add(GPIO_NUM_20,request_rx_edge,NULL);
            gpio_intr_disable(GPIO_NUM_20);
        }
        s.edge_ready=edge_err==ESP_OK;
        if(!s.edge_ready)ESP_LOGW(TAG,"J1708 TX blocked: RX edge monitor %s",esp_err_to_name(edge_err));
        if (xTaskCreate(request_task, "tb_j1708_tx", 4096, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    } else snprintf(s.j1708_error, sizeof(s.j1708_error), "%s", esp_err_to_name(err));
    }
    if (xTaskCreate(trip_checkpoint_task, "tb_trip", 3072, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate(snapshot_task, "tb_snapshot", 6144, NULL, 4, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "CAN listen-only=%s J1708 receive-only=%s", s.can_ready ? "ready" : s.can_error,
        s.j1708_ready ? "ready" : s.j1708_error);
    return ESP_OK;
}

void tb_capture_reset_trip(void)
{
    if (!s.lock) return;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    tbx_trip_reset(&s.trip);
    mirror_trip_locked();
    s.fuel_trip = 0; tbx_fuel_account_init(&s.fuel_account,0,true);tbx_fuel_init(&s.fuel_model); s.fuel_model_at = 0;
    s.distance_trip_valid = s.fuel_trip_valid = true;
    s.speed_at = s.fuel_at = 0;
    s.distance_counter_valid = false; s.distance_counter_at = 0;
    xSemaphoreGive(s.lock);
    persist_trip();
}

static void add_signal_field(cJSON *object, const char *name, bool valid, double value)
{
    if (valid) cJSON_AddNumberToObject(object, name, value);
    else cJSON_AddNullToObject(object, name);
}

static void request_stats_snapshot(cJSON *requests,unsigned offset,unsigned limit,
    const tbx_j1708_request_scheduler_t *schedule,const tbx_j1708_active_t *active)
{
    cJSON *stats = cJSON_AddArrayToObject(requests, "stats");
    if(!stats)return;
    for(unsigned i=offset;i<TBX_J1708_REQUEST_CAPACITY && i<offset+limit;i++){
        const tbx_j1708_request_stat_t *stat = &schedule->pids[i];
        cJSON *row = cJSON_CreateObject();
        if(!row)break;
        cJSON_AddNumberToObject(row, "pid", stat->pid);
        cJSON_AddBoolToObject(row, "enabled", stat->enabled);
        cJSON_AddNumberToObject(row,"target_mid",active->configured?active->config.target_mid[i]:(i<17?tbx_j1708_policy()[i].mid:tbx_j1708_candidate_policy()[i-17].mid));
        cJSON_AddNumberToObject(row, "sent", stat->sent);
        cJSON_AddNumberToObject(row, "response_candidates", stat->answered);
        cJSON_AddNumberToObject(row, "timeouts", stat->timed_out);
        cJSON_AddNumberToObject(row,"interval_ms",stat->period_ms);
        uint32_t now_ms=(uint32_t)((uint64_t)esp_timer_get_time()/1000u);
        if(stat->sent)cJSON_AddNumberToObject(row,"last_sent_age_ms",(uint32_t)(now_ms-stat->last_sent_ms));else cJSON_AddNullToObject(row,"last_sent_age_ms");
        if(stat->answered)cJSON_AddNumberToObject(row,"last_response_age_ms",(uint32_t)(now_ms-stat->last_answered_ms));else cJSON_AddNullToObject(row,"last_response_age_ms");
        cJSON_AddNumberToObject(row,"next_due_uptime_ms",stat->next_due_ms);
        if(cJSON_GetArraySize(row)!=10){cJSON_Delete(row);break;}
        if(stat->sent)cJSON_AddNumberToObject(row,"response_rate_pct",100.0*stat->answered/stat->sent);else cJSON_AddNullToObject(row,"response_rate_pct");
        if(stat->answered){cJSON_AddNumberToObject(row,"latency_ms",stat->last_latency_ms);}
        if(active->field.started_us && i<TBX_J1708_REQUEST_COUNT){
            const tbx_j1708_field_pid_t *v=&active->field.pids[i];cJSON *field=cJSON_AddObjectToObject(row,"field");
            cJSON_AddNumberToObject(field,"before",v->before);cJSON_AddNumberToObject(field,"after",v->after);
            cJSON_AddNumberToObject(field,"controls",v->controls);cJSON_AddNumberToObject(field,"control_hits",v->control_hits);
            cJSON_AddNumberToObject(field,"sent",v->sent);cJSON_AddNumberToObject(field,"echoes",v->echoes);cJSON_AddNumberToObject(field,"replies",v->replies);
            if(v->replies){cJSON_AddNumberToObject(field,"latency_min_us",v->latency_min_us);cJSON_AddNumberToObject(field,"latency_max_us",v->latency_max_us);}
            else{cJSON_AddNullToObject(field,"latency_min_us");cJSON_AddNullToObject(field,"latency_max_us");}
            if(!field || cJSON_GetArraySize(field)!=9){cJSON_Delete(row);break;}
        }
        if(!tb_json_add_array(stats,row))break;
    }
}

static void capture_json_mode(cJSON *object,bool compact)
{
    uint64_t now = esp_timer_get_time() / 1000;
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return;
    /* Fixed snapshot only; JSON allocation/serialization must not hold the RX/TX mutex. */
    __typeof__(s) view=s;
    view.active.scheduler=&view.requests;
    xSemaphoreGive(s.lock);
    add_signal_field(object, "rpm", view.rpm_at && now - view.rpm_at <= 5000, view.rpm);
    add_signal_field(object, "speedKph", view.speed_at && now - view.speed_at <= 5000, view.speed);
    add_signal_field(object, "distanceTotalKm", view.total_at && now - view.total_at <= 30000, view.distance_total);
    add_signal_field(object, "distanceTestKm", view.distance_trip_valid, view.distance_trip);
    add_signal_field(object, "fuelLitersTest", view.fuel_trip_valid, view.fuel_trip);
    tb_accounting_json(object,&view.fuel_account);
    cJSON_AddBoolToObject(object,"odometer_validated",tb_config()->j1587_odometer_validated);
    cJSON_AddBoolToObject(object,"fuel250_validated",tb_config()->j1587_fuel250_validated);
    cJSON_AddStringToObject(object, "trip_distance_quality", view.trip.quality);
    cJSON_AddNumberToObject(object, "trip_distance_gaps", view.trip.gaps);
    cJSON_AddNumberToObject(object, "trip_counter_resets", view.trip.resets);
    cJSON_AddNumberToObject(object, "trip_counter_range_errors", view.trip.implausible);
    cJSON_AddNullToObject(object, "combinationWeightKg");
    cJSON *can = cJSON_AddObjectToObject(object, "can");
    cJSON_AddBoolToObject(can,"enabled",tb_config()->capture_enabled && tb_config()->can_enabled);
    cJSON_AddStringToObject(object,"capture_profile",tb_config()->can_enabled?"DUAL":"J1708_ONLY");
    cJSON_AddBoolToObject(can, "ready", view.can_ready);
    cJSON_AddBoolToObject(can, "listen_only", true);
    cJSON_AddNumberToObject(can, "baud", tb_config()->can_baud);
    cJSON_AddNumberToObject(can, "frames", view.can_frames);
    cJSON_AddNumberToObject(can, "remote_frames", view.can_rtr);
    cJSON_AddNumberToObject(can, "read_errors", view.can_errors);
    cJSON_AddStringToObject(can, "error", view.can_error);
#ifndef CONFIG_TB_J1708_ONLY
    twai_status_info_t status;
    if (view.can_ready && twai_get_status_info(&status) == ESP_OK) {
        cJSON_AddNumberToObject(can, "rx_missed", status.rx_missed_count);
        cJSON_AddNumberToObject(can, "rx_overrun", status.rx_overrun_count);
        cJSON_AddNumberToObject(can, "bus_errors", status.bus_error_count);
    }
 #endif
    cJSON *j1708 = cJSON_AddObjectToObject(object, "j1708");
    cJSON_AddBoolToObject(j1708,"enabled",tb_config()->capture_enabled && tb_config()->j1708_enabled);
    cJSON_AddBoolToObject(j1708, "ready", view.j1708_ready);
    cJSON_AddStringToObject(j1708, "framing", "uart_idle_10bits_checksum_validated");
    cJSON_AddStringToObject(j1708, "error", view.j1708_error);
    cJSON_AddNumberToObject(j1708, "frames", view.j1708_frames);
    cJSON_AddNumberToObject(j1708, "checksum_errors", view.j1708_checksum_errors);
    cJSON_AddNumberToObject(j1708, "oversize_or_coalesced", view.j1708_oversize);
    cJSON_AddNumberToObject(j1708, "uart_overruns", view.uart_overruns);
    /* MIDs actually observed during this boot, to help the operator choose
     * a source address. Not observed != guaranteed free on the entire bus. */
    cJSON *observed = cJSON_AddArrayToObject(j1708, "observed_mids");
    if (observed) {
        for (unsigned mid = 128; mid <= 255; ++mid)
            if (view.observed_mid[mid >> 3] & (1u << (mid & 7u)))
                if(!tb_json_add_array(observed,cJSON_CreateNumber(mid)))break;
    }
    if (view.j1708_at) {
        cJSON_AddNumberToObject(j1708, "last_frame_age_ms", (double)(now - view.j1708_at));
        if(!compact){cJSON *last=cJSON_Parse(view.j1708_last);if(last)tb_json_add_object(j1708,"last_frame",last);}
    }
    cJSON *requests = cJSON_AddObjectToObject(j1708, "requests");
    cJSON_AddBoolToObject(requests, "armed", view.requests.armed);
    cJSON_AddBoolToObject(requests,"hardware_tx_available",view.j1708_ready && view.edge_ready);
    cJSON_AddStringToObject(requests,"hardware_tx_validation","EM_VALIDACAO");
    cJSON_AddBoolToObject(requests,"rx_edge_monitor_ready",view.edge_ready);
    cJSON_AddNumberToObject(requests,"rx_edges",view.rx_edges);
    cJSON_AddNumberToObject(requests,"bus_deferrals",view.requests.deferred_total);
    cJSON_AddNumberToObject(requests,"event_dropped",view.request_event_dropped);
    cJSON_AddNumberToObject(requests,"uart_start_faults",view.uart_start_faults);
    cJSON_AddNumberToObject(requests,"consecutive_tx_errors",view.requests.link_error_count);
    cJSON_AddNumberToObject(requests,"consecutive_request_failures",view.active.consecutive_failures);
    if(view.requests.sent_total)cJSON_AddNumberToObject(requests,"last_requested_pid",view.active.last_sent_pid);
    cJSON_AddBoolToObject(requests, "configured", view.active.configured);
    cJSON_AddNumberToObject(j1708,"frames_per_minute",view.frames_last_minute);
    cJSON *automatic=cJSON_AddObjectToObject(requests,"automatic");
    cJSON_AddBoolToObject(automatic,"enabled",tb_config_j1708_auto_enabled());
    cJSON_AddBoolToObject(automatic,"persisted",tb_config_j1708_auto_persisted());
    cJSON_AddStringToObject(automatic,"state",tbx_j1708_auto_phase_name(view.automatic.phase));
    cJSON_AddStringToObject(automatic,"reason",view.automatic.reason?view.automatic.reason:"WAITING_PASSIVE_RX");
    cJSON_AddNumberToObject(automatic,"starts",view.automatic.starts);
    cJSON_AddNumberToObject(automatic,"pauses",view.automatic.pauses);
    cJSON_AddNumberToObject(automatic,"retry_in_ms",view.automatic.resume_us>now*1000?(view.automatic.resume_us-now*1000)/1000:0);
    cJSON_AddNumberToObject(automatic,"max_requests_per_second",2);
    cJSON_AddBoolToObject(automatic,"TRANSMISSION_LAUNCH_MONITOR",view.transmission.launch);
    cJSON_AddNumberToObject(automatic,"launches",view.transmission.launches);
    cJSON_AddNumberToObject(automatic,"candidate_flags",view.transmission.candidate_flags);
    cJSON_AddBoolToObject(automatic,"slip_confirmed",false);
    cJSON_AddStringToObject(requests,"physical_validation",view.active.physical_validated?"EXTERNAL_CAPTURE_CONFIRMED_BY_OPERATOR":"PENDING_EXTERNAL_CAPTURE");
    cJSON_AddBoolToObject(requests,"physical_validated",view.active.physical_validated);
    cJSON *guard=cJSON_AddObjectToObject(requests,"rx_guard");
    const tbx_j1708_rx_guard_t *g=&view.active.rx_guard;
    cJSON_AddBoolToObject(guard,"fault_latched",g->fault_latched);
    cJSON_AddNumberToObject(guard,"suspect_windows",g->suspect_streak);
    cJSON_AddNumberToObject(guard,"reference_windows",g->reference_count);
    cJSON_AddStringToObject(guard,"last_cause",tbx_j1708_guard_cause_name(g->decision.cause));
    if(g->decision.at_us){cJSON_AddNumberToObject(guard,"decision_uptime_ms",(double)(g->decision.at_us/1000));cJSON_AddStringToObject(guard,"decision_scope","SNAPSHOT_AT_DECISION");}
    cJSON_AddNumberToObject(guard,"valid_total",g->total.valid);cJSON_AddNumberToObject(guard,"checksum_total",g->total.checksum);
    cJSON_AddNumberToObject(guard,"framing_total",g->total.framing);cJSON_AddNumberToObject(guard,"overrun_total",g->total.overruns);
    cJSON_AddNumberToObject(guard,"uart_frame_total",g->total.uart_frame);cJSON_AddNumberToObject(guard,"uart_read_total",g->total.read_errors);

    cJSON_AddStringToObject(requests,"validation_method",view.active.production_mode?"PERSISTENT_OPERATIONAL_INDIRECT":view.active.physical_validated?"EXTERNAL_INDEPENDENT":view.active.field.confirmed?"SUPERVISED_FIELD_INDIRECT":"NONE");
    const tbx_j1708_field_t *f=&view.active.field;
    cJSON *field=cJSON_AddObjectToObject(requests,"field_validation");
    cJSON_AddStringToObject(field,"phase",tbx_j1708_field_phase_name(f->phase));
    cJSON_AddBoolToObject(field,"running",tbx_j1708_field_running(f));
    cJSON_AddBoolToObject(field,"confirmed",f->confirmed);
    cJSON_AddBoolToObject(field,"external_electrical_proof",false);
    if(f->started_us){
        char session[64];snprintf(session,sizeof(session),"%" PRIu32 ":%" PRIu64,tb_boot_id(),f->started_us);
        cJSON_AddStringToObject(field,"session_id",session);cJSON_AddStringToObject(field,"reason",f->reason?f->reason:"");
        cJSON_AddNumberToObject(field,"completed_trials",f->completed);cJSON_AddNumberToObject(field,"total_trials",51);
        cJSON_AddNumberToObject(field,"contrasting_pids",f->contrasting_pids);
        cJSON_AddNumberToObject(field,"before_frames",f->before_frames);cJSON_AddNumberToObject(field,"after_frames",f->after_frames);
        cJSON_AddNumberToObject(field,"before_duration_ms",f->before_duration_us/1000.0);cJSON_AddNumberToObject(field,"after_duration_ms",f->after_duration_us/1000.0);
        cJSON_AddNumberToObject(field,"before_checksum_errors",f->before_errors);cJSON_AddNumberToObject(field,"after_checksum_errors",f->after_errors);
        cJSON_AddNumberToObject(field,"checksum_errors",f->bad_frames);
        cJSON_AddStringToObject(field,"latency_reference","SOFTWARE_REQUEST_TO_PARSED_REPLY");
        cJSON_AddBoolToObject(field,"audit_lost",__atomic_load_n(&f->audit_lost,__ATOMIC_RELAXED));
    }
    cJSON_AddBoolToObject(requests,"single_shot",view.active.config.single_shot);
    char txhex[13]={0},trial_id[64];
    for(unsigned i=0;i<view.active.tx_length;i++)snprintf(txhex+2*i,3,"%02X",view.active.tx_frame[i]);
    snprintf(trial_id,sizeof(trial_id),"%" PRIu32 ":%" PRIu64,tb_boot_id(),view.active.tx_started_us);
    cJSON_AddStringToObject(requests,"expected_trial_tx",txhex);
    cJSON_AddStringToObject(requests,"trial_id",trial_id);
    if(view.active.last_response_us){
        cJSON_AddNumberToObject(requests,"last_response_pid",view.active.last_response_pid);
        cJSON_AddNumberToObject(requests,"last_response_mid",view.active.last_response_mid);
        cJSON_AddNumberToObject(requests,"last_response_age_ms",(double)((uint64_t)esp_timer_get_time()-view.active.last_response_us)/1000.0);
    }
    cJSON_AddBoolToObject(requests, "work_mode", view.active.work_mode);
    cJSON_AddBoolToObject(requests, "work_evidence_ready", tbx_j1708_active_work_ready(&view.active,(uint64_t)esp_timer_get_time()));
    cJSON_AddBoolToObject(requests, "safety_stopped", view.requests.safety_stopped);
    cJSON_AddStringToObject(requests, "validation", "EM_VALIDACAO");
    cJSON_AddNumberToObject(requests, "echo_ok", view.active.echo_ok);
    cJSON_AddNumberToObject(requests, "collisions", view.active.collisions);
    cJSON_AddNumberToObject(requests, "echo_timeouts", view.active.echo_timeouts);
    cJSON_AddNumberToObject(requests, "lease_expirations", view.active.lease_expirations);
    cJSON_AddNumberToObject(requests, "authorization_lease_ms",
        view.active.production_mode?0:tbx_j1708_field_running(&view.active.field)?TBX_J1708_FIELD_LEASE_US/1000:view.active.work_mode ? TBX_J1708_WORK_LEASE_US / 1000 : TBX_J1708_ACTIVE_LEASE_US / 1000);
    cJSON_AddNumberToObject(requests, "lease_remaining_ms", !view.active.production_mode && view.requests.armed &&
        view.active.lease_deadline_us > (uint64_t)esp_timer_get_time() ?
        (double)(view.active.lease_deadline_us - (uint64_t)esp_timer_get_time()) / 1000.0 : 0);
    cJSON_AddStringToObject(requests, "block_reason", tb_config_j1708_auto_enabled()?(view.automatic.reason?view.automatic.reason:"WAITING_PASSIVE_RX"):!view.active.configured?"EXPLICIT_MIDS_AND_TOPOLOGY_ATTESTATION_REQUIRED":view.requests.safety_stopped?"THREE_TX_FAILURES_OR_MID_CONFLICT":!(view.active.physical_validated || view.active.field.confirmed)?"EXTERNAL_OR_SUPERVISED_FIELD_VALIDATION_REQUIRED":"NONE_OR_SAFETY_INTERLOCK");
    cJSON_AddBoolToObject(requests,"sequential_scan",view.active.config.sequential_scan);
    if(view.active.config.sequential_scan){
        cJSON_AddNumberToObject(requests,"start_pid",
            view.active.config.start_pid?view.active.config.start_pid:view.requests.pids[0].pid);
        cJSON_AddNumberToObject(requests,"scan_total",view.active.scan_budget);
        cJSON_AddNumberToObject(requests,"scan_sent",
            view.active.scan_completed+(view.requests.pending<TBX_J1708_REQUEST_CAPACITY?1:0));
        cJSON_AddNumberToObject(requests,"scan_attempts",
            view.requests.sent_total>=view.active.scan_sent_baseline?
              view.requests.sent_total-view.active.scan_sent_baseline:0);
        if(view.requests.pending<TBX_J1708_REQUEST_CAPACITY)
            cJSON_AddNumberToObject(requests,"pending_pid",view.requests.pids[view.requests.pending].pid);
    }
    cJSON_AddNumberToObject(requests, "min_spacing_ms", view.requests.min_spacing_ms);
    cJSON_AddNumberToObject(requests, "timeout_ms", view.requests.timeout_ms);
    cJSON_AddNumberToObject(requests, "sent", view.requests.sent_total);
    cJSON_AddNumberToObject(requests, "responses", view.requests.answered_total);
    cJSON_AddNumberToObject(requests, "timeouts", view.requests.timeout_total);
    cJSON_AddNumberToObject(requests, "tx_errors", view.requests.tx_failure_total);
    if(!compact){
        cJSON *pids=cJSON_AddArrayToObject(requests,"pids");
        if(pids)for(unsigned i=0;i<TBX_J1708_REQUEST_CAPACITY;i++)
            if(!tb_json_add_array(pids,cJSON_CreateNumber(view.requests.pids[i].pid)))break;
    }
    if(!compact){
        request_stats_snapshot(requests,0,TBX_J1708_REQUEST_CAPACITY,&view.requests,&view.active);
        if(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(requests,"stats"))!=TBX_J1708_REQUEST_CAPACITY)
            cJSON_AddBoolToObject(requests,"diagnostic_partial",true);
    }
    if (view.active.configured) cJSON_AddNumberToObject(requests, "local_mid", view.active.config.local_mid);
    cJSON_AddNumberToObject(requests, "idle_guard_us", view.active.idle_guard_us);
    cJSON_AddStringToObject(object, "telemetry_profile",
        tb_profile_raw(tb_config()->full_decode_enabled) ? "COMPLETO" : "LEGACY");
    cJSON_AddNumberToObject(object, "operational_interval_s",
        tb_profile_raw(tb_config()->full_decode_enabled) ? 1 : TB_LEGACY_OPERATIONAL_SECONDS);
    tb_telemetry_add_json(object);
}

void tb_capture_add_json(cJSON *object){capture_json_mode(object,false);}
void tb_capture_add_json_compact(cJSON *object){capture_json_mode(object,true);}
cJSON *tb_capture_request_stats_json(unsigned offset,unsigned limit)
{
    cJSON *o=cJSON_CreateObject();if(!o)return NULL;
    if(!s.lock || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE){cJSON_AddBoolToObject(o,"ok",false);cJSON_AddStringToObject(o,"error","capture_busy");return o;}
    const tbx_j1708_request_scheduler_t schedule=s.requests;
    const tbx_j1708_active_t active=s.active;
    xSemaphoreGive(s.lock);
    if(offset>TBX_J1708_REQUEST_CAPACITY)offset=TBX_J1708_REQUEST_CAPACITY;
    if(!limit || limit>8)limit=8;
    bool valid=cJSON_AddBoolToObject(o,"ok",true) && cJSON_AddNumberToObject(o,"offset",offset) &&
        cJSON_AddNumberToObject(o,"total",TBX_J1708_REQUEST_CAPACITY);
    if(valid)request_stats_snapshot(o,offset,limit,&schedule,&active);
    unsigned count=(unsigned)cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(o,"stats"));
    unsigned expected=TBX_J1708_REQUEST_CAPACITY-offset;if(expected>limit)expected=limit;
    valid=valid && cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(o,"stats")) && count==expected && cJSON_AddNumberToObject(o,"count",count);
    if(!valid){cJSON_Delete(o);return NULL;}
    return o;
}

bool tb_capture_tx_ready(void) { return s.j1708_ready && s.edge_ready; }

/* Read-only maintenance interlock. Never changes acquisition or TX policy. */
bool tb_capture_storage_stationary(void)
{
    if(!s.lock || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    const uint64_t now=(uint64_t)esp_timer_get_time()/1000u;
    bool parked=s.speed_at && now>=s.speed_at && now-s.speed_at<=5000u &&
        isfinite(s.speed) && s.speed>=0 && s.speed<=0.1;
    xSemaphoreGive(s.lock);return parked;
}

void tb_capture_pause_maintenance(void)
{
    s.emergency_stop=true;s.operator_hold=true;request_abort_tx(NULL);
    if(!s.lock || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE)return;
    tbx_j1708_active_stop(&s.active,"MAINTENANCE_PAUSE");xSemaphoreGive(s.lock);
}

bool tb_capture_request_control(bool arm, bool authorized, bool stationary)
{
    bool saved=true;
    if (!arm) {
        s.emergency_stop=true;s.operator_hold=true;request_abort_tx(NULL);
        saved=tb_config_set_j1708_auto(false)==ESP_OK;
    }
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool accepted = saved;
    if(arm && tb_config_j1708_auto_enabled()){xSemaphoreGive(s.lock);return false;}
    if (arm && tbx_j1708_field_running(&s.active.field)){xSemaphoreGive(s.lock);return false;}
    if (arm) {
        const uint64_t now_ms=(uint64_t)(esp_timer_get_time()/1000);
        const bool speed_fresh=s.speed_at && now_ms>=s.speed_at && now_ms-s.speed_at<=5000u;
        const bool movement=speed_fresh && s.speed>0.1;
        const bool parked_verified=speed_fresh && s.speed<=0.1;
        if(s.edge_ready && !s.edge_enabled){
            s.rx_edge_us=(uint32_t)esp_timer_get_time();
            s.edge_enabled=gpio_intr_enable(GPIO_NUM_20)==ESP_OK;
        }
        accepted = s.j1708_ready && s.edge_enabled && !tb_web_ota_busy() &&
            (!movement || s.active.work_mode) &&
            (s.active.work_mode || parked_verified) &&
            tbx_j1708_active_arm(&s.active, authorized, stationary, esp_timer_get_time());
        s.emergency_stop = !accepted;
        if(accepted)s.operator_hold=false;
        if (!accepted) tbx_j1708_active_stop(&s.active, "ARM_INTERLOCK_REFUSED");
    } else tbx_j1708_active_stop(&s.active, "OPERATOR_STOP");
    xSemaphoreGive(s.lock);
    char event[384];
    snprintf(event, sizeof(event), "{\"sample_kind\":\"J1708_REQUEST_EVENT\",\"boot_id\":%" PRIu32 ",\"uptime_ms\":%" PRIu64 ",\"action\":\"%s\",\"accepted\":%s,\"reason\":\"%s\"}",
        tb_boot_id(), (uint64_t)(esp_timer_get_time() / 1000), arm ? "arm" : "stop", accepted ? "true" : "false",
        arm ? (accepted ? (s.active.work_mode ? "AUTHORIZED_WORK_MODE" : "AUTHORIZED_PARKED_CONFIGURED") : "SAFETY_OR_CONFIGURATION_REQUIRED") : "operator_stop");
    tb_store_submit(event);
    return accepted;
}


bool tb_capture_request_enable_work(bool authorized, bool stationary)
{
    if(tb_config_j1708_auto_enabled())return false;
    if (!s.lock || !authorized || !stationary || !s.edge_ready || tb_web_ota_busy() ||
        xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    const uint64_t now = (uint64_t)esp_timer_get_time();
    const uint64_t now_ms = now / 1000u;
    /* Speed 0 must be observed freshly in the vehicle, not merely asserted
     * by the caller. A stopped vehicle can then operate with work-mode TX
     * after the operator confirms an external TX/response capture. */
    const bool parked = s.speed_at && now_ms >= s.speed_at &&
        now_ms - s.speed_at <= 5000u && s.speed <= 0.1;
    if(parked && !s.edge_enabled){
        s.rx_edge_us=(uint32_t)esp_timer_get_time();
        s.edge_enabled=gpio_intr_enable(GPIO_NUM_20)==ESP_OK;
    }
    const uint8_t local=s.active.config.local_mid;
    const bool local_free=!(s.observed_mid[local>>3] & (1u<<(local&7u)));
    const bool accepted = s.j1708_ready && s.edge_enabled && parked && local_free &&
        tbx_j1708_active_enable_work(&s.active, now);
    xSemaphoreGive(s.lock);
    return accepted;
}

bool tb_capture_automatic_enable(void)
{
    if(tb_config_set_j1708_auto(true)!=ESP_OK)return false;
    if(!s.lock || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    s.operator_hold=false;s.automatic.last_enabled=false;
    xSemaphoreGive(s.lock);return true;
}

static bool request_integer(const cJSON *object, const char *name, int min, int max, int *result)
{
    const cJSON *number = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(number) || !isfinite(number->valuedouble) ||
        number->valuedouble < min || number->valuedouble > max || floor(number->valuedouble) != number->valuedouble) return false;
    *result = number->valueint; return true;
}

bool tb_capture_request_configure(const cJSON *config, char *error, size_t error_size)
{
    if(tb_config_j1708_auto_enabled()){snprintf(error,error_size,"polling automatico habilitado; ensaio manual nao controla producao");return false;}
    tbx_j1708_active_config_t next = { .priority = 8 };
    int local, priority;
    if (!request_integer(config, "local_mid", 128, 255, &local) ||
        !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(config, "topology_confirmed"))) {
        snprintf(error, error_size, "local_mid explicito e topology_confirmed=true obrigatorios"); return false;
    }
    next.local_mid = local; next.inverted_de21_attested = true;
    /* Stage A always sends exactly one supervised parked request. */
    const cJSON *scan=cJSON_GetObjectItemCaseSensitive(config,"sequential_scan");
    if(scan && !cJSON_IsBool(scan)){snprintf(error,error_size,"sequential_scan deve ser booleano");return false;}
    next.sequential_scan=true;next.single_shot=true; /* Stage A: only one real request. */
    const cJSON *start=cJSON_GetObjectItemCaseSensitive(config,"start_pid");
    if(start){
        int pid;
        if(!request_integer(config,"start_pid",0,255,&pid)){
            snprintf(error,error_size,"start_pid invalido");return false;
        }
        next.start_pid=(uint16_t)pid;
    }
    if (cJSON_GetObjectItemCaseSensitive(config, "priority")) {
        if (!request_integer(config, "priority", 1, 8, &priority)) {
            snprintf(error, error_size, "priority deve ser inteiro 1..8"); return false;
        }
        next.priority = priority;
    }
    const cJSON *targets = cJSON_GetObjectItemCaseSensitive(config, "targets");
    const cJSON *preset = cJSON_GetObjectItemCaseSensitive(config, "preset");
    if (cJSON_IsString(preset) &&
        !strcmp(preset->valuestring, "fh2013_capture_20261029") && !targets) {
        /* Evidence: Validator 29/10, 70,386 J1708 frames; MID/PID time
         * correlations, NOT proof of causality or universal Volvo mapping.
         * Order: 26,33,34,37,62,68,117,118,235,236,250,31,32,63,108,158,160.
         * Local MID is NEVER inferred from these target MIDs. */
        for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++)next.target_mid[i]=tbx_j1708_policy()[i].mid;
        for (unsigned i = 0; i < TBX_J1708_REQUEST_COUNT; ++i) {
            if (next.target_mid[i] == next.local_mid) {
                snprintf(error, error_size, "MID local conflita com preset de alvos");
                return false;
            }
        }
    } else {
        if (preset || !cJSON_IsArray(targets) ||
            cJSON_GetArraySize(targets) != TBX_J1708_REQUEST_COUNT) {
            snprintf(error, error_size, "informe 17 PIDs/MIDs ou preset historico explicito"); return false;
        }
        tbx_j1708_request_scheduler_t defaults;
        tbx_j1708_request_init(&defaults);
        const cJSON *target;
        cJSON_ArrayForEach(target, targets) {
            int pid, mid;
            if (!request_integer(target, "pid", 0, 255, &pid) ||
                !request_integer(target, "mid", 128, 255, &mid) || mid == local) {
                snprintf(error, error_size, "PID/MID alvo invalido ou igual ao MID local");
                return false;
            }
            unsigned i;
            for (i = 0; i < TBX_J1708_REQUEST_COUNT && defaults.pids[i].pid != pid; ++i) { }
            if (i == TBX_J1708_REQUEST_COUNT || next.target_mid[i]) {
                snprintf(error, error_size, "PID alvo desconhecido ou duplicado"); return false;
            }
            next.target_mid[i] = mid;
        }
    }
    if (!s.lock || xSemaphoreTake(s.lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        snprintf(error, error_size, "captura ocupada"); return false;
    }
    bool accepted = false;
    if (s.observed_mid[next.local_mid >> 3] & (1u << (next.local_mid & 7u))) {
        snprintf(error, error_size, "MID local ja observado na rede; selecione MID livre confirmado");
    } else if(tbx_j1708_field_running(&s.active.field)){
        snprintf(error,error_size,"validacao supervisionada em andamento; pare antes de reconfigurar");
    } else if (s.requests.armed) {
        accepted = next.local_mid == s.active.config.local_mid && next.priority == s.active.config.priority &&
            memcmp(next.target_mid, s.active.config.target_mid, sizeof(next.target_mid)) == 0 &&
            next.sequential_scan==s.active.config.sequential_scan && next.start_pid==s.active.config.start_pid;
        if (!accepted) snprintf(error, error_size, "pare requisicoes antes de alterar configuracao");
    } else {
        accepted = s.j1708_ready && tbx_j1708_active_configure(&s.active, &next);
        if(accepted){
            s.trial_reply_size=0;s.requests.timeout_ms=tb_config()->j1708_timeout_ms;
            for(unsigned i=0;i<TBX_J1708_REQUEST_COUNT;i++)s.requests.pids[i].period_ms=tb_config()->j1708_intervals_ms[i];
        }
        if (!accepted) snprintf(error, error_size, "UART indisponivel ou configuracao recusada");
    }
    xSemaphoreGive(s.lock);
    return accepted;
}

static size_t evidence_hex(const cJSON *value,uint8_t *bytes,size_t capacity)
{
    if(!cJSON_IsString(value))return 0;
    unsigned half=0,v=0;size_t count=0;
    for(const unsigned char *p=(const unsigned char *)value->valuestring;*p;p++){
        if(*p==' ' || *p==':' || *p=='-' || *p=='\n' || *p=='\r')continue;
        unsigned digit;
        if(*p>='0' && *p<='9')digit=*p-'0';
        else if(*p>='a' && *p<='f')digit=*p-'a'+10;
        else if(*p>='A' && *p<='F')digit=*p-'A'+10;
        else return 0;
        v=(v<<4)|digit;
        if(++half==2){if(count>=capacity)return 0;bytes[count++]=(uint8_t)v;v=half=0;}
    }
    return half?0:count;
}
bool tb_capture_confirm_physical(const cJSON *body)
{
    if(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"external_capture_verified")) ||
       !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"local_mid_free_verified")) ||
       !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"stationary")))return false;
    uint8_t tx[5],rx[21];
    size_t nt=evidence_hex(cJSON_GetObjectItemCaseSensitive(body,"tx_observed_hex"),tx,sizeof(tx));
    size_t nr=evidence_hex(cJSON_GetObjectItemCaseSensitive(body,"response_observed_hex"),rx,sizeof(rx));
    const cJSON *nonce=cJSON_GetObjectItemCaseSensitive(body,"trial_id");
    if(nt!=5 || !nr || !cJSON_IsString(nonce) || !tb_protocol_j1708_checksum_valid(tx,nt) ||
       !tb_protocol_j1708_checksum_valid(rx,nr) || !s.lock || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    char id[64];snprintf(id,sizeof(id),"%" PRIu32 ":%" PRIu64,tb_boot_id(),s.active.tx_started_us);
    uint64_t now=(uint64_t)esp_timer_get_time();
    bool parked=s.speed_at && now/1000>=s.speed_at && now/1000-s.speed_at<=5000 && s.speed<=0.1;
    bool valid=parked && !tbx_j1708_field_running(&s.active.field) && !strcmp(id,nonce->valuestring) && !memcmp(tx,s.active.tx_frame,5) &&
        nr==s.trial_reply_size && !memcmp(rx,s.trial_reply,nr) &&
        tbx_j1708_active_attest_physical(&s.active,true,true,now);
    xSemaphoreGive(s.lock);
    if(valid){
        cJSON *audit=cJSON_CreateObject();
        if(audit){
            add_sample_identity(audit,"J1708_REQUEST_EVENT");
            cJSON_AddStringToObject(audit,"event","PHYSICAL_EVIDENCE");
            cJSON_AddStringToObject(audit,"classification","OPERATOR_CONFIRMED_EXTERNAL_CAPTURE");
            cJSON_AddStringToObject(audit,"trial_id",id);
            char txtext[11],rxtext[43];
            for(unsigned i=0;i<5;i++)snprintf(txtext+2*i,3,"%02X",tx[i]);
            for(size_t i=0;i<nr;i++)snprintf(rxtext+2*i,3,"%02X",rx[i]);
            cJSON_AddStringToObject(audit,"tx_observed_hex",txtext);
            cJSON_AddStringToObject(audit,"response_observed_hex",rxtext);
            queue_sample(audit);
        }
    }
    return valid;
}

bool tb_capture_field_start(bool authorized,bool stationary)
{
    if(!authorized || !stationary || !s.lock || tb_web_ota_busy() || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    uint64_t now=(uint64_t)esp_timer_get_time();uint8_t mid=s.active.config.local_mid;
    bool parked=s.speed_at && now/1000>=s.speed_at && now/1000-s.speed_at<=5000 && s.speed<=0.1;
    if(parked && s.edge_ready && !s.edge_enabled){s.rx_edge_us=(uint32_t)now;s.edge_enabled=gpio_intr_enable(GPIO_NUM_20)==ESP_OK;}
    bool accepted=s.j1708_ready && s.edge_enabled && parked && !(s.observed_mid[mid>>3]&(1u<<(mid&7u))) &&
        tbx_j1708_field_start(&s.active,authorized,stationary,now);
    if(accepted){s.emergency_stop=false;s.operator_hold=false;}
    xSemaphoreGive(s.lock);return accepted;
}
bool tb_capture_field_confirm(const cJSON *body)
{
    const cJSON *nonce=cJSON_GetObjectItemCaseSensitive(body,"session_id");
    if(!cJSON_IsString(nonce) || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"authorized")) ||
       !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"stationary")) ||
       !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"indirect_validation_acknowledged")) ||
       !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body,"local_mid_free_verified")) || !s.lock ||
       tb_web_ota_busy() || xSemaphoreTake(s.lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    uint64_t now=(uint64_t)esp_timer_get_time();char session[64];
    snprintf(session,sizeof(session),"%" PRIu32 ":%" PRIu64,tb_boot_id(),s.active.field.started_us);
    bool parked=s.speed_at && now/1000>=s.speed_at && now/1000-s.speed_at<=5000 && s.speed<=0.1;
    uint8_t mid=s.active.config.local_mid;
    bool accepted=parked && !strcmp(session,nonce->valuestring) && !(s.observed_mid[mid>>3]&(1u<<(mid&7u))) &&
        tbx_j1708_field_confirm(&s.active,s.active.field.started_us,true,now);
    xSemaphoreGive(s.lock);return accepted;
}
