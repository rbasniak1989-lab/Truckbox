#include "tbx_fuel_account.h"
#include <math.h>
#include <string.h>
#include <stddef.h>
#define QUANTUM_L 0.473176473
#define MAX_LPH 1000.0 /* Existing conservative input/rate envelope, not engine calibration. */
#define FUEL_SCALAR_BYTES (offsetof(tbx_fuel_persist_t,operation_end_interval_id)+sizeof(uint32_t))
_Static_assert(FUEL_SCALAR_BYTES<=offsetof(tbx_fuel_account_t,quality),"scalar checkpoint excludes pointer on RV32 and host");
_Static_assert(offsetof(tbx_fuel_persist_t,operation_end_interval_id)==offsetof(tbx_fuel_account_t,operation_end_interval_id),"scalar field layout");
const char *tbx_fuel_result_name(tbx_fuel_result_t r)
{
    static const char *const n[]={"NONE","ANCHOR","RECONCILED","DUPLICATE","WRONG_MID","TIME_REFERENCE_REQUIRED","COUNTER_DECREASE","IMPLAUSIBLE_DELTA","INVALID"};
    return (unsigned)r<sizeof(n)/sizeof(*n)?n[r]:"INVALID";
}
static void quality(tbx_fuel_account_t *a)
{
    a->unknown_gap=a->historical_unknown || a->interval_unknown;
    a->quality=a->unknown_gap?"CONSUMO_PARCIAL_LACUNA_DESCONHECIDA":
        !a->direct_valid?"INTEGRADO_PID183_EM_VALIDACAO":
        tbx_fuel_pending(a)+tbx_fuel_unanchored(a)>1e-9?"PID250_COM_ESTIMATIVA_PENDENTE":"INTERVALO_CONFIRMADO_PID250_EM_VALIDACAO";
}
double tbx_fuel_pending(const tbx_fuel_account_t *a)
{return a->direct_valid?fmax(0,a->observed_l-a->direct_anchor_l):a->observed_l;}
double tbx_fuel_unanchored(const tbx_fuel_account_t *a)
{return a->direct_valid?fmax(0,a->direct_anchor_l-a->direct_l):0;}
double tbx_fuel_operation_litres(const tbx_fuel_account_t *a)
{return fmax(0,(a->operation_open?a->observed_l:a->operation_end_l+a->operation_end_adjustment_l)-a->operation_start_l-a->operation_adjustment_l);}
void tbx_fuel_account_init(tbx_fuel_account_t *a,double litres,bool valid)
{
    memset(a,0,sizeof(*a));
    if(valid && isfinite(litres) && litres>=0){a->observed_l=litres;a->valid=true;}
    a->restored=valid;a->historical_unknown=valid;a->unknown_gap=valid;a->reboot_gap_pending=valid;
    a->quality=valid?"RECUPERADO_ORIGEM_LEGADA":"SEM_LEITURA";
}
void tbx_fuel_account_rate_at(tbx_fuel_account_t *a,uint8_t mid,double lph,double kmh,tbx_fuel_stamp_t t)
{
    if(!a || !isfinite(lph) || lph<0 || lph>MAX_LPH)return;
    if(a->rate_at_ms && t.boot==a->rate_boot && t.uptime_ms<=a->rate_at_ms){
        /* A rate change at the exact cut instant changes the next interval,
         * never integrates a second copy of the preceding sample. */
        if(a->rate_valid && t.uptime_ms==a->rate_at_ms && mid==a->rate_mid)a->last_rate_lph=lph;
        return;
    }
    if(a->rate_valid && mid!=a->rate_mid && t.boot==a->rate_boot && t.uptime_ms-a->rate_at_ms<=3000)return;
    bool same=a->rate_valid && mid==a->rate_mid && t.boot==a->rate_boot && t.uptime_ms>a->rate_at_ms;
    uint64_t dt=same?t.uptime_ms-a->rate_at_ms:0;
    if(same && dt<=3000){
        double inc=(a->last_rate_lph+lph)*0.5*(double)dt/3600000.0;
        a->integrated_l+=inc;a->observed_l+=inc;a->valid=true;
    }else if(a->rate_valid || a->restored){
        ++a->gaps;
        if(same && dt<=300000){
            /* Bounded interpolation is explicitly estimated and provisional. */
            double inc=(a->last_rate_lph+lph)*0.5*(double)dt/3600000.0;
            a->estimated_l+=inc;a->observed_l+=inc;a->valid=true;
        }else a->interval_unknown=true;
    }
    a->rate_valid=true;a->rate_mid=mid;a->rate_at_ms=t.uptime_ms;a->rate_boot=t.boot;a->last_rate_lph=lph;
    a->last_speed_kmh=kmh;
    if(isfinite(kmh) && kmh>=5 && kmh<=250 && lph>0)a->last_l_per_km=lph/kmh;
    a->restored=false;quality(a);
}
void tbx_fuel_account_rate(tbx_fuel_account_t *a,uint8_t mid,double lph,double kmh,uint64_t now)
{tbx_fuel_account_rate_at(a,mid,lph,kmh,(tbx_fuel_stamp_t){.uptime_ms=now});}
void tbx_fuel_account_distance(tbx_fuel_account_t *a,double km)
{
    if(!a || !isfinite(km) || km<0)return;
    if(a->distance_valid && km>=a->distance_km && a->reboot_gap_pending && a->last_l_per_km>0 && a->last_l_per_km<=5){
        double delta=km-a->distance_km;
        if(delta<=2500 && delta>0){double inc=delta*a->last_l_per_km;a->estimated_l+=inc;a->observed_l+=inc;}
    }
    a->distance_km=km;a->distance_valid=true;a->reboot_gap_pending=false;quality(a);
}
static bool elapsed(tbx_fuel_stamp_t x,tbx_fuel_stamp_t y,uint64_t *dt)
{
    if(x.boot==y.boot && y.uptime_ms>x.uptime_ms){*dt=y.uptime_ms-x.uptime_ms;return true;}
    if(x.boot!=y.boot && x.unix_ms && y.unix_ms>x.unix_ms){*dt=y.unix_ms-x.unix_ms;return true;}
    return false;
}
/* Cut at the anchor using only a recent rate, then move its integration cursor.
 * This prevents integrating across both sides of an already reconciled anchor. */
