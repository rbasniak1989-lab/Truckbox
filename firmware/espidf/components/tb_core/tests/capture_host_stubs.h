#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "protocols.h"
#include "tbx_trip.h"
#include "tbx_fuel.h"
#include "trip_checkpoint.h"
#include "tbx_j1587_bridge.h"
#include "tbx_j1708_active.h"
#define TB_RECORD_MAX 768
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define NVS_READWRITE 1
#define portMAX_DELAY UINT32_MAX
#define ESP_LOGW(tag, ...) ((void)(tag))
typedef int esp_err_t;
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef unsigned nvs_handle_t;
typedef struct {
    bool extd, rtr;
    uint32_t identifier;
    uint8_t data_length_code;
    uint8_t data[8];
} twai_message_t;
static SemaphoreHandle_t xSemaphoreCreateMutex(void);
static bool xSemaphoreTake(SemaphoreHandle_t, uint32_t);
static void xSemaphoreGive(SemaphoreHandle_t);
static int64_t esp_timer_get_time(void);
static esp_err_t nvs_open(const char *, int, nvs_handle_t *);
static esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
static esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
static esp_err_t nvs_commit(nvs_handle_t);

static struct { bool j1708_enabled,j1587_odometer_validated,j1587_fuel250_validated;uint8_t j1587_odometer_mid,j1587_fuel250_mid; } host_capture_config;
#define tb_config() (&host_capture_config)

#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
static uint32_t tb_boot_id(void){return 42;}

#define GPIO_NUM_20 20
#define gpio_intr_enable(pin) ((void)(pin),ESP_OK)
#define tb_web_ota_busy() false
