#include "tb.h"
#include "tb_nmea.h"
#include "tb_gps_outbox.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_netif_sntp.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TB_GNSS_UART UART_NUM_0
#define TB_GNSS_RX 17
#define TB_LTE_UART UART_NUM_1
#define TB_LTE_RX 10
#define TB_LTE_TX 11
#define TB_LTE_PWRKEY 0
#define TB_PPP_CONNECTED BIT0
#define TB_PPP_DISCONNECTED BIT1
#define TB_GNSS_FRESH_US INT64_C(15000000)
#define TB_UPLINK_MAX (32 * 1024)
#define TB_JSON_SAFE_INTEGER 9007199254740991.0

static const char *TAG = "tb_link";
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static EventGroupHandle_t s_ppp_events;
static SemaphoreHandle_t s_uplink_lock;
static SemaphoreHandle_t s_gps_outbox_lock;
static tb_gps_outbox_t s_gps_outbox;
static wl_handle_t s_gps_wl = WL_INVALID_HANDLE;
static esp_netif_t *s_ppp_netif;
static esp_modem_dce_t *s_modem;
static esp_netif_t *uplink_netif(void);

static const char *uplink_transport(void)
{
    esp_netif_t *netif = uplink_netif();
    return !netif ? "UNAVAILABLE" : netif == s_ppp_netif ? "LTE" : "WIFI";
}

typedef struct {
    tb_gnss_fix_t fix;
    int64_t last_fix_us;
    uint64_t nmea_lines;
    uint64_t nmea_invalid;
    uint64_t nmea_overflows;
    uint64_t telemetry_sent;
    uint64_t telemetry_failed;
    bool ppp_connected;
    bool sim_ready;
    bool has_signal;
    int signal_dbm;
    int ber;
    int64_t signal_us;
    char ppp_ip[16];
    char modem_phase[48];
    int32_t ppp_error;
    esp_err_t last_modem_error;
    uint64_t uplink_ok;
    uint64_t uplink_failed;
    uint64_t gps_upload_ok;
    uint64_t gps_upload_failed;
    int last_http_status;
    char uplink_error[48];
    char gps_upload_error[48];
    bool gps_storage_ready;
    uint64_t gps_pending;
    uint64_t gps_storage_bytes;
    uint64_t gps_dropped;
    uint64_t gps_recovered_tail_bytes;
    char gps_storage_phase[48];
    int gps_storage_error;
} link_state_t;

static link_state_t s_state;

static link_state_t state_snapshot(void)
{
    link_state_t snapshot;
    portENTER_CRITICAL(&s_state_lock);
    snapshot = s_state;
    portEXIT_CRITICAL(&s_state_lock);
    return snapshot;
}

static void modem_phase(const char *phase, esp_err_t error)
{
    portENTER_CRITICAL(&s_state_lock);
    strlcpy(s_state.modem_phase, phase, sizeof(s_state.modem_phase));
    s_state.last_modem_error = error;
    portEXIT_CRITICAL(&s_state_lock);
}

static void add_number(cJSON *object, const char *name, double value)
{
    if (isfinite(value)) cJSON_AddNumberToObject(object, name, value);
}

