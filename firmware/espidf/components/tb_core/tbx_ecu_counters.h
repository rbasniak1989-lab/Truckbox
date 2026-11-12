#pragma once
#include "tbx_j1587_bridge.h"

typedef enum {TBX_COUNTER_BOOT=1,TBX_COUNTER_STOP,TBX_COUNTER_PARKED,TBX_COUNTER_TRIP_START,TBX_COUNTER_TRIP_END}tbx_counter_reason_t;
typedef struct {
    uint32_t raw,boot;
    uint64_t uptime_ms,unix_ms;
    uint8_t mid;
    bool valid;
}tbx_ecu_counter_t;
typedef struct {
    tbx_counter_reason_t reason;
    char trip_uid[65];
    uint64_t requested_unix_ms,requested_uptime_ms,not_before_ms;uint32_t requested_boot,queue_age_ms;uint8_t attempt;
}tbx_counter_job_t;
typedef struct {
    tbx_ecu_counter_t latest[3],baseline[3],captured[3];
    tbx_counter_job_t job,waiting[4],start_job;
    uint32_t sequence,completed,failed,queue_full,baseline_generation,revision;
    uint64_t boot_started_ms,stopped_since_ms,last_parked_ms,phase_started_ms;
    uint32_t phase_sent,phase_timeouts;
    uint8_t phase,queued,missing;
    bool active,waiting_response,dirty,record_ready,boot_requested,stop_requested,previous_speed_valid;
    double previous_speed;
    bool trip_open;
    char trip_uid[65];
}tbx_ecu_counters_t;
void tbx_ecu_counters_init(tbx_ecu_counters_t *);
bool tbx_ecu_counters_trigger(tbx_ecu_counters_t *,tbx_counter_reason_t,const char *trip_uid);
void tbx_ecu_counters_motion(tbx_ecu_counters_t *,bool speed_valid,double speed,bool ignition_available,uint64_t now_ms,uint32_t dwell_s,uint32_t parked_interval_s);
void tbx_ecu_counters_tick(tbx_ecu_counters_t *,tbx_j1708_request_scheduler_t *,uint64_t now_ms);
void tbx_ecu_counters_observe(tbx_ecu_counters_t *,const tbx_j1587_observation_t *,bool response_candidate,uint32_t boot,uint64_t unix_ms);
/* 0=measured comparable interval; 1=missing; 2=changed MID/reset/rollover;
 * 3=implausible/time unknown. Never manufactures a wrapped delta. */
unsigned tbx_ecu_counter_delta(const tbx_ecu_counter_t *,const tbx_ecu_counter_t *,unsigned index,double *delta);
uint16_t tbx_ecu_counter_pid(unsigned index);
