#pragma once
#include "tbx_j1587_bridge.h"
typedef struct {double value;uint64_t at_ms;uint8_t mid;bool valid;}tbx_volvo_value_t;
typedef struct {
    tbx_volvo_value_t speed,rpm,pedal,rate,idle,input_rpm,veb;
    char selected[3],attained[3];uint64_t selected_ms,attained_ms,gear_changed_ms;
    uint64_t condition_since_ms,last_ms,slip_since_ms;
    double iroll_seconds,iroll_distance_km,iroll_fuel_l,rpm_difference;
    bool iroll_fuel_gap,iroll_probable,rpm_difference_valid,slip_candidate,idle_reference_estimated;
    uint32_t iroll_episodes;
}tbx_volvo_operating_t;
void tbx_volvo_operating_observe(tbx_volvo_operating_t *,const tbx_j1587_observation_t *);
void tbx_volvo_operating_tick(tbx_volvo_operating_t *,uint64_t now_ms);
