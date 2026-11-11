#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct { uint16_t pid; uint8_t mid; uint32_t period_ms; } tbx_j1708_policy_t;
/* Targets from the documented 29/10/2026 NEXIQ capture. No local MID default. */
static inline const tbx_j1708_policy_t *tbx_j1708_policy(void)
{
    static const tbx_j1708_policy_t policy[17]={
        {26,128,60000},{33,130,60000},{34,130,60000},{37,130,60000},
        {62,144,60000},{68,128,60000},{117,140,60000},{118,140,60000},
        {235,128,900000},{236,128,900000},{250,128,300000},
        {31,130,60000},{32,130,60000},{63,130,60000},{108,128,300000},
        {158,128,60000},{160,130,60000}
    };
    return policy;
}
/* Keep legacy/manual intervals parseable by rollback; production has its own
 * persistent cadence tables and still shares exactly one request scheduler. */
static inline uint32_t tbx_j1708_production_period(uint16_t pid,bool launch)
{
    switch(pid){
    case 31:return launch?2000:10000;
    case 33:return launch?2000:5000;
    case 34:case 63:return launch?5000:15000;
    case 32:case 160:return launch?10000:30000;
    case 37:return launch?15000:30000;
    case 250:return 300000;
    case 235:case 236:return 900000;
    default:return 60000;
    }
}
