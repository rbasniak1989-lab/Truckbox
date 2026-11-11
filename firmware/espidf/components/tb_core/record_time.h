#pragma once
#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
/* Observation time, not upload time; no GPS dependency or invented UTC. */
static inline bool tb_record_time(cJSON *row,uint32_t boot,uint64_t uptime,uint64_t unix_ms)
{
    bool valid=unix_ms>=UINT64_C(1704067200000) && unix_ms<UINT64_C(253402300800000);
    bool ok=cJSON_AddNumberToObject(row,"boot_id",boot) && cJSON_AddNumberToObject(row,"uptime_ms",(double)uptime) &&
        cJSON_AddBoolToObject(row,"time_valid",valid) && cJSON_AddStringToObject(row,"time_source",valid?"system_clock":"boot_monotonic");
    if(valid){
        time_t seconds=(time_t)(unix_ms/1000);struct tm utc;char prefix[32],text[40];
        if(!gmtime_r(&seconds,&utc) || !strftime(prefix,sizeof(prefix),"%Y-%m-%dT%H:%M:%S",&utc))return false;
        snprintf(text,sizeof(text),"%s.%03uZ",prefix,(unsigned)(unix_ms%1000));
        ok=ok && cJSON_AddStringToObject(row,"ts",text);
    }
    return ok;
}
