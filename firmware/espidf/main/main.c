#include "tb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern esp_err_t tb_runtime_init(void);
static const char *TAG = "truckbox";

static void fail_startup(esp_err_t err, const char *operation)
{
    ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(err));
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGE(TAG, "New image failed startup; restoring previous OTA application");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    /* Preserve NVS and logs. A failed first USB image must not erase configuration. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

static void print_setup(void)
{
    tb_network_status_t network;
    tb_network_status(&network);
    printf("\nTRUCKBOX %s %s\nDevice: %s\n", tb_role_name(), tb_version(), tb_device_uid());
    printf("AP: %s\nAP password: %s\nAP URL: http://%s/\n",
           network.ap_ssid, tb_config()->ap_password, network.ap_ip);
    printf("Admin key: %s\n", tb_config()->admin_token);
    printf("Keep these local credentials private. Configure the station Wi-Fi on the page.\n");
    fflush(stdout);
}

static void console_task(void *arg)
{
    (void)arg;
    char line[64];
    for (;;) {
        if (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strcmp(line, "setup")) {
                print_setup();
            } else if (!strcmp(line, "status")) {
                cJSON *status = cJSON_CreateObject();
                if (status) {
                    tb_base_add_json(status);
                    tb_network_add_json(status);
                    tb_application_add_json(status);
                    char *json = cJSON_PrintUnformatted(status);
                    if (json) { puts(json); free(json); }
                    cJSON_Delete(status);
                }
            } else if (!strcmp(line, "restart")) {
                tb_schedule_restart(1000);
            } else {
                puts("Commands: setup | status | restart");
            }
        } else {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void)
{
    /* Detach ROM UART0 outputs before its pins can be reused by this board. */
    gpio_reset_pin(GPIO_NUM_16);
    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_INPUT);
    gpio_reset_pin(GPIO_NUM_17);
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_INPUT);
    if (tb_role() == TB_ROLE_CORE) {
        gpio_reset_pin(GPIO_NUM_1);
        gpio_set_direction(GPIO_NUM_1, GPIO_MODE_INPUT);
    }
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&usb) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();
    }
    esp_err_t err = tb_config_init();
    if (err != ESP_OK) fail_startup(err, "NVS/configuration");
    err = tb_runtime_init();
    if (err != ESP_OK) fail_startup(err, "boot identity");
    err = tb_network_start();
    if (err != ESP_OK) fail_startup(err, "Wi-Fi");
    err = tb_application_start();
    if (err != ESP_OK) fail_startup(err, "role services");
    err = tb_web_start();
    if (err != ESP_OK) fail_startup(err, "HTTP/OTA");
    if (xTaskCreate(console_task, "usb_console", 6144, NULL, 2, NULL) != pdPASS) {
        fail_startup(ESP_ERR_NO_MEM, "USB console");
    }
    ESP_LOGI(TAG, "Services ready. Type setup on USB to retrieve local access credentials.");
    /* Do not require router connectivity, a SIM, GPS fix or vehicle traffic. */
    vTaskDelay(pdMS_TO_TICKS(15000));
    size_t startup_free = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Startup heap: free=%u minimum=%u largest=%u bytes (required=20000)",
             (unsigned)startup_free, (unsigned)esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    if (startup_free < 20000) fail_startup(ESP_ERR_NO_MEM, "startup heap");
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK
        && state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) fail_startup(err, "OTA confirmation");
        ESP_LOGI(TAG, "New image confirmed; previous image remains available for the next update.");
    }
}