void tb_application_add_json(cJSON *object)
{
    const tb_config_t *config = tb_config();
    link_state_t state = state_snapshot();
    const int64_t now = esp_timer_get_time();
    const bool fresh = state.fix.valid && state.last_fix_us > 0 && now - state.last_fix_us <= TB_GNSS_FRESH_US;
    cJSON *gps = cJSON_AddObjectToObject(object, "gps");
    cJSON_AddBoolToObject(gps, "enabled", config->gnss_enabled);
    cJSON_AddBoolToObject(gps, "valid", fresh);
    if (state.last_fix_us > 0) cJSON_AddNumberToObject(gps, "age_ms", (now - state.last_fix_us) / 1000);
    else cJSON_AddNullToObject(gps, "age_ms");
    cJSON_AddNumberToObject(gps, "nmea_lines", (double)state.nmea_lines);
    cJSON_AddNumberToObject(gps, "invalid_lines", (double)state.nmea_invalid);
    cJSON_AddNumberToObject(gps, "uart_overflows", (double)state.nmea_overflows);
    cJSON_AddBoolToObject(gps, "storage_ready", state.gps_storage_ready);
    cJSON_AddStringToObject(gps, "storage_phase", state.gps_storage_phase[0] ? state.gps_storage_phase : "starting");
    cJSON_AddNumberToObject(gps, "pending_points", (double)state.gps_pending);
    cJSON_AddNumberToObject(gps, "storage_bytes", (double)state.gps_storage_bytes);
    cJSON_AddNumberToObject(gps, "dropped_points", (double)state.gps_dropped);
    cJSON_AddNumberToObject(gps, "recovered_tail_bytes", (double)state.gps_recovered_tail_bytes);
    if (state.gps_storage_error) cJSON_AddNumberToObject(gps, "storage_error", state.gps_storage_error);
    if (fresh) {
        add_number(gps, "lat", state.fix.latitude);
        add_number(gps, "lon", state.fix.longitude);
        if (state.fix.has_speed) add_number(gps, "speed_kmh", state.fix.speed_kmh);
        if (state.fix.has_heading) add_number(gps, "bearing_deg", state.fix.heading_deg);
        if (state.fix.has_altitude) add_number(gps, "altitude_m", state.fix.altitude_m);
        if (state.fix.has_hdop) add_number(gps, "hdop", state.fix.hdop);
        if (state.fix.quality) cJSON_AddNumberToObject(gps, "fix_quality", state.fix.quality);
        if (state.fix.satellites) cJSON_AddNumberToObject(gps, "satellites", state.fix.satellites);
        if (state.fix.utc_valid) cJSON_AddNumberToObject(gps, "time_ms", (double)state.fix.unix_ms);
    }
    cJSON *cellular = cJSON_AddObjectToObject(object, "cellular");
    cJSON_AddBoolToObject(cellular, "enabled", config->modem_enabled);
    cJSON_AddStringToObject(cellular, "phase", state.modem_phase[0] ? state.modem_phase : "disabled");
    cJSON_AddBoolToObject(cellular, "sim_ready", state.sim_ready);
    cJSON_AddBoolToObject(cellular, "ppp_connected", state.ppp_connected);
    if (state.ppp_connected) cJSON_AddStringToObject(cellular, "ip", state.ppp_ip);
    if (state.has_signal) {
        cJSON_AddNumberToObject(cellular, "signal_dbm", state.signal_dbm);
        cJSON_AddNumberToObject(cellular, "signal_age_ms", (now - state.signal_us) / 1000);
    }
    cJSON_AddNumberToObject(cellular, "ppp_error", state.ppp_error);
    cJSON_AddStringToObject(cellular, "last_error", esp_err_to_name(state.last_modem_error));
    cJSON_AddBoolToObject(cellular, "uplink_configured", config->uplink_url[0] != 0);
    cJSON_AddNumberToObject(cellular, "uplink_ok", (double)state.uplink_ok);
    cJSON_AddNumberToObject(cellular, "uplink_failed", (double)state.uplink_failed);
    cJSON *uplink = cJSON_AddObjectToObject(object, "uplink");
    cJSON_AddStringToObject(uplink, "transport", uplink_transport());
    cJSON_AddBoolToObject(uplink, "configured", config->uplink_url[0] && config->uplink_token[0]);
    cJSON_AddNumberToObject(uplink, "successful_acks", (double)state.uplink_ok);
    cJSON_AddNumberToObject(uplink, "failed", (double)state.uplink_failed);
    cJSON_AddNumberToObject(uplink, "http_status", state.last_http_status);
    cJSON_AddStringToObject(uplink, "error", state.uplink_error);
    cJSON_AddNumberToObject(cellular, "gps_upload_ok", (double)state.gps_upload_ok);
    cJSON_AddNumberToObject(cellular, "gps_upload_failed", (double)state.gps_upload_failed);
    if (state.last_http_status) cJSON_AddNumberToObject(cellular, "last_http_status", state.last_http_status);
    if (state.uplink_error[0]) cJSON_AddStringToObject(cellular, "uplink_error", state.uplink_error);
    if (state.gps_upload_error[0]) cJSON_AddStringToObject(cellular, "gps_upload_error", state.gps_upload_error);
    cJSON_AddNumberToObject(object, "link_telemetry_sent", (double)state.telemetry_sent);
    cJSON_AddNumberToObject(object, "link_telemetry_failed", (double)state.telemetry_failed);
}

static void gnss_task(void *arg)
{
    (void)arg;
    uart_config_t uart = {
        .baud_rate = (int)tb_config()->gnss_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    QueueHandle_t events = NULL;
    esp_err_t error = uart_param_config(TB_GNSS_UART, &uart);
    if (error == ESP_OK) error = uart_set_pin(TB_GNSS_UART, UART_PIN_NO_CHANGE, TB_GNSS_RX,
                                              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (error == ESP_OK) error = uart_driver_install(TB_GNSS_UART, 4096, 0, 16, &events, 0);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "GNSS UART initialization: %s", esp_err_to_name(error));
        vTaskDelete(NULL);
        return;
    }
    char line[160];
    size_t used = 0;
    bool discarding = false;
    tb_gnss_fix_t fix = {0};
    uint8_t input[128];
    for (;;) {
        uart_event_t event;
        while (xQueueReceive(events, &event, 0) == pdTRUE) {
            if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
                uart_flush_input(TB_GNSS_UART);
                used = 0;
                discarding = true;
                portENTER_CRITICAL(&s_state_lock);
                s_state.nmea_overflows++;
                portEXIT_CRITICAL(&s_state_lock);
            }
        }
        int count = uart_read_bytes(TB_GNSS_UART, input, sizeof(input), pdMS_TO_TICKS(100));
        for (int i = 0; i < count; ++i) {
            char c = (char)input[i];
            if (c == '$') { used = 0; discarding = false; }
            if (discarding) {
                if (c == '\n') discarding = false;
                continue;
            }
            if (c == '\n') {
                line[used] = 0;
                tb_nmea_result_t parsed = tb_nmea_parse(line, &fix);
                portENTER_CRITICAL(&s_state_lock);
                s_state.nmea_lines++;
                if (parsed == TB_NMEA_INVALID) s_state.nmea_invalid++;
                if (parsed == TB_NMEA_VALID || parsed == TB_NMEA_NO_FIX) s_state.fix = fix;
                if (parsed == TB_NMEA_VALID) s_state.last_fix_us = esp_timer_get_time();
                portEXIT_CRITICAL(&s_state_lock);
                if (parsed == TB_NMEA_VALID && fix.utc_valid && time(NULL) < 1704067200) {
                    struct timeval value = {.tv_sec = (time_t)(fix.unix_ms / 1000),
                                            .tv_usec = (suseconds_t)((fix.unix_ms % 1000) * 1000)};
                    settimeofday(&value, NULL);
                }
                used = 0;
            } else if (used + 1 < sizeof(line)) {
                line[used++] = c;
            } else {
                used = 0;
                discarding = true;
                portENTER_CRITICAL(&s_state_lock);
                s_state.nmea_invalid++;
                portEXIT_CRITICAL(&s_state_lock);
            }
        }
    }
}

