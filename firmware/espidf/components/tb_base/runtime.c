#include "tb.h"
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

static uint32_t s_boot_id;
static bool s_restart_scheduled;

const char *tb_version(void)
{
    return esp_app_get_description()->version;
}

uint32_t tb_boot_id(void)
{
    return s_boot_id;
}

/* Called once after NVS initialization and before role tasks start. */
esp_err_t tb_runtime_init(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("tb_system", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint32_t previous = 0;
    err = nvs_get_u32(handle, "boot_id", &previous);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }
    s_boot_id = previous == UINT32_MAX ? 1 : previous + 1;
    err = nvs_set_u32(handle, "boot_id", s_boot_id);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void restart_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

void tb_schedule_restart(uint32_t delay_ms)
{
    if (s_restart_scheduled) return;
    s_restart_scheduled = true;
    if (xTaskCreate(restart_task, "tb_restart", 2048,
                    (void *)(uintptr_t)delay_ms, 5, NULL) != pdPASS) {
        s_restart_scheduled = false;
        ESP_LOGE("tb_runtime", "Unable to schedule restart");
    }
}

void tb_base_add_json(cJSON *object)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running) (void)esp_ota_get_state_partition(running, &state);
    cJSON_AddBoolToObject(object, "ok", true);
    cJSON_AddStringToObject(object, "role", tb_role_name());
    cJSON_AddStringToObject(object, "device_uid", tb_device_uid());
    cJSON_AddStringToObject(object, "version", app->version);
    cJSON_AddStringToObject(object, "project", app->project_name);
    cJSON_AddNumberToObject(object, "boot_id", s_boot_id);
    cJSON_AddNumberToObject(object, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddNumberToObject(object, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(object, "heap_minimum", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(object, "heap_largest_block", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(object, "reset_reason", esp_reset_reason());
    cJSON_AddBoolToObject(object, "clock_synced", time(NULL) >= 1704067200);
    cJSON *ota = cJSON_AddObjectToObject(object, "ota");
    if (ota) {
        cJSON_AddStringToObject(ota, "running_partition", running ? running->label : "unknown");
        cJSON_AddNumberToObject(ota, "state", state);
        cJSON_AddBoolToObject(ota, "rollback_enabled", true);
    }
    cJSON_AddStringToObject(object, "hardware_notice",
        "Rev B1: validar EN/U8, chaves de alimentacao Q1-Q4 e GND do SIM. "
        "O firmware nao corrige pinagens espelhadas.");
}
