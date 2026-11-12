#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "../../tb_base/include/tb_json.h"
#include "protocols.h"
#include "tbx_trip.h"
#include "tbx_fuel.h"
#include "trip_checkpoint.h"
#include "tbx_j1587_bridge.h"
#include "tbx_j1708_active.h"
#include "tbx_j1708_auto.h"
#include "tbx_transmission.h"
#define tb_config_j1708_auto_enabled() false
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

static void tb_ecu_counters_observe(const tbx_j1587_observation_t *o,bool matched){(void)o;(void)matched;}
static struct { bool full_decode_enabled,j1708_enabled,j1587_odometer_validated,j1587_fuel250_validated;uint8_t j1587_odometer_mid,j1587_fuel250_mid; } host_capture_config;
#define tb_config() (&host_capture_config)

#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
static uint32_t tb_boot_id(void){return 42;}

#define GPIO_NUM_20 20
#define gpio_intr_enable(pin) ((void)(pin),ESP_OK)
#define tb_web_ota_busy() false

/* Actual RX task driven by scripted UART interrupts in host integration tests. */
#define UART_NUM_1 1
#define UART_DATA 1
#define UART_FIFO_OVF 2
#define UART_BUFFER_FULL 3
#define UART_FRAME_ERR 4
#define UART_PARITY_ERR 5
#define UART_LL_GET_HW(port) (port)
typedef struct {int type;size_t size;bool timeout_flag;}uart_event_t;
static bool xQueueReceive(QueueHandle_t,void *,unsigned);
static bool xQueueSend(QueueHandle_t,const void *,unsigned);
static void xQueueReset(QueueHandle_t);
static int uart_read_bytes(int,void *,size_t,unsigned);
static void uart_flush_input(int);
static int gpio_get_level(int);
static unsigned uart_ll_get_rxfifo_len(int);
static int uart_get_buffered_data_len(int,size_t *);
static bool tb_telemetry_j1708(const uint8_t *,size_t,uint64_t,bool);
static bool tb_telemetry_j1708_echo(const uint8_t *,size_t,uint64_t,bool);
