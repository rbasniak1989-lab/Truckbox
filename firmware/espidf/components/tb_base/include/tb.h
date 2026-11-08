#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"

#define TB_RECORD_MAX 768
#define TB_LINK_IP "192.168.4.2"
#define TB_CORE_AP_IP "192.168.4.1"

typedef enum { TB_ROLE_CORE = 0, TB_ROLE_LINK = 1 } tb_role_t;

typedef struct {
    char wifi_ssid[33];
    char wifi_password[65];
    char admin_token[33];
    char ap_password[17];
    char pair_key[65];
    char core_ssid[33];
    char core_password[65];
    char core_host[65];
    char uplink_url[257];
    char uplink_token[129];
    char cloud_device_uid[65];
    char gps_url[257];
    char gps_device_uid[65];
    char gps_token[129];
    char apn[65];
    char apn_user[65];
    char apn_password[65];
    uint32_t can_baud;
    uint32_t gnss_baud;
    uint64_t sequence_floor;
    bool capture_enabled;
    bool full_decode_enabled; /* opt-in; LEGACY telemetry is the default */
    bool sd_enabled;
    bool fram_enabled;
    bool modem_enabled;
    bool gnss_enabled;
    bool repaired_power_gates;
} tb_config_t;

typedef struct {
    bool station_connected;
    bool access_point_running;
    char station_ip[16];
    char ap_ip[16];
    char ap_ssid[33];
    char hostname[40];
    int rssi;
} tb_network_status_t;

tb_role_t tb_role(void);
const char *tb_role_name(void);
const char *tb_device_uid(void);
const char *tb_version(void);
uint32_t tb_boot_id(void);
const tb_config_t *tb_config(void);
esp_err_t tb_config_init(void);
esp_err_t tb_config_apply_json(const cJSON *json, char *error, size_t error_size);
cJSON *tb_config_public_json(void);
bool tb_auth_admin(const char *token);
bool tb_auth_pair(const char *token);
void tb_schedule_restart(uint32_t delay_ms);

esp_err_t tb_network_start(void);
void tb_network_status(tb_network_status_t *status);
void tb_network_add_json(cJSON *object);
bool tb_network_wifi_uplink(void);
void tb_network_start_time_sync(void);
void tb_network_note_link_address(const char *address);
void tb_network_note_link_socket(int socket_fd);
void tb_network_link_host(char *host, size_t capacity);
void tb_base_add_json(cJSON *object);

esp_err_t tb_web_start(void);
void tb_web_set_ota_busy(bool busy);
bool tb_web_ota_busy(void);

/* The selected role implements these entry points. Other-role files must not be linked. */
esp_err_t tb_application_start(void);
void tb_application_add_json(cJSON *object);
esp_err_t tb_application_register_routes(httpd_handle_t server);

/* Core storage. Submit valid JSON objects, never fabricated sample values. */
esp_err_t tb_store_submit(const char *record_json);
cJSON *tb_store_batch(size_t limit, size_t max_bytes);
esp_err_t tb_store_ack(uint64_t sequence);
/* Explicitly discard local telemetry logs on the next boot. Never erase NVS/OTA. */
esp_err_t tb_store_schedule_discard(void);
void tb_store_add_json(cJSON *object);

/* Link accepts batches from Core and returns an explicit ack_seq after HTTPS acceptance. */
esp_err_t tb_link_uplink(const char *json, size_t length, char **response_json);

/* Common authenticated handlers may use this helper (owned by tb_web). */
bool tb_http_require_admin(httpd_req_t *request);
bool tb_http_require_pair(httpd_req_t *request);
esp_err_t tb_http_send_json(httpd_req_t *request, cJSON *owned_json);
esp_err_t tb_http_read_json(httpd_req_t *request, size_t max_size, cJSON **json);