static void on_ppp_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *event = data;
        portENTER_CRITICAL(&s_state_lock);
        s_state.ppp_connected = true;
        s_state.ppp_error = 0;
        snprintf(s_state.ppp_ip, sizeof(s_state.ppp_ip), IPSTR, IP2STR(&event->ip_info.ip));
        portEXIT_CRITICAL(&s_state_lock);
        esp_netif_set_default_netif(event->esp_netif);
        xEventGroupClearBits(s_ppp_events, TB_PPP_DISCONNECTED);
        xEventGroupSetBits(s_ppp_events, TB_PPP_CONNECTED);
        modem_phase("online", ESP_OK);
        tb_network_start_time_sync();
    } else if (id == IP_EVENT_PPP_LOST_IP) {
        portENTER_CRITICAL(&s_state_lock);
        s_state.ppp_connected = false;
        s_state.ppp_ip[0] = 0;
        portEXIT_CRITICAL(&s_state_lock);
        xEventGroupClearBits(s_ppp_events, TB_PPP_CONNECTED);
        xEventGroupSetBits(s_ppp_events, TB_PPP_DISCONNECTED);
    }
}

static void on_ppp_status(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if ((id > 0 && id < NETIF_PP_PHASE_OFFSET) || id == NETIF_PPP_CONNECT_FAILED) {
        portENTER_CRITICAL(&s_state_lock);
        s_state.ppp_error = id;
        portEXIT_CRITICAL(&s_state_lock);
        xEventGroupSetBits(s_ppp_events, TB_PPP_DISCONNECTED);
    }
}

