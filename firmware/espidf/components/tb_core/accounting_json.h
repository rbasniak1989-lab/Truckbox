#pragma once
/* cJSON is supplied by the caller in firmware and host integration tests. */
#include "tbx_fuel_account.h"
#include <math.h>
#include "../tb_base/include/tb_json.h"
static inline void tb_accounting_json_compact(cJSON *parent,const tbx_fuel_account_t *a)
{
    cJSON *o=cJSON_AddObjectToObject(parent,"trip_accounting");
    if(!o)return;
    /* Compact diagnostics use millilitre precision; canonical trip_fuel_l and
     * raw ECU counters remain at full precision. No integration is rounded. */
    cJSON_AddNumberToObject(o,"integrated_l",round(a->integrated_l*1000)/1000);
    cJSON_AddNumberToObject(o,"estimated_l",round(a->estimated_l*1000)/1000);
    if(a->direct_valid)cJSON_AddNumberToObject(o,"direct_l",round(a->direct_l*1000)/1000);else cJSON_AddNullToObject(o,"direct_l");
    cJSON_AddNumberToObject(o,"gap_count",a->gaps);
    cJSON_AddBoolToObject(o,"unknown_gap",a->unknown_gap);
    cJSON_AddStringToObject(o,"quality",a->unknown_gap?"PARCIAL":!a->direct_valid?"PID183_PROVISORIO":tbx_fuel_pending(a)+tbx_fuel_unanchored(a)>1e-9?"ECU_E_PROVISORIO":"INTERVALO_ECU");
    cJSON_AddNumberToObject(o,"v",2);
    cJSON_AddNumberToObject(o,"pending_l",round(tbx_fuel_pending(a)*1000)/1000);
    cJSON_AddNumberToObject(o,"interval_id",a->interval_id);
    if(tbx_fuel_unanchored(a)>1e-9)cJSON_AddNumberToObject(o,"unanchored_l",round(tbx_fuel_unanchored(a)*1000)/1000);
    if(a->operation_uid[0] && !a->operation_reference_delayed){
        cJSON *op=cJSON_CreateArray();
        bool ok=op && tb_json_add_array(op,cJSON_CreateString(a->operation_uid)) &&
            tb_json_add_array(op,cJSON_CreateNumber(tbx_fuel_operation_litres(a))) &&
            tb_json_add_array(op,cJSON_CreateBool(a->operation_estimated));
        if(ok)tb_json_add_object(o,"op",op);else cJSON_Delete(op);
    }
}
static inline void tb_accounting_json(cJSON *parent,const tbx_fuel_account_t *a)
{
    tb_accounting_json_compact(parent,a);
    cJSON *o=cJSON_GetObjectItemCaseSensitive(parent,"trip_accounting");if(!o)return;
    cJSON_DeleteItemFromObjectCaseSensitive(o,"quality");
    cJSON_AddStringToObject(o,"quality",a->quality?a->quality:"SEM_LEITURA");
    cJSON_AddStringToObject(o,"counter_result",tbx_fuel_result_name(a->result));
    cJSON_AddNumberToObject(o,"canonical_l",a->observed_l);
    cJSON_AddNumberToObject(o,"last_correction_l",a->last_correction_l);
    cJSON_AddBoolToObject(o,"historical_unknown",a->historical_unknown);
    cJSON_AddStringToObject(o,"source_validation","EM_VALIDACAO");
    cJSON *r=cJSON_AddObjectToObject(o,"pid250");
    if(a->direct_valid){
        cJSON_AddNumberToObject(r,"mid",a->direct_mid);cJSON_AddNumberToObject(r,"raw",a->direct_raw);
        cJSON_AddNumberToObject(r,"litres",a->counter_l);cJSON_AddNumberToObject(r,"boot_id",a->anchor.boot);
        cJSON_AddNumberToObject(r,"uptime_ms",(double)a->anchor.uptime_ms);
        if(a->anchor.unix_ms)cJSON_AddNumberToObject(r,"unix_ms",(double)a->anchor.unix_ms);else cJSON_AddNullToObject(r,"unix_ms");
    }else cJSON_AddBoolToObject(r,"available",false);
    if(a->operation_uid[0]){
        cJSON *p=cJSON_AddObjectToObject(o,"operation");cJSON_AddStringToObject(p,"uid",a->operation_uid);
        cJSON_AddBoolToObject(p,"open",a->operation_open);cJSON_AddBoolToObject(p,"allocation_estimated",a->operation_estimated);
        cJSON_AddBoolToObject(p,"reference_delayed_or_unknown",a->operation_reference_delayed);
        cJSON_AddNumberToObject(p,"fuel_l",tbx_fuel_operation_litres(a));
        cJSON_AddNumberToObject(p,"boundary_adjustment_l",a->operation_adjustment_l);
    }
}
