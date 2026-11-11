#pragma once
#include <stdint.h>
#include <stdbool.h>
enum {TBX_TR_SPEED,TBX_TR_RPM,TBX_TR_PEDAL,TBX_TR_GEAR,TBX_TR_SELECTED,TBX_TR_CLUTCH,TBX_TR_SHAFT,TBX_TR_INPUTS};
enum {TBX_TR_COUPLING_DELAY=1,TBX_TR_ACTUATOR_OSCILLATION=2,TBX_TR_JUDDER_CANDIDATE=4,TBX_TR_POSSIBLE_SLIP_CANDIDATE=8};
typedef struct {double value;uint64_t at_us;bool valid;}tbx_tr_value_t;
typedef struct {
    tbx_tr_value_t input[TBX_TR_INPUTS];
    uint64_t started_us,ended_us,last_stop_us,turn_window_us;
    double launch_rpm,launch_speed,launch_gear;
    uint32_t launches,candidate_flags;
    uint8_t rpm_turns,actuator_turns,speed_turns;
    int8_t rpm_direction,actuator_direction,speed_direction;
    bool launch,intent;
    const char *reason;
}tbx_transmission_t;
void tbx_transmission_observe(tbx_transmission_t *,unsigned signal,double value,uint64_t at_us);
void tbx_transmission_tick(tbx_transmission_t *,uint64_t now_us);
