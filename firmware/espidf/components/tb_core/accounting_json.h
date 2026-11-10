#pragma once
/* cJSON is supplied by the caller in firmware and host integration tests. */
#include "tbx_fuel_account.h"
static inline void tb_accounting_json(cJSON *parent,const tbx_fuel_account_t *a)
{
    cJSON *o=cJSON_AddObjectToObject(parent,"trip_accounting");
    if(!o)return;
    cJSON_AddNumberToObject(o,"integrated_l",a->integrated_l);
    cJSON_AddNumberToObject(o,"estimated_l",a->estimated_l);
    if(a->direct_valid)cJSON_AddNumberToObject(o,"direct_l",a->direct_l);else cJSON_AddNullToObject(o,"direct_l");
    cJSON_AddNumberToObject(o,"gap_count",a->gaps);
    cJSON_AddBoolToObject(o,"unknown_gap",a->unknown_gap);
    cJSON_AddStringToObject(o,"quality",a->quality?a->quality:"SEM_LEITURA");
}