static esp_err_t sync_modem(void)
{
    for (unsigned tries = 0; tries < 3; ++tries) {
        if (esp_modem_sync(s_modem) == ESP_OK) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return ESP_ERR_TIMEOUT;
}

static void modem_task(void *arg)
{
    (void)arg;
    const tb_config_t *config = tb_config();
    if (!config->apn[0]) {
        modem_phase("configure_apn", ESP_ERR_INVALID_STATE);
        vTaskDelete(NULL);
        return;
    }
    esp_netif_inherent_config_t inherent = ESP_NETIF_INHERENT_DEFAULT_PPP();
    inherent.route_prio = 1000;
    esp_netif_config_t netif = {.base = &inherent, .stack = ESP_NETIF_NETSTACK_DEFAULT_PPP};
    s_ppp_netif = esp_netif_new(&netif);
    if (!s_ppp_netif) {
        modem_phase("ppp_netif_failed", ESP_ERR_NO_MEM);
        vTaskDelete(NULL);
        return;
    }
    esp_err_t error = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ppp_ip, NULL);
    if (error == ESP_OK) error = esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, on_ppp_status, NULL);
    if (error != ESP_OK) {
        modem_phase("ppp_events_failed", error);
        vTaskDelete(NULL);
        return;
    }
    esp_netif_ppp_config_t parameters = {.ppp_error_event_enabled = true};
    error = esp_netif_ppp_set_params(s_ppp_netif, &parameters);
    if (error != ESP_OK) {
        modem_phase("ppp_parameters_failed", error);
        vTaskDelete(NULL);
        return;
    }
    if (config->apn_user[0]) {
        error = esp_netif_ppp_set_auth(s_ppp_netif, (esp_netif_auth_type_t)(NETIF_PPP_AUTHTYPE_PAP | NETIF_PPP_AUTHTYPE_CHAP),
                                     config->apn_user, config->apn_password);
        if (error != ESP_OK) {
            modem_phase("ppp_auth_failed", error);
            vTaskDelete(NULL);
            return;
        }
    }
    esp_modem_dte_config_t dte = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte.uart_config.port_num = TB_LTE_UART;
    dte.uart_config.tx_io_num = TB_LTE_TX;
    dte.uart_config.rx_io_num = TB_LTE_RX;
    dte.uart_config.rts_io_num = UART_PIN_NO_CHANGE;
    dte.uart_config.cts_io_num = UART_PIN_NO_CHANGE;
    dte.uart_config.flow_control = ESP_MODEM_FLOW_CONTROL_NONE;
    dte.uart_config.rx_buffer_size = 8192;
    dte.uart_config.tx_buffer_size = 2048;
    dte.dte_buffer_size = 2048;
    esp_modem_dce_config_t dce = ESP_MODEM_DCE_DEFAULT_CONFIG(config->apn);
    s_modem = esp_modem_new_dev(ESP_MODEM_DCE_GENERIC, &dte, &dce, s_ppp_netif);
    if (!s_modem) {
        modem_phase("modem_init_failed", ESP_FAIL);
        vTaskDelete(NULL);
        return;
    }
    bool power_key_used = false;
    for (;;) {
        modem_phase("checking_modem", ESP_OK);
        /* Detect an existing PPP session after an ESP-only reset before considering PWRKEY. */
        if (esp_modem_set_mode(s_modem, ESP_MODEM_MODE_DETECT) == ESP_OK &&
            esp_modem_get_mode(s_modem) == ESP_MODEM_MODE_DATA) {
            esp_modem_set_mode(s_modem, ESP_MODEM_MODE_COMMAND);
        }
        error = sync_modem();
        if (error != ESP_OK && !power_key_used) {
            power_key_used = true;
            modem_phase("powerkey_once", error);
            gpio_set_level(TB_LTE_PWRKEY, 0);
            gpio_set_direction(TB_LTE_PWRKEY, GPIO_MODE_OUTPUT);
            gpio_set_level(TB_LTE_PWRKEY, 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
            gpio_set_level(TB_LTE_PWRKEY, 0);
            vTaskDelay(pdMS_TO_TICKS(15000));
            error = sync_modem();
        }
        if (error != ESP_OK) {
            modem_phase("modem_no_response", error);
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        /* esp_modem 1.4 declares set_echo but does not export its C wrapper.
         * Use the supported raw AT C API; no response buffer is needed. */
        error = esp_modem_at(s_modem, "ATE0", NULL, 1000);
        if (error != ESP_OK) {
            modem_phase("echo_configuration_failed", error);
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        bool ready = false;
        error = esp_modem_read_pin(s_modem, &ready);
        portENTER_CRITICAL(&s_state_lock);
        s_state.sim_ready = error == ESP_OK && ready;
        portEXIT_CRITICAL(&s_state_lock);
        if (error != ESP_OK || !ready) {
            modem_phase("sim_not_ready_or_pin_required", error == ESP_OK ? ESP_ERR_INVALID_STATE : error);
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        int rssi = 99, ber = 99;
        if (esp_modem_get_signal_quality(s_modem, &rssi, &ber) == ESP_OK) {
            portENTER_CRITICAL(&s_state_lock);
            s_state.has_signal = rssi >= 0 && rssi <= 31;
            s_state.signal_dbm = -113 + rssi * 2;
            s_state.ber = ber;
            s_state.signal_us = esp_timer_get_time();
            portEXIT_CRITICAL(&s_state_lock);
        }
        error = esp_modem_set_flow_control(s_modem, 0, 0);
        if (error == ESP_OK) error = esp_modem_set_radio_state(s_modem, 1);
        if (error == ESP_OK) error = esp_modem_set_apn(s_modem, config->apn);
        if (error != ESP_OK) {
            modem_phase("modem_configuration_failed", error);
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        modem_phase("connecting_ppp", ESP_OK);
        xEventGroupClearBits(s_ppp_events, TB_PPP_CONNECTED | TB_PPP_DISCONNECTED);
        error = esp_modem_set_mode(s_modem, ESP_MODEM_MODE_DATA);
        if (error == ESP_OK) {
            EventBits_t bits = xEventGroupWaitBits(s_ppp_events, TB_PPP_CONNECTED | TB_PPP_DISCONNECTED,
                                                  pdFALSE, pdFALSE, pdMS_TO_TICKS(60000));
            if (!(bits & TB_PPP_CONNECTED)) error = ESP_ERR_TIMEOUT;
        }
        if (error == ESP_OK) {
            modem_phase("online", ESP_OK);
            xEventGroupWaitBits(s_ppp_events, TB_PPP_DISCONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);
        }
        modem_phase("ppp_reconnecting", error);
        esp_modem_set_mode(s_modem, ESP_MODEM_MODE_COMMAND);
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}

typedef struct {
    char data[4096];
    size_t used;
    int64_t started_us;
    bool overflow;
    bool deadline;
} http_capture_t;

static esp_err_t capture_http_event(esp_http_client_event_t *event)
{
    http_capture_t *capture = event->user_data;
    if (!capture) return ESP_OK;
    if (esp_timer_get_time() - capture->started_us > INT64_C(30000000)) {
        capture->deadline = true;
        return ESP_ERR_TIMEOUT;
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if ((size_t)event->data_len > sizeof(capture->data) - capture->used - 1) {
            capture->overflow = true;
            return ESP_ERR_INVALID_SIZE;
        }
        memcpy(capture->data + capture->used, event->data, event->data_len);
        capture->used += event->data_len;
        capture->data[capture->used] = 0;
    }
    return ESP_OK;
}

static char *result_json(bool ok, const char *error, double acknowledgement)
{
    cJSON *result = cJSON_CreateObject();
    if (!result) return NULL;
    cJSON_AddBoolToObject(result, "ok", ok);
    if (error) cJSON_AddStringToObject(result, "error", error);
    if (ok) cJSON_AddNumberToObject(result, "ack_seq", acknowledgement);
    char *json = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    return json;
}

static esp_err_t uplink_error(const char *reason, esp_err_t error, char **response)
{
    *response = result_json(false, reason, 0);
    portENTER_CRITICAL(&s_state_lock);
    s_state.uplink_failed++;
    strlcpy(s_state.uplink_error, reason, sizeof(s_state.uplink_error));
    portEXIT_CRITICAL(&s_state_lock);
    return error;
}

static bool safe_sequence(const cJSON *item, double *value)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 1 ||
        item->valuedouble > TB_JSON_SAFE_INTEGER || floor(item->valuedouble) != item->valuedouble) return false;
    *value = item->valuedouble;
    return true;
}

static esp_netif_t *uplink_netif(void)
{
    tb_network_status_t network;
    tb_network_status(&network);
    if (tb_network_wifi_uplink() && network.station_connected) {
        esp_netif_t *wifi = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (wifi) return wifi;
    }
    if (s_ppp_events && s_ppp_netif && (xEventGroupGetBits(s_ppp_events) & TB_PPP_CONNECTED))
        return s_ppp_netif;
    return NULL;
}

static esp_err_t https_post_json(const char *url, const char *payload, size_t length,
                                 http_capture_t *capture, int *status)
{
    if (strncmp(url, "https://", 8) != 0 || !url[8]) return ESP_ERR_INVALID_ARG;
    esp_netif_t *netif = uplink_netif();
    if (!netif) return ESP_ERR_INVALID_STATE;
    if (time(NULL) < 1704067200) return ESP_ERR_INVALID_STATE;
    struct ifreq interface = {0};
    if (esp_netif_get_netif_impl_name(netif, interface.ifr_name) != ESP_OK) return ESP_ERR_INVALID_STATE;
    capture->started_us = esp_timer_get_time();
    esp_http_client_config_t http = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .event_handler = capture_http_event,
        .user_data = capture,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .is_async = true,
        .if_name = &interface,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, (int)length);
    esp_err_t error;
    do {
        error = esp_http_client_perform(client);
        if (capture->overflow || capture->deadline ||
            esp_timer_get_time() - capture->started_us > INT64_C(30000000)) {
            if (!capture->overflow) capture->deadline = true;
            error = capture->overflow ? ESP_ERR_INVALID_SIZE : ESP_ERR_TIMEOUT;
            break;
        }
        if (error == ESP_ERR_HTTP_EAGAIN) vTaskDelay(pdMS_TO_TICKS(10));
    } while (error == ESP_ERR_HTTP_EAGAIN);
    *status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return error;
}

esp_err_t tb_link_uplink(const char *json, size_t length, char **response_json)
{
    if (!response_json) return ESP_ERR_INVALID_ARG;
    *response_json = NULL;
    if (!json || !length || length > TB_UPLINK_MAX) return uplink_error("invalid_batch_size", ESP_ERR_INVALID_SIZE, response_json);
    if (!s_uplink_lock || xSemaphoreTake(s_uplink_lock, 0) != pdTRUE)
        return uplink_error("uplink_busy", ESP_ERR_INVALID_STATE, response_json);
    esp_err_t error = ESP_OK;
    const char *reason = NULL;
    cJSON *batch = NULL, *backend = NULL, *envelope = NULL;
    http_capture_t *capture = NULL;
    char *post_payload = NULL;
    const tb_config_t *config = tb_config();
    double maximum_sequence = 0, acknowledgement = 0, offered_sequences[32];
    size_t offered_count = 0;
    if (!config->uplink_url[0] || !config->uplink_token[0]) {
        reason = "uplink_configuration_required"; error = ESP_ERR_INVALID_STATE; goto done;
    }
    if (strncmp(config->uplink_url, "https://", 8) != 0 || !config->uplink_url[8]) {
        reason = "https_endpoint_required"; error = ESP_ERR_INVALID_ARG; goto done;
    }
    if (!uplink_netif()) {
        reason = "wifi_or_cellular_not_connected"; error = ESP_ERR_INVALID_STATE; goto done;
    }
    if (time(NULL) < 1704067200) {
        reason = "tls_clock_not_synchronized"; error = ESP_ERR_INVALID_STATE; goto done;
    }
    batch = cJSON_ParseWithLength(json, length);
    const cJSON *device_uid = cJSON_GetObjectItemCaseSensitive(batch, "device_uid");
    const cJSON *samples = cJSON_GetObjectItemCaseSensitive(batch, "samples");
    int sample_count = cJSON_IsArray(samples) ? cJSON_GetArraySize(samples) : 0;
    if (!cJSON_IsObject(batch) || !cJSON_IsString(device_uid) || !device_uid->valuestring[0] ||
        strlen(device_uid->valuestring) > 64 || sample_count < 1 || sample_count > 32) {
        reason = "invalid_batch"; error = ESP_ERR_INVALID_ARG; goto done;
    }
    const cJSON *sample;
    cJSON_ArrayForEach(sample, samples) {
        double sequence;
        if (!cJSON_IsObject(sample) || !safe_sequence(cJSON_GetObjectItemCaseSensitive(sample, "seq"), &sequence) ||
            sequence <= maximum_sequence) {
            reason = "invalid_sequence"; error = ESP_ERR_INVALID_ARG; goto done;
        }
        maximum_sequence = sequence;
        offered_sequences[offered_count++] = sequence;
    }
    envelope = cJSON_CreateObject();
    if (!envelope) { reason = "out_of_memory"; error = ESP_ERR_NO_MEM; goto done; }
    cJSON_AddStringToObject(envelope, "device_uid", device_uid->valuestring);
    cJSON_AddStringToObject(envelope, "token", config->uplink_token);
    cJSON *copied_samples = cJSON_DetachItemFromObjectCaseSensitive(batch, "samples");
    if (!copied_samples) { reason = "out_of_memory"; error = ESP_ERR_NO_MEM; goto done; }
    cJSON_AddItemToObject(envelope, "samples", copied_samples);
    struct timeval utc;
    gettimeofday(&utc, NULL);
    cJSON_AddNumberToObject(envelope, "device_time_ms", (double)utc.tv_sec * 1000 + utc.tv_usec / 1000);
    const cJSON *uptime = cJSON_GetObjectItemCaseSensitive(batch, "device_uptime_ms");
    if (cJSON_IsNumber(uptime) && isfinite(uptime->valuedouble) && uptime->valuedouble >= 0)
        cJSON_AddNumberToObject(envelope, "device_uptime_ms", uptime->valuedouble);
    const cJSON *boot = cJSON_GetObjectItemCaseSensitive(batch, "boot_id");
    if (cJSON_IsNumber(boot) && isfinite(boot->valuedouble) && boot->valuedouble >= 0)
        cJSON_AddNumberToObject(envelope, "boot_id", boot->valuedouble);
    post_payload = cJSON_PrintUnformatted(envelope);
    if (!post_payload) { reason = "out_of_memory"; error = ESP_ERR_NO_MEM; goto done; }
    cJSON_Delete(envelope); envelope = NULL;
    cJSON_Delete(batch); batch = NULL;
    capture = calloc(1, sizeof(*capture));
    if (!capture) { reason = "out_of_memory"; error = ESP_ERR_NO_MEM; goto done; }
    int status = 0;
    error = https_post_json(config->uplink_url, post_payload, strlen(post_payload), capture, &status);
    free(post_payload); post_payload = NULL;
    portENTER_CRITICAL(&s_state_lock);
    s_state.last_http_status = status;
    portEXIT_CRITICAL(&s_state_lock);
    if (capture->overflow) { reason = "backend_response_too_large"; error = ESP_ERR_INVALID_SIZE; goto done; }
    if (capture->deadline) { reason = "backend_timeout"; error = ESP_ERR_TIMEOUT; goto done; }
    if (error != ESP_OK) { reason = "https_request_failed"; goto done; }
    if (status < 200 || status >= 300) { reason = "backend_rejected_batch"; error = ESP_FAIL; goto done; }
    backend = cJSON_ParseWithLength(capture->data, capture->used);
    if (!cJSON_IsObject(backend) || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(backend, "ok")) ||
        !safe_sequence(cJSON_GetObjectItemCaseSensitive(backend, "ack_seq"), &acknowledgement) ||
        acknowledgement > maximum_sequence) {
        reason = "backend_ack_required"; error = ESP_ERR_INVALID_RESPONSE; goto done;
    }
    bool offered = false;
    for (size_t i = 0; i < offered_count; ++i)
        if (offered_sequences[i] == acknowledgement) offered = true;
    if (!offered) { reason = "backend_ack_not_offered"; error = ESP_ERR_INVALID_RESPONSE; goto done; }
    *response_json = result_json(true, NULL, acknowledgement);
    if (!*response_json) { reason = "out_of_memory"; error = ESP_ERR_NO_MEM; goto done; }
    portENTER_CRITICAL(&s_state_lock);
    s_state.uplink_ok++;
    s_state.uplink_error[0] = 0;
    portEXIT_CRITICAL(&s_state_lock);
done:
    free(post_payload);
    free(capture);
    cJSON_Delete(envelope);
    cJSON_Delete(backend);
    cJSON_Delete(batch);
    if (reason) error = uplink_error(reason, error, response_json);
    xSemaphoreGive(s_uplink_lock);
    return error;
}

static void publish_outbox_state(const char *phase, int error)
{
    /* Called while the outbox mutex is held, or before either worker starts using it. */
    portENTER_CRITICAL(&s_state_lock);
    s_state.gps_storage_ready = s_gps_outbox.ready;
    s_state.gps_pending = tb_gps_outbox_pending(&s_gps_outbox);
    s_state.gps_storage_bytes = s_gps_outbox.file_bytes;
    s_state.gps_recovered_tail_bytes = s_gps_outbox.recovered_tail_bytes;
    s_state.gps_storage_error = error;
    strlcpy(s_state.gps_storage_phase, phase, sizeof(s_state.gps_storage_phase));
    portEXIT_CRITICAL(&s_state_lock);
}

static esp_err_t mount_gps_storage(void)
{
    const esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                               ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (!partition) return ESP_ERR_NOT_FOUND;
    bool completely_erased = true;
    uint8_t bytes[512];
    /* Do this before WL mount: mounting can itself initialize wear-level metadata. */
    for (size_t offset = 0; offset < partition->size; offset += sizeof(bytes)) {
        size_t count = partition->size - offset;
        if (count > sizeof(bytes)) count = sizeof(bytes);
        esp_err_t error = esp_partition_read(partition, offset, bytes, count);
        if (error != ESP_OK) return error;
        for (size_t i = 0; i < count; ++i) {
            if (bytes[i] != 0xff) { completely_erased = false; break; }
        }
        if (!completely_erased) break;
    }
    const esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = completely_erased,
        .max_files = 6,
        .allocation_unit_size = 4096,
    };
    return esp_vfs_fat_spiflash_mount_rw_wl("/gps", "storage", &mount, &s_gps_wl);
}

