#include "tbx_fuel_account.h"
#include <math.h>
#include <string.h>
void tbx_fuel_account_init(tbx_fuel_account_t *a,double litres,bool valid)
{
    memset(a,0,sizeof(*a));
    if(valid && isfinite(litres) && litres>=0){a->observed_l=litres;a->valid=true;}
    a->restored=valid;a->unknown_gap=valid;a->reboot_gap_pending=valid;
    a->quality=valid?"RECUPERADO_ORIGEM_LEGADA":"SEM_LEITURA";
}
void tbx_fuel_account_rate(tbx_fuel_account_t *a,uint8_t mid,double lph,double kmh,uint64_t now)
{
    if(!a || !isfinite(lph) || lph<0 || lph>1000)return;
    if(a->rate_valid && mid!=a->rate_mid && now>=a->rate_at_ms && now-a->rate_at_ms<=3000)return;
    bool same=a->rate_valid && mid==a->rate_mid && now>a->rate_at_ms;
    if(same && now-a->rate_at_ms<=3000){
        double increment=(a->last_rate_lph+lph)*0.5*(double)(now-a->rate_at_ms)/3600000.0;
        a->integrated_l+=increment;a->observed_l+=increment;a->valid=true;
        a->quality=a->direct_valid?"MISTO_PID250_VALIDADO_E_INTEGRACAO":"INTEGRADO_VAZAO_J1587_EM_VALIDACAO";
    }else if(a->rate_valid || a->restored){
        ++a->gaps;a->unknown_gap=true;
        if(same && now-a->rate_at_ms<=300000){
            /* Bounded in-boot interpolation, never counted as observed. */
            a->estimated_l+=(a->last_rate_lph+lph)*0.5*(double)(now-a->rate_at_ms)/3600000.0;
            a->unknown_gap=false;
        }
    }
    a->rate_valid=true;a->rate_mid=mid;a->rate_at_ms=now;a->last_rate_lph=lph;
    a->last_speed_kmh=kmh;
    if(isfinite(kmh) && kmh>=5 && kmh<=250 && lph>0)a->last_l_per_km=lph/kmh;
    a->restored=false;
}
void tbx_fuel_account_distance(tbx_fuel_account_t *a,double km)
{
    if(!a || !isfinite(km) || km<0)return;
    if(a->distance_valid && km>=a->distance_km && a->reboot_gap_pending &&
       isfinite(a->last_l_per_km) && a->last_l_per_km>0 && a->last_l_per_km<=5){
        double delta=km-a->distance_km;
        if(delta<=2500 && delta>0){a->estimated_l+=delta*a->last_l_per_km;a->unknown_gap=false;}
    }
    a->distance_km=km;a->distance_valid=true;a->reboot_gap_pending=false;
}
bool tbx_fuel_account_counter(tbx_fuel_account_t *a,uint8_t mid,uint32_t raw,uint64_t now)
{
    if(!a || raw==UINT32_MAX)return false;
    bool reconciled=false;
    if(a->direct_valid && mid!=a->direct_mid){
        /* A different ECU never silently replaces the validated authority. */
        return false;
    }
    if(a->direct_valid){
        if(raw<a->direct_raw)++a->resets;
        else{
            double delta=(raw-a->direct_raw)*0.473176473;
            double allowed=a->direct_at_ms && now>=a->direct_at_ms?
                1000.0*(double)(now-a->direct_at_ms)/3600000.0+0.947:5000.0;
            if(delta<=allowed && isfinite(a->direct_anchor_l+delta)){
                a->observed_l=a->direct_anchor_l+delta;a->direct_l+=delta;a->valid=true;
                /* Counter delta supersedes integration and estimates only
                 * within this counter interval. Historical components remain
                 * diagnostics and must NOT be summed again. */
                a->estimated_l=a->estimated_at_direct;a->unknown_gap=false;a->reboot_gap_pending=false;
                a->quality="DELTA_PID250_VALIDADO";reconciled=true;
            }else ++a->range_errors;
        }
    }
    a->direct_valid=true;a->direct_mid=mid;a->direct_raw=raw;
    a->direct_at_ms=now;a->direct_anchor_l=a->observed_l;
    a->estimated_at_direct=a->estimated_l;
    return reconciled;
}
