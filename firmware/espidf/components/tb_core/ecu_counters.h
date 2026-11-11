#pragma once
#include "tbx_ecu_counters.h"
#include "cJSON.h"
void tb_ecu_counters_start(void);
void tb_ecu_counters_tick(tbx_j1708_request_scheduler_t *,bool,double,bool,uint64_t,uint32_t,uint32_t);
void tb_ecu_counters_observe(const tbx_j1587_observation_t *,bool candidate);
bool tb_ecu_counters_boundary(bool start,const char *trip_uid);
bool tb_ecu_counters_boundary_at(bool start,const char *trip_uid,uint64_t requested_unix_ms,uint32_t queue_age_ms);
cJSON *tb_ecu_counters_json(void);
void tb_ecu_counters_flush(void);
bool tb_ecu_counters_checkpoint(bool force);
