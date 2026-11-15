#pragma once
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
/* A fixed six-decimal JSON number without printf floating point/dtoa heap. */
static inline void tb_live_number(char out[40],bool valid,double value)
{
    if(!valid || !isfinite(value) || fabs(value)>9e12){snprintf(out,40,"null");return;}
    bool negative=value<0;uint64_t scaled=(uint64_t)llround(fabs(value)*1000000.0);
    snprintf(out,40,"%s%" PRIu64 ".%06u",negative?"-":"",scaled/1000000u,(unsigned)(scaled%1000000u));
}
