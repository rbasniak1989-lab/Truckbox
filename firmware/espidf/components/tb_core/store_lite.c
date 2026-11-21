#include "tb.h"

/* Shared diagnostic producers retain their local cache. Legacy record writes
 * return a defined failure even if a producer is enabled in a future build. */
esp_err_t tb_store_submit(const char *record_json)
{
    (void)record_json;
    return ESP_ERR_NOT_SUPPORTED;
}
