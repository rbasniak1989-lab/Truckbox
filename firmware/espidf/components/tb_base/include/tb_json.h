#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Allocation-free encoding. The owned variant consumes the complete tree on
 * success AND failure, freeing emitted children as it progresses. */
typedef bool (*tb_json_sink_t)(const char *, size_t, void *);
size_t tb_json_measure(const cJSON *, size_t maximum_bytes); /* SIZE_MAX on error. */
bool tb_json_write_owned(cJSON *, tb_json_sink_t, void *);
char *tb_json_print_owned(cJSON *, size_t maximum_bytes); /* cJSON_free result. */

/* cJSON leaves rejected items owned by the caller. These helpers consume the
 * item on either outcome, including failure to allocate an object key. */
static inline bool tb_json_add_array(cJSON *array, cJSON *item)
{
    if (cJSON_AddItemToArray(array, item)) return true;
    cJSON_Delete(item);
    return false;
}

static inline bool tb_json_add_object(cJSON *object, const char *key, cJSON *item)
{
    if (cJSON_AddItemToObject(object, key, item)) return true;
    cJSON_Delete(item);
    return false;
}
