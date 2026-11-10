#pragma once
#include "store_internal.h"
#include "tbx_trip.h"
#include "tbx_fuel_account.h"
#include <math.h>
#include <stddef.h>
/* Fixed scalar checkpoint, v3. Zero padding before CRC. A/B slots permit
 * falling back to the previous valid commit without reusing mutable pointers. */
typedef struct {
    uint32_t version,generation,legacy_before_crc,legacy_after_crc;
    double distance,fuel;
    uint8_t distance_valid,fuel_valid,counter_valid,counter_sa;
    uint32_t counter_raw;
    uint8_t odo_valid,odo_mid,direct_valid,direct_mid;
    uint32_t odo_raw,direct_raw;
    double odo_anchor_km,integrated_l,estimated_l,direct_l,direct_anchor_l,estimated_at_direct;
    double last_l_per_km,distance_anchor_km;
    uint32_t gaps,resets,range_errors;
    uint8_t distance_anchor_valid,unknown_gap,reserved[6];
    uint32_t crc;
} tb_trip_checkpoint_t;
static inline void tb_trip_checkpoint_make(tb_trip_checkpoint_t *b,uint32_t gen,const tbx_trip_t *t,const tbx_fuel_account_t *a)
{
    memset(b,0,sizeof(*b));b->version=3;b->generation=gen;
    b->distance=t->total_km;b->distance_valid=t->has_distance;
    b->fuel=a->observed_l;b->fuel_valid=a->valid;
    b->counter_valid=t->counter_valid;b->counter_sa=t->counter_sa;b->counter_raw=t->counter_raw;
    b->odo_valid=t->odo_valid;b->odo_mid=t->odo_mid;b->odo_raw=t->odo_raw;b->odo_anchor_km=t->odo_anchor_km;
    b->direct_valid=a->direct_valid;b->direct_mid=a->direct_mid;b->direct_raw=a->direct_raw;
    b->integrated_l=a->integrated_l;b->estimated_l=a->estimated_l;b->direct_l=a->direct_l;
    b->direct_anchor_l=a->direct_anchor_l;b->estimated_at_direct=a->estimated_at_direct;
    b->last_l_per_km=a->last_l_per_km;b->distance_anchor_km=a->distance_km;
    b->distance_anchor_valid=a->distance_valid;b->unknown_gap=a->unknown_gap;
    b->gaps=a->gaps;b->resets=a->resets;b->range_errors=a->range_errors;
    b->crc=tb_store_crc32(b,offsetof(tb_trip_checkpoint_t,crc));
}
static inline bool tb_trip_checkpoint_valid(const tb_trip_checkpoint_t *b)
{
    return b->version==3 && b->crc==tb_store_crc32(b,offsetof(tb_trip_checkpoint_t,crc)) &&
        isfinite(b->distance) && b->distance>=0 && isfinite(b->fuel) && b->fuel>=0 &&
        isfinite(b->odo_anchor_km) && b->odo_anchor_km>=0 &&
        isfinite(b->integrated_l) && b->integrated_l>=0 && isfinite(b->estimated_l) && b->estimated_l>=0 &&
        isfinite(b->direct_l) && b->direct_l>=0 && isfinite(b->direct_anchor_l) && b->direct_anchor_l>=0 &&
        isfinite(b->estimated_at_direct) && b->estimated_at_direct>=0 &&
        isfinite(b->last_l_per_km) && b->last_l_per_km>=0 &&
        isfinite(b->distance_anchor_km) && b->distance_anchor_km>=0;
}
static inline void tb_trip_checkpoint_restore(const tb_trip_checkpoint_t *b,tbx_trip_t *t,tbx_fuel_account_t *a)
{
    tbx_trip_init(t,b->distance,b->distance_valid);
    if(b->counter_valid)tbx_trip_restore_counter(t,b->counter_sa,b->counter_raw);
    if(b->odo_valid)tbx_trip_restore_odometer(t,b->odo_mid,b->odo_raw,b->odo_anchor_km);
    tbx_fuel_account_init(a,b->fuel,b->fuel_valid);
    a->integrated_l=b->integrated_l;a->estimated_l=b->estimated_l;a->direct_l=b->direct_l;
    a->direct_valid=b->direct_valid;a->direct_mid=b->direct_mid;a->direct_raw=b->direct_raw;
    a->direct_anchor_l=b->direct_anchor_l;a->estimated_at_direct=b->estimated_at_direct;
    a->last_l_per_km=b->last_l_per_km;a->distance_km=b->distance_anchor_km;a->distance_valid=b->distance_anchor_valid;
    a->gaps=b->gaps;a->resets=b->resets;a->range_errors=b->range_errors;
    a->quality=b->direct_valid?"PID250_RECUPERADO_AGUARDANDO_LEITURA":"INTEGRACAO_RECUPERADA_AGUARDANDO_LEITURA";
}
