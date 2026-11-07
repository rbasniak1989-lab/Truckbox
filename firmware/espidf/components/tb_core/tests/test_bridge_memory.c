/* Tests the actual core.c serializer and ACK membership functions. A tracked
 * cJSON allocator models an unreachable Link: only wire bytes may stay live. */
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_under_test.h"

typedef union { max_align_t alignment; size_t size; } allocation_header_t;
static size_t live_bytes, live_allocations, allocation_budget = SIZE_MAX;
static void *tracked_malloc(size_t size)
{
    if (allocation_budget == 0) return NULL;
    if (allocation_budget != SIZE_MAX) --allocation_budget;
    allocation_header_t *header = malloc(sizeof(*header) + size);
    assert(header);
    header->size = size;
    live_bytes += size;
    ++live_allocations;
    return header + 1;
}
static void tracked_free(void *pointer)
{
    if (!pointer) return;
    allocation_header_t *header = (allocation_header_t *)pointer - 1;
    assert(live_bytes >= header->size && live_allocations);
    live_bytes -= header->size;
    --live_allocations;
    free(header);
}
static cJSON *make_batch(unsigned length)
{
    cJSON *batch = cJSON_CreateObject();
    assert(batch);
    cJSON_AddBoolToObject(batch, "ok", true);
    cJSON_AddNumberToObject(batch, "count", length);
    cJSON_AddStringToObject(batch, "device_uid", "tb-test");
    cJSON *samples = cJSON_AddArrayToObject(batch, "samples");
    for (unsigned i = 0; i < length; ++i) {
        cJSON *sample = cJSON_CreateObject();
        assert(cJSON_AddItemToArray(samples, sample));
        cJSON_AddNumberToObject(sample, "seq", i < 2 ? i + 1 : 256 + i);
        cJSON_AddStringToObject(sample, "source", "snapshot");
        cJSON_AddStringToObject(sample, "quoted", "a\"b\\c\n");
        for (unsigned field = 0; field < 24; ++field) {
            char key[24];
            snprintf(key, sizeof(key), "field_%u", field);
            cJSON_AddNumberToObject(sample, key, field + 0.25);
        }
    }
    return batch;
}
static void test_retained_memory_and_exact_ack(void)
{
    cJSON *batch = make_batch(BRIDGE_BATCH_LIMIT);
    char *old_payload = cJSON_PrintUnformatted(batch);
    assert(old_payload);
    size_t retained_before = live_bytes;
    char *expected = strdup(old_payload);
    assert(expected);
    cJSON_free(old_payload);

    bridge_batch_t prepared;
    assert(prepare_bridge_batch(batch, &prepared));
    assert(live_allocations == 1); /* No parsed tree held during network wait. */
    assert(!strcmp(prepared.payload, expected));
    assert(retained_before > live_bytes + 14000);
    printf("Same 8-record fixture: retained heap %zu -> %zu bytes; released %zu before HTTP wait\n",
           retained_before, live_bytes, retained_before - live_bytes);
    free(expected);
    assert(batch_contains(&prepared, 1));
    assert(batch_contains(&prepared, 2));
    assert(batch_contains(&prepared, 258));
    assert(!batch_contains(&prepared, 3)); /* A reserved sequence gap is not an ACK. */
    assert(!batch_contains(&prepared, 9999));
    cJSON_free(prepared.payload);
    prepared.payload = NULL;
    assert(live_bytes == 0 && live_allocations == 0);
    assert(batch_contains(&prepared, 258)); /* ACK check after freeing HTTP text. */
}
static void test_bad_batches(void)
{
    const char *bad[] = {
        "null", "{}", "{\"ok\":false,\"count\":1,\"samples\":[{\"seq\":1}]}",
        "{\"ok\":true,\"count\":0,\"samples\":[]}",
        "{\"ok\":true,\"count\":1,\"samples\":[{}]}",
        "{\"ok\":true,\"count\":1,\"samples\":[{\"seq\":-1}]}",
        "{\"ok\":true,\"count\":1,\"samples\":[{\"seq\":1.5}]}",
        "{\"ok\":true,\"count\":1,\"samples\":[{\"seq\":9007199254740992}]}",
        "{\"ok\":true,\"count\":2,\"samples\":[{\"seq\":1},{\"seq\":1}]}",
        "{\"ok\":true,\"count\":2,\"samples\":[{\"seq\":2},{\"seq\":1}]}",
        "{\"ok\":true,\"count\":1.5,\"samples\":[{\"seq\":1}]}",
        "{\"ok\":true,\"count\":2,\"samples\":[{\"seq\":1}]}"
    };
    bridge_batch_t prepared;
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        cJSON *batch = cJSON_Parse(bad[i]);
        assert(batch);
        assert(!prepare_bridge_batch(batch, &prepared));
        assert(!prepared.payload && prepared.count == 0);
        assert(live_bytes == 0 && live_allocations == 0);
    }
    assert(!prepare_bridge_batch(NULL, &prepared));
    assert(!prepare_bridge_batch(make_batch(BRIDGE_BATCH_LIMIT + 1), &prepared));
    assert(live_bytes == 0 && live_allocations == 0);
}
static void test_serialization_allocation_failure_and_size_limit(void)
{
    bridge_batch_t prepared;
    for (size_t budget = 0; budget < 8; ++budget) {
        cJSON *batch = make_batch(BRIDGE_BATCH_LIMIT);
        allocation_budget = budget;
        bool ready = prepare_bridge_batch(batch, &prepared);
        allocation_budget = SIZE_MAX;
        if (ready) cJSON_free(prepared.payload);
        else assert(!prepared.payload && prepared.count == 0);
        assert(live_bytes == 0 && live_allocations == 0);
    }
    cJSON *batch = make_batch(1);
    char oversized[BRIDGE_BATCH_BYTES + 1];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    assert(cJSON_AddStringToObject(batch, "oversized", oversized));
    assert(!prepare_bridge_batch(batch, &prepared));
    assert(live_bytes == 0 && live_allocations == 0);
}
int main(void)
{
    cJSON_Hooks hooks = { tracked_malloc, tracked_free };
    cJSON_InitHooks(&hooks);
    test_retained_memory_and_exact_ack();
    test_bad_batches();
    test_serialization_allocation_failure_and_size_limit();
    puts("Bridge serializer passed: early tree release, wire preservation, exact ACK IDs, bounded batches, allocation failure cleanup");
    return 0;
}
