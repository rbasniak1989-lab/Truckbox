#include "tbx_trip.h"
#include <math.h>
#include <string.h>
static bool elapsed_within(uint64_t now, uint64_t before, uint64_t limit)
{
    return now >= before && now-before <= limit;
}
void tbx_trip_init(tbx_trip_t *t, double restored_km, bool restored_valid)
{
    if (!t) return;
    memset(t,0,sizeof(*t));
    if (restored_valid && isfinite(restored_km) && restored_km>=0) {
        t->total_km=restored_km;t->has_distance=true;t->quality="RECUPERADO_CHECKPOINT";
    } else t->quality="SEM_MARCO_INICIAL";
}
void tbx_trip_restore_counter(tbx_trip_t *t,uint8_t sa,uint32_t raw)
{
    if (!t || raw>UINT32_C(0xfaffffff)) return;
    t->restored_counter=true;t->counter_sa=sa;t->counter_raw=raw;
    /* No monotonic capture reference survives reboot. Intentionally not valid. */
    t->counter_valid=false;
}
void tbx_trip_reset(tbx_trip_t *t)
{
    if(!t)return;
    tbx_trip_init(t,0,false);
}
void tbx_trip_restore_odometer(tbx_trip_t *t,uint8_t mid,uint32_t raw,double anchor)
{
    if(!t || raw==UINT32_MAX || !isfinite(anchor) || anchor<0)return;
    t->odo_valid=true;t->odo_restored=true;t->odo_mid=mid;t->odo_raw=raw;t->odo_anchor_km=anchor;
}
tbx_trip_result_t tbx_trip_odometer(tbx_trip_t *t,uint8_t mid,uint32_t raw,uint64_t now)
{
    if(!t || raw==UINT32_MAX)return TBX_TRIP_INVALID;
    tbx_trip_result_t result=TBX_TRIP_BASELINE;
    if(t->odo_valid && mid!=t->odo_mid){
        if(!t->odo_restored && elapsed_within(now,t->odo_at_ms,TBX_TRIP_COUNTER_FRESH_MS))return TBX_TRIP_SOURCE_IGNORED;
        ++t->source_changes;++t->gaps;result=TBX_TRIP_SOURCE_REBASELINE;
    }else if(t->odo_valid){
        if(raw<t->odo_raw){++t->resets;result=TBX_TRIP_RESET_REBASELINE;}
        else{
            double delta=(raw-t->odo_raw)*0.1609344;
            /* Reboot loses elapsed monotonic time. Accept only a bounded
             * validated ECU delta; report that its interval is unknown. */
            double allowed=t->odo_restored?2500.0:
                (now>=t->odo_at_ms?250.0*(double)(now-t->odo_at_ms)/3600000.0+0.322:0);
            if(delta>allowed || !isfinite(t->odo_anchor_km+delta)){
                ++t->implausible;result=TBX_TRIP_RANGE_REBASELINE;
            }else{
                if(t->odo_restored || now-t->odo_at_ms>TBX_TRIP_COUNTER_FRESH_MS)++t->gaps;
                t->total_km=t->odo_anchor_km+delta;t->has_distance=true;
                t->quality=t->odo_restored?"DELTA_ODO_J1587_RECUPERADO_INTERVALO_DESCONHECIDO":"DELTA_ODO_J1587_VALIDADO";
                result=TBX_TRIP_COUNTER_ADDED;
            }
        }
    }
    t->odo_valid=true;t->odo_restored=false;t->odo_raw=raw;t->odo_mid=mid;
    t->odo_at_ms=now;t->odo_anchor_km=t->total_km;t->fallback_since_counter=false;
    return result;
}
static tbx_trip_result_t baseline(tbx_trip_t *t,uint8_t sa,uint32_t raw,
                                   uint64_t now,tbx_trip_result_t result)
{
    t->counter_valid=true;t->restored_counter=false;
    t->counter_sa=sa;t->counter_raw=raw;t->counter_at_ms=now;
    t->fallback_since_counter=false;
    return result;
}
tbx_trip_result_t tbx_trip_counter(tbx_trip_t *t,uint8_t sa,uint32_t raw,uint64_t now)
{
    if(!t || raw>UINT32_C(0xfaffffff))return TBX_TRIP_INVALID;
    if(!t->counter_valid) {
        tbx_trip_result_t reason=t->restored_counter?TBX_TRIP_BOOT_REBASELINE:TBX_TRIP_BASELINE;
        if(t->restored_counter)++t->gaps;
        return baseline(t,sa,raw,now,reason);
    }
    const bool fresh=elapsed_within(now,t->counter_at_ms,TBX_TRIP_COUNTER_FRESH_MS);
    if(sa!=t->counter_sa) {
        /* Preserve the selected ECU while it is fresh. Other ECUs remain in raw
         * logs and sensor API, without alternating their counters in one trip. */
        if(fresh)return TBX_TRIP_SOURCE_IGNORED;
        ++t->source_changes;++t->gaps;
        return baseline(t,sa,raw,now,TBX_TRIP_SOURCE_REBASELINE);
    }
    if(!fresh || t->fallback_since_counter) {
        ++t->gaps;
        return baseline(t,sa,raw,now,TBX_TRIP_GAP_REBASELINE);
    }
    if(raw<t->counter_raw) {
        ++t->resets;
        return baseline(t,sa,raw,now,TBX_TRIP_RESET_REBASELINE);
    }
    double delta=(raw-t->counter_raw)*0.005;
    /* 250 km/h upper bound plus 10 m quantization tolerance. The allowance is
     * per observation; it permits 5 m counter steps at low update intervals. */
    double allowed=250.0*(double)(now-t->counter_at_ms)/3600000.0+0.010;
    if(delta>allowed || !isfinite(t->total_km+delta)) {
        ++t->implausible;
        return baseline(t,sa,raw,now,TBX_TRIP_RANGE_REBASELINE);
    }
    t->total_km+=delta;t->has_distance=true;t->quality="DELTA_CONTADOR_J1939";
    baseline(t,sa,raw,now,TBX_TRIP_COUNTER_ADDED);
    return TBX_TRIP_COUNTER_ADDED;
}
tbx_trip_result_t tbx_trip_speed(tbx_trip_t *t,tbx_trip_network_t network,
                                 uint8_t address,double kmh,uint64_t now)
{
    if(!t || !isfinite(kmh) || kmh<0 || kmh>250 ||
       (network!=TBX_TRIP_J1939 && network!=TBX_TRIP_J1587))return TBX_TRIP_INVALID;
    if(network==TBX_TRIP_J1587 && t->can_speed_valid &&
       elapsed_within(now,t->can_speed_at_ms,TBX_TRIP_CAN_PREFERENCE_MS))return TBX_TRIP_SOURCE_IGNORED;
    if(network==TBX_TRIP_J1939) {t->can_speed_valid=true;t->can_speed_at_ms=now;}
    bool same=t->speed_valid && t->speed_network==network && t->speed_address==address;
    if(t->speed_valid && !same && t->speed_network==network &&
       elapsed_within(now,t->speed_at_ms,TBX_TRIP_SPEED_WINDOW_MS))return TBX_TRIP_SOURCE_IGNORED;
    const bool interval_ok=same && now>t->speed_at_ms &&
        elapsed_within(now,t->speed_at_ms,TBX_TRIP_SPEED_WINDOW_MS);
    const bool counter_fresh=t->counter_valid &&
        elapsed_within(now,t->counter_at_ms,TBX_TRIP_COUNTER_FRESH_MS);
    const bool odometer_fresh=t->odo_valid && !t->odo_restored &&
        elapsed_within(now,t->odo_at_ms,TBX_TRIP_COUNTER_FRESH_MS);
    tbx_trip_result_t result=TBX_TRIP_BASELINE;
    if(counter_fresh || odometer_fresh)result=TBX_TRIP_COUNTER_FRESH;
    else if(interval_ok) {
        double delta=(t->speed_kmh+kmh)*0.5*(double)(now-t->speed_at_ms)/3600000.0;
        if(!isfinite(t->total_km+delta))return TBX_TRIP_INVALID;
        t->total_km+=delta;t->has_distance=true;t->fallback_since_counter=true;
        t->quality=network==TBX_TRIP_J1939?"ESTIMADO_VELOCIDADE_J1939":"ESTIMADO_VELOCIDADE_J1587";
        result=TBX_TRIP_ESTIMATE_ADDED;
    } else if(t->speed_valid) {
        result=same?TBX_TRIP_GAP_REBASELINE:TBX_TRIP_SOURCE_REBASELINE;
    }
    t->speed_valid=true;t->speed_network=network;t->speed_address=address;
    t->speed_kmh=kmh;t->speed_at_ms=now;
    return result;
}
const char *tbx_trip_result_name(tbx_trip_result_t r)
{
    static const char *const names[]={"INVALIDO","MARCO_INICIAL","DELTA_CONTADOR","DELTA_ESTIMADO",
        "CONTADOR_PRINCIPAL_RECENTE","ORIGEM_SECUNDARIA_IGNORADA","LACUNA_NOVO_MARCO",
        "RESET_NOVO_MARCO","FORA_FAIXA_NOVO_MARCO","ORIGEM_NOVO_MARCO","REBOOT_NOVO_MARCO"};
    return (unsigned)r<sizeof(names)/sizeof(names[0])?names[r]:"INVALIDO";
}