static char *gps_record(const tb_gnss_fix_t *fix)
{
    time_t seconds = (time_t)(fix->unix_ms / 1000);
    struct tm utc;
    if (!fix->utc_valid || !gmtime_r(&seconds, &utc)) return NULL;
    char base[24], timestamp[32], client_uid[96];
    if (!strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &utc)) return NULL;
    snprintf(timestamp, sizeof(timestamp), "%s.%03dZ", base, (int)(fix->unix_ms % 1000));
    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) return NULL;
    snprintf(client_uid, sizeof(client_uid), "gnss-%02x%02x%02x%02x%02x%02x:%" PRId64,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], fix->unix_ms);
    cJSON *record = cJSON_CreateObject();
    if (!record) return NULL;
    cJSON_AddStringToObject(record, "client_uid", client_uid);
    cJSON_AddStringToObject(record, "ts", timestamp);
    cJSON_AddStringToObject(record, "source", "TRUCKBOX_GNSS");
    cJSON_AddStringToObject(record, "provider", "ATGM336H");
    cJSON_AddNumberToObject(record, "lat", fix->latitude);
    cJSON_AddNumberToObject(record, "lon", fix->longitude);
    if (fix->has_speed) cJSON_AddNumberToObject(record, "speed_kmh", fix->speed_kmh);
    if (fix->has_heading) cJSON_AddNumberToObject(record, "heading_deg", fix->heading_deg);
    if (fix->has_altitude) cJSON_AddNumberToObject(record, "altitude_m", fix->altitude_m);
    /* HDOP is dimensionless, not an accuracy estimate in meters. Omit accuracy_m. */
    char *json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    return json;
}

