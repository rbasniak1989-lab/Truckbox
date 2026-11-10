#pragma once
/* Fault-injecting cJSON allocator. Host byte counts include native cJSON node
 * sizes and are not claimed as physical ESP32 heap measurements. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "cJSON.h"
typedef union { max_align_t alignment; struct { size_t size; uint32_t magic; } info; } allocation_header_t;
static size_t heap_used,heap_peak,heap_calls,heap_fail_call,heap_budget=SIZE_MAX;
static void *test_allocate(size_t size)
{
    ++heap_calls;
    if((heap_fail_call && heap_calls==heap_fail_call) || size>heap_budget-heap_used)return NULL;
    allocation_header_t *h=malloc(sizeof(*h)+size);
    if(!h)return NULL;
    h->info.size=size;h->info.magic=0x1abe12ab;
    heap_used+=size;if(heap_used>heap_peak)heap_peak=heap_used;
    return h+1;
}
static void test_free(void *pointer)
{
    if(!pointer)return;
    allocation_header_t *h=(allocation_header_t *)pointer-1;
    assert(h->info.magic==0x1abe12ab && heap_used>=h->info.size);
    heap_used-=h->info.size;h->info.magic=0;free(h);
}
static void heap_reset(void)
{
    assert(heap_used==0);heap_peak=heap_calls=heap_fail_call=0;heap_budget=SIZE_MAX;
    cJSON_Hooks hooks={test_allocate,test_free};cJSON_InitHooks(&hooks);
}
