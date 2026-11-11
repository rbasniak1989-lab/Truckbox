#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#define TB_RECORD_MAX 768
#define ESP_OK 0
#define ESP_FAIL -1
#define NVS_READWRITE 1
typedef unsigned nvs_handle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
int nvs_open(const char *,int,nvs_handle_t *);
int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
int nvs_commit(nvs_handle_t);
int tb_store_submit(const char *);
int64_t esp_timer_get_time(void);
uint32_t tb_boot_id(void);

typedef unsigned SemaphoreHandle_t;
#define pdMS_TO_TICKS(n) (n)
#define pdTRUE 1
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
const char *tb_device_uid(void);
