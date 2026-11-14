#pragma once
#include "store_internal.h"
#include <stddef.h>
/* A technical hold never changes the operator's desired polling policy.
 * Only a checksum-valid independent RX frame can create this record. */
typedef struct {
    uint32_t version,boot;
    uint64_t uptime_us;
    uint8_t mid,size,frame[21],reserved;
    uint32_t crc;
} tb_polling_guard_t;
static inline bool tb_polling_guard_valid(const tb_polling_guard_t *g)
{
    if(g->version!=1 || g->mid<128 || g->size<2 || g->size>21 || g->frame[0]!=g->mid ||
       g->crc!=tb_store_crc32(g,offsetof(tb_polling_guard_t,crc)))return false;
    uint8_t sum=0;for(unsigned i=0;i<g->size;i++)sum+=g->frame[i];return sum==0;
}
