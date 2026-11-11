#include "tbx_transmission.h"
#include <math.h>
#include <stddef.h>
static bool fresh(const tbx_tr_value_t *v,uint64_t now)
{return v->valid && now>=v->at_us && now-v->at_us<=UINT64_C(5000000);}
void tbx_transmission_observe(tbx_transmission_t *t,unsigned signal,double value,uint64_t now)
{
    if(signal>=TBX_TR_INPUTS || !isfinite(value))return;
    tbx_tr_value_t *v=&t->input[signal];if(v->at_us && now<=v->at_us)return;
    double delta=value-v->value,threshold=signal==TBX_TR_RPM?100:signal==TBX_TR_SPEED?0.3:1;
    if(t->launch && fresh(v,now) && fabs(delta)>=threshold){
        int8_t direction=delta>0?1:-1;int8_t *old=NULL;uint8_t *turns=NULL;
        if(signal==TBX_TR_RPM){old=&t->rpm_direction;turns=&t->rpm_turns;}
        if(signal==TBX_TR_CLUTCH){old=&t->actuator_direction;turns=&t->actuator_turns;}
        if(signal==TBX_TR_SPEED){old=&t->speed_direction;turns=&t->speed_turns;}
        if(old && turns){if(*old && *old!=direction && *turns<255)++*turns;*old=direction;}
    }
    *v=(tbx_tr_value_t){value,now,true};
}
void tbx_transmission_tick(tbx_transmission_t *t,uint64_t now)
{
    const tbx_tr_value_t *s=&t->input[TBX_TR_SPEED],*r=&t->input[TBX_TR_RPM],*p=&t->input[TBX_TR_PEDAL],*g=&t->input[TBX_TR_GEAR],*selected=&t->input[TBX_TR_SELECTED];
    bool usable=fresh(s,now) && fresh(r,now);
    if(!usable){t->launch=false;t->intent=false;t->reason="SPEED_OR_RPM_STALE";return;}
    bool stopped=s->value<=1;
    if(stopped)t->last_stop_us=now;
    bool intent=fresh(p,now) && p->value>=5 && r->value>=600 && s->value<=3;
    bool departing=t->last_stop_us && now-t->last_stop_us<UINT64_C(10000000) && s->value>1 && s->value<10;
    if(!t->launch && now>=t->ended_us && (!t->ended_us || now-t->ended_us>=UINT64_C(10000000)) && ((intent && !t->intent) || departing)){
        t->launch=true;t->started_us=t->turn_window_us=now;++t->launches;t->candidate_flags=0;
        t->launch_rpm=r->value;t->launch_speed=s->value;t->launch_gear=fresh(g,now)?g->value:0;
        t->rpm_turns=t->actuator_turns=t->speed_turns=0;
        t->rpm_direction=t->actuator_direction=t->speed_direction=0;t->reason=intent?"STATIONARY_LAUNCH_INTENT":"LOW_SPEED_DEPARTURE_OR_MANEUVER";
    }
    t->intent=intent;
    if(!t->launch)return;
    if(now<t->started_us || now-t->started_us>=UINT64_C(30000000) || s->value>=15){t->launch=false;t->ended_us=now;t->reason=s->value>=15?"CRUISE_SPEED":"LAUNCH_WINDOW_COMPLETE";return;}
    if(now-t->turn_window_us>=UINT64_C(5000000)){t->turn_window_us=now;t->rpm_turns=t->actuator_turns=t->speed_turns=0;}
    bool pedal=fresh(p,now) && p->value>=10;
    bool gear=fresh(g,now) && fresh(selected,now) && g->value!=0 && g->value==selected->value && g->value==t->launch_gear;
    if(pedal && gear && now-t->started_us>=UINT64_C(5000000) && s->value<1 && r->value>800)t->candidate_flags|=TBX_TR_COUPLING_DELAY;
    if(t->actuator_turns>=4)t->candidate_flags|=TBX_TR_ACTUATOR_OSCILLATION;
    if(pedal && t->rpm_turns>=4 && t->speed_turns>=2)t->candidate_flags|=TBX_TR_JUDDER_CANDIDATE;
    if(pedal && gear && fresh(&t->input[TBX_TR_CLUTCH],now) && now-t->started_us>=UINT64_C(5000000) && r->value-t->launch_rpm>300 && s->value-t->launch_speed<0.5)t->candidate_flags|=TBX_TR_POSSIBLE_SLIP_CANDIDATE;
    /* All flags are candidates. PID160 shaft semantics/ratio are not proven;
     * no branch confirms real clutch slip or commands vehicle actuators. */
}