static void cut(tbx_fuel_account_t *a,tbx_fuel_stamp_t t)
{
    if(!a->rate_valid || a->rate_boot!=t.boot || t.uptime_ms<a->rate_at_ms)return;
    uint64_t dt=t.uptime_ms-a->rate_at_ms;
    if(dt<=3000){double inc=a->last_rate_lph*(double)dt/3600000.0;a->integrated_l+=inc;a->observed_l+=inc;a->valid=true;}
    else {a->interval_unknown=true;a->rate_valid=false;}
    a->rate_at_ms=t.uptime_ms;
}
bool tbx_fuel_account_counter_litres(tbx_fuel_account_t *a,uint8_t mid,double litres,tbx_fuel_stamp_t t,double odo)
{
    if(!a || !isfinite(litres) || litres<0){if(a)a->result=TBX_FUEL_INVALID;return false;}
    if(a->direct_valid && mid!=a->direct_mid){a->result=TBX_FUEL_WRONG_MID;return false;}
    uint64_t dt=0;
    if(a->direct_valid){
        if(t.boot==a->anchor.boot && t.uptime_ms==a->anchor.uptime_ms && litres==a->counter_l){a->result=TBX_FUEL_DUPLICATE;return false;}
        if(!elapsed(a->anchor,t,&dt)){a->result=TBX_FUEL_TIME_INVALID;return false;}
        if(litres<a->counter_l){++a->resets;a->result=TBX_FUEL_COUNTER_DECREASE;return false;}
        double delta=litres-a->counter_l;
        if(delta>MAX_LPH*(double)dt/3600000.0+2*QUANTUM_L || !isfinite(a->direct_anchor_l+delta)){
            ++a->range_errors;a->result=TBX_FUEL_COUNTER_JUMP;return false;
        }
        cut(a,t);
        double pending=tbx_fuel_pending(a),correction=delta-pending;
        a->interval_start=a->anchor;a->interval_end=t;
        a->last_delta_l=delta;a->last_integrated_l=a->integrated_l-a->integrated_at_direct;
        a->last_estimated_l=a->estimated_l-a->estimated_at_direct;
        a->last_correction_l=correction;a->last_distance_km=isfinite(odo) && odo>=a->anchor_odometer_km?odo-a->anchor_odometer_km:NAN;
        a->boundary_correction_l=0;
        if(a->operation_interval_id==a->interval_id && a->operation_stamp.uptime_ms){
            /* A boundary within this interval is an allocation estimate. Never
             * call an internal subsegment an independent ECU measurement. */
            double fraction=0;uint64_t before=0;
            if(pending>1e-9)fraction=a->operation_start_pending_l/pending;
            else if(elapsed(a->anchor,a->operation_stamp,&before))fraction=(double)before/(double)dt;
            fraction=fmin(1,fmax(0,fraction));
            a->boundary_correction_l=correction*fraction;
            a->operation_adjustment_l+=a->boundary_correction_l;a->operation_estimated=true;
            a->interval_crosses_operation=true;
        }else a->interval_crosses_operation=false;
        if(!a->operation_open && a->operation_end_stamp.uptime_ms && a->operation_end_interval_id==a->interval_id){
            double fraction=0;uint64_t before=0;
            if(pending>1e-9)fraction=a->operation_end_pending_l/pending;
            else if(elapsed(a->anchor,a->operation_end_stamp,&before))fraction=(double)before/(double)dt;
            a->operation_end_adjustment_l+=correction*fmin(1,fmax(0,fraction));
            a->operation_estimated=true;a->interval_crosses_operation=true;
        }
        a->observed_l=a->direct_anchor_l+delta;a->direct_l+=delta;a->valid=true;
        a->estimated_l=a->estimated_at_direct; /* ECU supersedes this interval's estimates. */
        a->interval_unknown=false;a->reboot_gap_pending=false;
        a->result=TBX_FUEL_RECONCILED;
        ++a->interval_id;
    }else{
        cut(a,t);a->historical_unknown=a->historical_unknown || a->interval_unknown;
        a->interval_unknown=false;a->interval_id=1;a->result=TBX_FUEL_ANCHORED;
    }
    a->direct_valid=true;a->direct_mid=mid;a->counter_l=litres;
    a->anchor=t;a->direct_at_ms=t.uptime_ms;a->direct_anchor_l=a->observed_l;
    a->estimated_at_direct=a->estimated_l;a->integrated_at_direct=a->integrated_l;
    a->anchor_odometer_km=odo;quality(a);
    return a->result==TBX_FUEL_RECONCILED;
}
bool tbx_fuel_account_counter_at(tbx_fuel_account_t *a,uint8_t mid,uint32_t raw,tbx_fuel_stamp_t t,double odo)
{
    if(!a || raw==UINT32_MAX){if(a)a->result=TBX_FUEL_INVALID;return false;}
    uint32_t before=a->direct_raw;
    bool ok=tbx_fuel_account_counter_litres(a,mid,raw*QUANTUM_L,t,odo);
    if(a->result==TBX_FUEL_ANCHORED || ok){a->interval_start_raw=before;a->direct_raw=raw;}
    return ok;
}
bool tbx_fuel_account_counter(tbx_fuel_account_t *a,uint8_t mid,uint32_t raw,uint64_t now)
{return tbx_fuel_account_counter_at(a,mid,raw,(tbx_fuel_stamp_t){.uptime_ms=now},NAN);}
bool tbx_fuel_account_boundary(tbx_fuel_account_t *a,bool start,const char *uid,tbx_fuel_stamp_t t,double odo)
{
    if(!a || !uid || !*uid || strlen(uid)>64)return false;
    if(!start && strcmp(uid,a->operation_uid))return false;
    if(start && a->operation_open && !strcmp(uid,a->operation_uid))return true;
    if(start && !a->operation_open && a->operation_end_stamp.uptime_ms && !strcmp(uid,a->operation_uid))return false;
    cut(a,t);
    if(start){
        memcpy(a->operation_uid,uid,strlen(uid)+1);a->operation_start_l=a->observed_l;
        a->operation_adjustment_l=0;a->operation_start_pending_l=tbx_fuel_pending(a);
        a->operation_stamp=t;a->operation_interval_id=a->interval_id;a->operation_odometer_km=odo;
        a->operation_start_reference=(tbx_fuel_reference_t){.at=a->anchor,.litres=a->counter_l,.raw=a->direct_raw,.mid=a->direct_mid,.valid=a->direct_valid};
        a->operation_estimated=true;
        memset(&a->operation_end_stamp,0,sizeof(a->operation_end_stamp));a->operation_end_adjustment_l=0;
    }else{
        if(!a->operation_open)return true;
        a->operation_end_stamp=t;a->operation_end_l=a->observed_l;
        a->operation_end_pending_l=tbx_fuel_pending(a);a->operation_end_interval_id=a->interval_id;
        a->operation_end_odometer_km=odo;
        a->operation_end_reference=(tbx_fuel_reference_t){.at=a->anchor,.litres=a->counter_l,.raw=a->direct_raw,.mid=a->direct_mid,.valid=a->direct_valid};
    }
    a->operation_open=start;return true;
}
void tbx_fuel_account_export(const tbx_fuel_account_t *a,tbx_fuel_persist_t *p)
{memset(p,0,sizeof(*p));memcpy(p,a,FUEL_SCALAR_BYTES);}
bool tbx_fuel_account_restore(tbx_fuel_account_t *a,const tbx_fuel_persist_t *p)
{
    if(!p || !isfinite(p->observed_l) || p->observed_l<0 || !isfinite(p->direct_anchor_l) || p->direct_anchor_l<0 ||
       !isfinite(p->integrated_l) || p->integrated_l<0 || !isfinite(p->estimated_l) || p->estimated_l<0 ||
       !isfinite(p->direct_l) || p->direct_l<0 || !isfinite(p->counter_l) || p->counter_l<0 ||
       !memchr(p->operation_uid,0,sizeof(p->operation_uid)) || p->result>TBX_FUEL_INVALID)return false;
    memset(a,0,sizeof(*a));memcpy(a,p,FUEL_SCALAR_BYTES);
    a->rate_valid=false;a->restored=true;a->interval_unknown=true;a->reboot_gap_pending=true;quality(a);return true;
}