static void gps_spool_task(void *arg)
{
    (void)arg;
    esp_err_t mount = mount_gps_storage();
    if (mount != ESP_OK) {
        publish_outbox_state("filesystem_unavailable", mount);
        ESP_LOGE(TAG, "GPS storage mount failed: %s; existing data retained", esp_err_to_name(mount));
        vTaskDelete(NULL);
        return;
    }
    xSemaphoreTake(s_gps_outbox_lock, portMAX_DELAY);
    int error = tb_gps_outbox_init(&s_gps_outbox, "/gps/outbox.log", 768 * 1024);
    publish_outbox_state(error ? "journal_fault" : "ready", error);
    xSemaphoreGive(s_gps_outbox_lock);
    if (error) { vTaskDelete(NULL); return; }
    int64_t last_spooled_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        link_state_t state = state_snapshot();
        if (!state.fix.valid || !state.fix.utc_valid || state.last_fix_us <= 0 ||
            esp_timer_get_time() - state.last_fix_us > TB_GNSS_FRESH_US ||
            state.fix.unix_ms == last_spooled_ms) continue;
        char *record = gps_record(&state.fix);
        if (!record) continue;
        xSemaphoreTake(s_gps_outbox_lock, portMAX_DELAY);
        error = tb_gps_outbox_append(&s_gps_outbox, record);
        publish_outbox_state(error == ENOSPC ? "queue_full" : error ? "journal_fault" : "ready", error);
        xSemaphoreGive(s_gps_outbox_lock);
        free(record);
        if (!error) last_spooled_ms = state.fix.unix_ms;
        else {
            portENTER_CRITICAL(&s_state_lock);
            s_state.gps_dropped++;
            portEXIT_CRITICAL(&s_state_lock);
        }
    }
}

