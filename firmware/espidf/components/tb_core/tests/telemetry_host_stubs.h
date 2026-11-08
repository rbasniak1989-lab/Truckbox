#pragma once
/* Test only: RTOS, wall-clock source and storage boundaries. The serializer,
 * splitting, decoders, capture identities and latest-reading cache are real. */
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define TB_RECORD_MAX 768
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_FAIL -1
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(value) (value)
#define portMAX_DELAY UINT32_MAX
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef struct {
    size_t capacity, size, count;
    unsigned char *bytes;
} *QueueHandle_t;
static uint64_t host_now_us = 10000000;
static bool host_have_gps, host_full_gps, host_fail_store;
static uint64_t host_gps_received_us;
static char host_records[256][TB_RECORD_MAX];
static unsigned host_record_count;
typedef struct { bool full_decode_enabled; } tb_config_t;
static tb_config_t host_telemetry_config = { .full_decode_enabled = true };
static const tb_config_t *tb_config(void) { return &host_telemetry_config; }
static uint32_t tb_boot_id(void) { return 42; }
static void vTaskDelay(unsigned ticks) { (void)ticks; }
static int64_t esp_timer_get_time(void) { return (int64_t)host_now_us; }
static QueueHandle_t xQueueCreate(size_t capacity, size_t size)
{
    QueueHandle_t queue = calloc(1, sizeof(*queue));
    if (queue) {
        queue->bytes = calloc(capacity, size);
        queue->capacity = capacity; queue->size = size;
    }
    return queue;
}
static void vQueueDelete(QueueHandle_t queue)
{ if (queue) { free(queue->bytes); free(queue); } }
static int xQueueSend(QueueHandle_t queue, const void *item, unsigned delay)
{
    (void)delay;
    if (!queue || queue->count == queue->capacity) return 0;
    memcpy(queue->bytes + queue->count++ * queue->size, item, queue->size);
    return pdTRUE;
}
static int xQueueReceive(QueueHandle_t queue, void *item, unsigned delay)
{
    (void)delay;
    if (!queue || !queue->count) return 0;
    memcpy(item, queue->bytes, queue->size);
    --queue->count;
    memmove(queue->bytes, queue->bytes + queue->size, queue->count * queue->size);
    return pdTRUE;
}
static int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *argument, unsigned priority, void *handle)
{
    (void)task; (void)name; (void)stack; (void)argument; (void)priority; (void)handle;
    return pdPASS;
}
static esp_err_t tb_store_submit(const char *json)
{
    if (host_fail_store) return ESP_FAIL;
    if (host_record_count >= 256 || strlen(json) >= TB_RECORD_MAX) abort();
    strcpy(host_records[host_record_count++], json);
    return ESP_OK;
}
static bool tb_core_append_gps_at(cJSON *record, uint64_t captured_us)
{
    if (!host_have_gps || host_gps_received_us > captured_us) return false;
    cJSON_AddStringToObject(record, "gps_status", "FIX");
    cJSON_AddBoolToObject(record, "has_fix", true);
    cJSON_AddNumberToObject(record, "lat", -25.123456789);
    cJSON_AddNumberToObject(record, "lon", -49.987654321);
    cJSON_AddStringToObject(record, "gps_source", "android");
    if (host_full_gps) {
        cJSON_AddNumberToObject(record, "gps_speed_kmh", 83.12345678);
        cJSON_AddNumberToObject(record, "speed_kmh", 83.12345678);
        cJSON_AddStringToObject(record, "speed_source", "android");
        cJSON_AddNumberToObject(record, "heading_deg", 179.1234567);
        cJSON_AddNumberToObject(record, "altitude_m", 987.1234567);
        cJSON_AddNumberToObject(record, "gps_accuracy_m", 12.1234567);
        cJSON_AddNumberToObject(record, "gps_time_ms", 1794000123456.0);
    }
    return true;
}
