#include "tbx_volvo_operating.h"
#include <math.h>
#include <string.h>
static bool fresh(const tbx_volvo_value_t *v,uint64_t now,uint64_t age)
{return v->valid && v->at_ms && now>=v->at_ms && now-v->at_ms<=age;}
void tbx_volvo_operating_observe(tbx_volvo_operating_t *s,const tbx_j1587_observation_t *o)
{
    uint64_t now=o->uptime_ms;tbx_volvo_value_t *v=NULL;
    switch(o->pid){case 84:v=&s->speed;break;case 91:v=&s->pedal;break;case 190:v=&s->rpm;break;
    case 183:v=&s->rate;break;case 188:v=&s->idle;break;case 161:v=&s->input_rpm;break;case 122:v=&s->veb;break;default:break;}
    if(v){
        uint8_t primary=o->pid==161?130:128;
        if(!(v->mid==primary && o->mid!=primary && fresh(v,now,5000))){
            v->valid=o->quality==TBX_J1587_OBS_EM_VALIDACAO && o->has_numeric && isfinite(o->value);
            v->at_ms=now;v->mid=o->mid;v->value=v->valid?o->value:0;
        }
    }
    if((o->pid==162 || o->pid==163) && o->mid==130){
        char *text=o->pid==162?s->selected:s->attained;
        if(o->pid==163 && strcmp(text,o->passive.text))s->gear_changed_ms=now;
        if(o->quality==TBX_J1587_OBS_EM_VALIDACAO && o->passive.interpreted)memcpy(text,o->passive.text,3);else memset(text,0,3);
        if(o->pid==162)s->selected_ms=now;else s->attained_ms=now;
    }
    tbx_volvo_operating_tick(s,now);
}
void tbx_volvo_operating_tick(tbx_volvo_operating_t *s,uint64_t now)
{
    bool usable=fresh(&s->speed,now,3000) && fresh(&s->rpm,now,3000) && fresh(&s->pedal,now,3000);
    bool ranges=s->selected_ms && s->attained_ms && now>=s->selected_ms && now>=s->attained_ms && now-s->selected_ms<=3000 && now-s->attained_ms<=3000;
    s->idle_reference_estimated=!fresh(&s->idle,now,1800000);
    bool idle=s->idle_reference_estimated?(s->rpm.value>=500 && s->rpm.value<=900):fabs(s->rpm.value-s->idle.value)<=150;
    bool automatic=!strcmp(s->selected,"A"),neutral=!strcmp(s->attained,"N") || !strcmp(s->attained,"0");
    bool condition=usable && ranges && automatic && neutral && idle && s->speed.value>=5 && s->pedal.value<=1;
    bool previous=s->iroll_probable;
    if(condition){if(!s->condition_since_ms)s->condition_since_ms=now;
        s->iroll_probable=now>=s->condition_since_ms && now-s->condition_since_ms>=3000;
    }else{s->condition_since_ms=0;s->iroll_probable=false;}
    if(!previous && s->iroll_probable)++s->iroll_episodes;
    if(previous && s->iroll_probable && now>=s->last_ms && now-s->last_ms<=3000){
        double seconds=(double)(now-s->last_ms)/1000;s->iroll_seconds+=seconds;
        s->iroll_distance_km+=s->speed.value*seconds/3600;
        if(fresh(&s->rate,now,3000))s->iroll_fuel_l+=s->rate.value*seconds/3600;else s->iroll_fuel_gap=true;
    }
    uint64_t skew=s->rpm.at_ms>s->input_rpm.at_ms?s->rpm.at_ms-s->input_rpm.at_ms:s->input_rpm.at_ms-s->rpm.at_ms;
    s->rpm_difference_valid=fresh(&s->rpm,now,3000) && fresh(&s->input_rpm,now,3000) && skew<=1000;
    if(s->rpm_difference_valid)s->rpm_difference=s->rpm.value-s->input_rpm.value;
    bool gear=ranges && *s->attained && !neutral && now>=s->gear_changed_ms && now-s->gear_changed_ms>=2000;
    bool difference=usable && gear && s->rpm_difference_valid && s->speed.value>=5 && s->pedal.value>=5 && s->rpm.value>=600 && fabs(s->rpm_difference)>fmax(200,s->rpm.value*0.2);
    if(difference){if(!s->slip_since_ms)s->slip_since_ms=now;s->slip_candidate=now>=s->slip_since_ms && now-s->slip_since_ms>=3000;}
    else{s->slip_since_ms=0;s->slip_candidate=false;}
    s->last_ms=now;
}