static esp_err_t upload_gps_record(const char *record_json, const char **reason)
{
    const tb_config_t *config = tb_config();
    cJSON *record = cJSON_Parse(record_json);
    if (!cJSON_IsObject(record)) {
        cJSON_Delete(record);
        *reason = "gps_journal_invalid_json";
        return ESP_ERR_INVALID_RESPONSE;
    }
    const cJSON *timestamp = cJSON_GetObjectItemCaseSensitive(record, "ts");
    if (!cJSON_IsString(timestamp)) {
        cJSON_Delete(record);
        *reason = "gps_journal_missing_time";
        return ESP_ERR_INVALID_RESPONSE;
    }
    char expected_timestamp[40];
    strlcpy(expected_timestamp, timestamp->valuestring, sizeof(expected_timestamp));
    cJSON_AddStringToObject(record, "device_uid", config->gps_device_uid);
    cJSON_AddStringToObject(record, "token", config->gps_token);
    char *payload = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!payload) { *reason = "out_of_memory"; return ESP_ERR_NO_MEM; }
    http_capture_t *capture = calloc(1, sizeof(*capture));
    if (!capture) { free(payload); *reason = "out_of_memory"; return ESP_ERR_NO_MEM; }
    int status = 0;
    esp_err_t error = https_post_json(config->gps_url, payload, strlen(payload), capture, &status);
    free(payload);
    if (error != ESP_OK || capture->deadline || capture->overflow) {
        *reason = capture->overflow ? "gps_response_too_large" :
                  capture->deadline ? "gps_backend_timeout" : "gps_https_failed";
        if (error == ESP_OK) error = ESP_FAIL;
    } else if (status < 200 || status >= 300) {
        *reason = "gps_backend_rejected";
        error = ESP_FAIL;
    } else {
        cJSON *response = cJSON_ParseWithLength(capture->data, capture->used);
        const cJSON *accepted = cJSON_GetObjectItemCaseSensitive(response, "accepted_ts");
        if (!cJSON_IsObject(response) || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok")) ||
            !cJSON_IsString(accepted) || strcmp(accepted->valuestring, expected_timestamp) != 0) {
            *reason = "gps_backend_ack_required";
            error = ESP_ERR_INVALID_RESPONSE;
        }
        cJSON_Delete(response);
    }
    free(capture);
    return error;
}

static void gps_uploader_task(void *arg)
{
    (void)arg;
    unsigned backoff_seconds = 5;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const tb_config_t *config = tb_config();
        if (!config->gps_url[0] || !config->gps_device_uid[0] || !config->gps_token[0]) {
            portENTER_CRITICAL(&s_state_lock);
            strlcpy(s_state.gps_upload_error, "configure_gps_backend", sizeof(s_state.gps_upload_error));
            portEXIT_CRITICAL(&s_state_lock);
            continue;
        }
        if (tb_web_ota_busy() || !uplink_netif() || time(NULL) < 1704067200)
            continue;
        if (xSemaphoreTake(s_uplink_lock, 0) != pdTRUE) continue;
        char record[TB_GPS_OUTBOX_JSON_MAX];
        xSemaphoreTake(s_gps_outbox_lock, portMAX_DELAY);
        int queue_result = tb_gps_outbox_peek(&s_gps_outbox, record, sizeof(record));
        xSemaphoreGive(s_gps_outbox_lock);
        if (queue_result != 0) {
            xSemaphoreGive(s_uplink_lock);
            continue;
        }
        const char *reason = NULL;
        esp_err_t result = upload_gps_record(record, &reason);
        xSemaphoreTake(s_gps_outbox_lock, portMAX_DELAY);
        if (result == ESP_OK) {
            queue_result = tb_gps_outbox_ack(&s_gps_outbox);
            publish_outbox_state(queue_result ? "journal_fault" : "ready", queue_result);
        } else {
            tb_gps_outbox_retry(&s_gps_outbox);
        }
        xSemaphoreGive(s_gps_outbox_lock);
        xSemaphoreGive(s_uplink_lock);
        portENTER_CRITICAL(&s_state_lock);
        if (result == ESP_OK) {
            s_state.gps_upload_ok++;
            s_state.gps_upload_error[0] = 0;
        } else {
            s_state.gps_upload_failed++;
            strlcpy(s_state.gps_upload_error, reason ? reason : "gps_upload_failed", sizeof(s_state.gps_upload_error));
        }
        portEXIT_CRITICAL(&s_state_lock);
        if (result == ESP_OK) backoff_seconds = 5;
        else {
            vTaskDelay(pdMS_TO_TICKS(backoff_seconds * 1000));
            if (backoff_seconds < 300) backoff_seconds = backoff_seconds > 150 ? 300 : backoff_seconds * 2;
        }
    }
}

