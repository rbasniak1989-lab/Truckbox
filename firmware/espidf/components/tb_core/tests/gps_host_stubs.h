#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#define LIVE_CLIENTS 4
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(value) (value)
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
static bool xSemaphoreTake(SemaphoreHandle_t, uint32_t);
static void xSemaphoreGive(SemaphoreHandle_t);
static int64_t esp_timer_get_time(void);
static int host_settimeofday(const struct timeval *, const void *);
#define settimeofday host_settimeofday
