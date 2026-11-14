#ifndef TBX_FUEL_ACCOUNT_H
#define TBX_FUEL_ACCOUNT_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {uint64_t uptime_ms,unix_ms;uint32_t boot;}tbx_fuel_stamp_t;
typedef struct {tbx_fuel_stamp_t at;double litres;uint32_t raw;uint8_t mid;bool valid;}tbx_fuel_reference_t;
typedef enum {
    TBX_FUEL_NONE,TBX_FUEL_ANCHORED,TBX_FUEL_RECONCILED,TBX_FUEL_DUPLICATE,
    TBX_FUEL_WRONG_MID,TBX_FUEL_TIME_INVALID,TBX_FUEL_COUNTER_DECREASE,
    TBX_FUEL_COUNTER_JUMP,TBX_FUEL_INVALID
}tbx_fuel_result_t;
/* Scalar-only state has a stable CRC checkpoint; no allocation or live pointer.
 * observed_l is the ONE canonical cumulative value. Components are diagnostics,
 * never independent amounts to add to it. */
#define TBX_FUEL_STATE_FIELDS \
    double observed_l,integrated_l,estimated_l,direct_l; \
    bool valid,rate_valid,direct_valid,restored,unknown_gap; \
    uint8_t rate_mid,direct_mid; \
    double last_rate_lph,last_speed_kmh,last_l_per_km; \
    uint64_t rate_at_ms,direct_at_ms; \
    uint32_t direct_raw,gaps,resets,range_errors; \
    double direct_anchor_l,estimated_at_direct; \
    bool distance_valid,reboot_gap_pending; \
    double distance_km; \
    tbx_fuel_stamp_t anchor,interval_start,interval_end; \
    uint32_t rate_boot,interval_id,interval_start_raw; \
    double counter_l,integrated_at_direct,anchor_odometer_km; \
    double last_delta_l,last_integrated_l,last_estimated_l,last_correction_l,last_distance_km; \
    bool historical_unknown,interval_unknown,interval_crosses_operation; \
    tbx_fuel_result_t result; \
    char operation_uid[65]; \
    bool operation_open,operation_estimated,operation_reference_delayed; \
    tbx_fuel_stamp_t operation_stamp; \
    double operation_start_l,operation_start_pending_l,operation_adjustment_l,operation_odometer_km; \
    uint32_t operation_interval_id; \
    double boundary_correction_l; \
    tbx_fuel_stamp_t operation_end_stamp; \
    double operation_end_l,operation_end_pending_l,operation_end_adjustment_l; \
    double operation_end_odometer_km; \
    tbx_fuel_reference_t operation_start_reference,operation_end_reference; \
    uint32_t operation_end_interval_id

typedef struct {TBX_FUEL_STATE_FIELDS;}tbx_fuel_persist_t;
typedef struct {TBX_FUEL_STATE_FIELDS;const char *quality;}tbx_fuel_account_t;
#undef TBX_FUEL_STATE_FIELDS
void tbx_fuel_account_init(tbx_fuel_account_t *,double restored_l,bool valid);
void tbx_fuel_account_rate(tbx_fuel_account_t *,uint8_t,double,double,uint64_t);
void tbx_fuel_account_rate_at(tbx_fuel_account_t *,uint8_t,double,double,tbx_fuel_stamp_t);
void tbx_fuel_account_distance(tbx_fuel_account_t *,double);
bool tbx_fuel_account_counter(tbx_fuel_account_t *,uint8_t,uint32_t,uint64_t);
bool tbx_fuel_account_counter_at(tbx_fuel_account_t *,uint8_t,uint32_t,tbx_fuel_stamp_t,double);
/* SI-domain reconciliation, also used by the wire decoder. Raw conversion is
 * independently tested at .473176473 L/tick, never fabricated litre frames. */
bool tbx_fuel_account_counter_litres(tbx_fuel_account_t *,uint8_t,double,tbx_fuel_stamp_t,double);
bool tbx_fuel_account_boundary(tbx_fuel_account_t *,bool,const char *,tbx_fuel_stamp_t,double);
void tbx_fuel_account_export(const tbx_fuel_account_t *,tbx_fuel_persist_t *);
bool tbx_fuel_account_restore(tbx_fuel_account_t *,const tbx_fuel_persist_t *);
const char *tbx_fuel_result_name(tbx_fuel_result_t);
double tbx_fuel_pending(const tbx_fuel_account_t *);
double tbx_fuel_unanchored(const tbx_fuel_account_t *);
double tbx_fuel_operation_litres(const tbx_fuel_account_t *);
#endif