static esp_err_t uplink_handler(httpd_req_t *request)
{
    if (!tb_http_require_pair(request)) return ESP_OK;
    cJSON *batch = NULL;
    esp_err_t error = tb_http_read_json(request, TB_UPLINK_MAX, &batch);
    if (error != ESP_OK) return error;
    char *payload = cJSON_PrintUnformatted(batch);
    cJSON_Delete(batch);
    if (!payload) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "out_of_memory");
    char *response = NULL;
    error = tb_link_uplink(payload, strlen(payload), &response);
    free(payload);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (error != ESP_OK) httpd_resp_set_status(request, error == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "503 Service Unavailable");
    esp_err_t sent = httpd_resp_send(request, response ? response : "{\"ok\":false,\"error\":\"out_of_memory\"}", HTTPD_RESP_USE_STRLEN);
    free(response);
    return sent;
}

esp_err_t tb_application_register_routes(httpd_handle_t server)
{
    httpd_uri_t route = {.uri = "/api/uplink", .method = HTTP_POST, .handler = uplink_handler};
    return httpd_register_uri_handler(server, &route);
}

static void telemetry_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tb_network_status_t network;
        tb_network_status(&network);
        if (!network.station_connected || !tb_config()->pair_key[0] || tb_web_ota_busy()) continue;
        cJSON *record = cJSON_CreateObject();
        if (!record) continue;
        cJSON_AddStringToObject(record, "type", "link_status");
        cJSON_AddStringToObject(record, "device_uid", tb_device_uid());
        cJSON_AddNumberToObject(record, "uptime_us", (double)esp_timer_get_time());
        /* A compact transport record, not the verbose diagnostic response. */
        link_state_t state = state_snapshot();
        cJSON *gps = cJSON_AddObjectToObject(record, "gps");
        bool fresh = state.fix.valid && state.last_fix_us > 0 &&
                     esp_timer_get_time() - state.last_fix_us <= TB_GNSS_FRESH_US;
        cJSON_AddBoolToObject(gps, "valid", fresh);
        if (state.last_fix_us > 0)
            cJSON_AddNumberToObject(gps, "age_ms", (esp_timer_get_time() - state.last_fix_us) / 1000);
        if (fresh) {
            cJSON_AddNumberToObject(gps, "lat", state.fix.latitude);
            cJSON_AddNumberToObject(gps, "lon", state.fix.longitude);
            if (state.fix.has_speed) cJSON_AddNumberToObject(gps, "speed_kmh", state.fix.speed_kmh);
            if (state.fix.has_heading) cJSON_AddNumberToObject(gps, "bearing_deg", state.fix.heading_deg);
            if (state.fix.has_altitude) cJSON_AddNumberToObject(gps, "altitude_m", state.fix.altitude_m);
            if (state.fix.utc_valid) cJSON_AddNumberToObject(gps, "time_ms", (double)state.fix.unix_ms);
        }
        cJSON *cellular = cJSON_AddObjectToObject(record, "cellular");
        cJSON_AddBoolToObject(cellular, "enabled", tb_config()->modem_enabled);
        cJSON_AddBoolToObject(cellular, "ppp_connected", state.ppp_connected);
        cJSON_AddBoolToObject(cellular, "sim_ready", state.sim_ready);
        cJSON_AddStringToObject(cellular, "phase", state.modem_phase);
        cJSON_AddStringToObject(record, "uplink_transport", uplink_transport());
        if (state.has_signal) cJSON_AddNumberToObject(cellular, "signal_dbm", state.signal_dbm);
        char *payload = cJSON_PrintUnformatted(record);
        cJSON_Delete(record);
        if (!payload) continue;
        if (strlen(payload) >= TB_RECORD_MAX) {
            free(payload);
            portENTER_CRITICAL(&s_state_lock);
            s_state.telemetry_failed++;
            portEXIT_CRITICAL(&s_state_lock);
            continue;
        }
        char core_url[128];
        snprintf(core_url, sizeof(core_url), "http://%s/api/link/telemetry",
                 tb_network_wifi_uplink() ? tb_config()->core_host : TB_CORE_AP_IP);
        esp_http_client_config_t http = {.url = core_url,
                                          .method = HTTP_METHOD_POST, .timeout_ms = 3000,
                                          .disable_auto_redirect = true};
        esp_http_client_handle_t client = esp_http_client_init(&http);
        bool accepted = false;
        if (client) {
            char authorization[sizeof(tb_config()->pair_key) + 8];
            snprintf(authorization, sizeof(authorization), "Bearer %s", tb_config()->pair_key);
            esp_http_client_set_header(client, "Authorization", authorization);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, payload, (int)strlen(payload));
            esp_err_t result = esp_http_client_perform(client);
            accepted = result == ESP_OK && esp_http_client_get_status_code(client) == 200;
            esp_http_client_cleanup(client);
        }
        free(payload);
        portENTER_CRITICAL(&s_state_lock);
        if (accepted) s_state.telemetry_sent++;
        else s_state.telemetry_failed++;
        portEXIT_CRITICAL(&s_state_lock);
    }
}

esp_err_t tb_application_start(void)
{
    s_uplink_lock = xSemaphoreCreateMutex();
    s_gps_outbox_lock = xSemaphoreCreateMutex();
    s_ppp_events = xEventGroupCreate();
    if (!s_uplink_lock || !s_gps_outbox_lock || !s_ppp_events) return ESP_ERR_NO_MEM;
    modem_phase(tb_config()->modem_enabled ? "starting" : "disabled", ESP_OK);
    if (tb_config()->gnss_enabled && xTaskCreate(gnss_task, "tb_gnss", 4096, NULL, 5, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (tb_config()->gnss_enabled && xTaskCreate(gps_spool_task, "tb_gps_spool", 6144, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (tb_config()->gnss_enabled && xTaskCreate(gps_uploader_task, "tb_gps_upload", 8192, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (tb_config()->modem_enabled && xTaskCreate(modem_task, "tb_modem", 6144, NULL, 4, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(telemetry_task, "tb_link_report", 6144, NULL, 3, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void tb_application_add_json_compact(cJSON *object){tb_application_add_json(object);}
