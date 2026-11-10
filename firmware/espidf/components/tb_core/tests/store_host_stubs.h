#pragma once
/* Hardware/RTOS boundaries only. Tests include the real store.c transaction,
 * recovery, serialization, append, batch and ACK implementation. */
#include "cJSON.h"
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define TB_RECORD_MAX 768
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define pdTRUE 1
#define pdMS_TO_TICKS(value) (value)
#define portMAX_DELAY UINT32_MAX
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_LOGE(tag, ...) ((void)(tag))

typedef int esp_err_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef unsigned nvs_handle_t;
typedef int wl_handle_t;
typedef int sdmmc_card_t;
typedef int portMUX_TYPE;
typedef struct {
    uint64_t sequence_floor;
    char cloud_device_uid[65];
} tb_config_t;

static tb_config_t host_config;
static unsigned host_queue_length;
static unsigned host_fsync_calls;
static unsigned host_commit_calls;
static bool host_allow_compression=true;
static bool host_mutex_busy;
static bool host_fail_fsync_once;
static bool host_fail_commit_once;
static uint64_t host_nvs_ack, host_nvs_reserved;
static uint64_t host_pending_ack, host_pending_reserved;
static uint8_t host_pending_clean;

static inline const tb_config_t *tb_config(void) { return &host_config; }
static inline const char *tb_device_uid(void) { return "TB-HOST-0123456789AB"; }
static inline uint32_t tb_boot_id(void) { return 42; }
static inline int64_t esp_timer_get_time(void) { return 1234567; }
static inline bool xSemaphoreTake(void *lock, unsigned delay)
{ (void)lock; (void)delay; return !host_mutex_busy; }
static inline void xSemaphoreGive(void *lock) { (void)lock; }
static inline void vTaskDelay(unsigned delay) { (void)delay; }
static inline unsigned uxQueueMessagesWaiting(void *queue)
{ (void)queue; return host_queue_length; }
static inline bool xQueueSend(void *queue, const void *item, unsigned delay)
{
    (void)queue; (void)item; (void)delay;
    if (host_queue_length >= 64) return false;
    ++host_queue_length;
    return true;
}
static inline esp_err_t nvs_set_u64(nvs_handle_t handle, const char *name, uint64_t value)
{
    (void)handle;
    if (!strcmp(name, "ack")) host_pending_ack = value;
    else if (!strcmp(name, "reserved")) host_pending_reserved = value;
    return ESP_OK;
}
static inline esp_err_t nvs_set_u8(nvs_handle_t handle, const char *name, uint8_t value)
{ (void)handle; (void)name; host_pending_clean = value; return ESP_OK; }
static inline esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    ++host_commit_calls;
    if (host_fail_commit_once) { host_fail_commit_once = false; return ESP_FAIL; }
    host_nvs_ack = host_pending_ack;
    host_nvs_reserved = host_pending_reserved;
    return ESP_OK;
}
static inline int store_host_fsync(int descriptor)
{
    ++host_fsync_calls;
    if (host_fail_fsync_once) { host_fail_fsync_once = false; errno = EIO; return -1; }
    return fsync(descriptor);
}
#define fsync store_host_fsync
