#include "tb.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "dhcpserver/dhcpserver.h"
#include "mdns.h"

static const char *TAG = "tb_network";
static esp_netif_t *s_station;
static esp_netif_t *s_access_point;
static tb_network_status_t s_status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_subnet_conflict;
static bool s_sntp_initialized;
static bool s_sntp_started;
static unsigned s_retries;
static bool s_retry_pending;
static char s_link_address[16];
static int64_t s_link_seen_us;

bool tb_network_wifi_uplink(void)
{
    /* A legacy Link STA connects to Core's private AP, which has no Internet route. */
    return tb_role() == TB_ROLE_CORE || tb_config()->wifi_ssid[0] != '\0';
}

static const char *station_ssid(void)
{
    return tb_network_wifi_uplink() ? tb_config()->wifi_ssid : tb_config()->core_ssid;
}

void tb_network_start_time_sync(void)
{
    if (s_sntp_initialized && !s_sntp_started && esp_netif_sntp_start() == ESP_OK)
        s_sntp_started = true;
}

void tb_network_note_link_address(const char *address)
{
    if (tb_role() != TB_ROLE_CORE || !address) return;
    ip4_addr_t candidate;
    if (!ip4addr_aton(address, &candidate) || !ip4_addr_get_u32(&candidate)) return;
    bool local = false;
    esp_netif_t *interfaces[] = {s_station, s_access_point};
    for (size_t i = 0; i < 2; ++i) {
        esp_netif_ip_info_t own;
        if (interfaces[i] && esp_netif_get_ip_info(interfaces[i], &own) == ESP_OK && own.ip.addr &&
            candidate.addr != own.ip.addr &&
            (candidate.addr & own.netmask.addr) == (own.ip.addr & own.netmask.addr)) local = true;
    }
    if (!local) return;
    portENTER_CRITICAL(&s_status_lock);
    strlcpy(s_link_address, address, sizeof(s_link_address));
    s_link_seen_us = esp_timer_get_time();
    portEXIT_CRITICAL(&s_status_lock);
}

void tb_network_link_host(char *host, size_t capacity)
{
    if (!host || !capacity) return;
    portENTER_CRITICAL(&s_status_lock);
    const bool fresh = s_link_address[0] && esp_timer_get_time() - s_link_seen_us < INT64_C(60000000);
    strlcpy(host, fresh ? s_link_address : TB_LINK_IP, capacity);
    portEXIT_CRITICAL(&s_status_lock);
}

static void reconnect_task(void *unused)
{
    /* Reconnect outside the event callback and bound the rate on absent routers. */
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)unused));
    portENTER_CRITICAL(&s_status_lock);
    const bool already_connected = s_status.station_connected;
    s_retry_pending = false;
    portEXIT_CRITICAL(&s_status_lock);
    esp_err_t err = already_connected ? ESP_OK : esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "STA reconnect: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        const char *ssid = station_ssid();
        if (ssid[0]) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.station_connected = false;
        s_status.station_ip[0] = '\0';
        portEXIT_CRITICAL(&s_status_lock);
        ++s_retries;
        const char *ssid = station_ssid();
        portENTER_CRITICAL(&s_status_lock);
        bool schedule = ssid[0] && !s_retry_pending;
        if (schedule) s_retry_pending = true;
        portEXIT_CRITICAL(&s_status_lock);
        if (schedule && xTaskCreate(reconnect_task, "wifi_retry", 2048,
            (void *)(uintptr_t)(s_retries < 5 ? 2000 : 10000), 3, NULL) != pdPASS) {
            portENTER_CRITICAL(&s_status_lock);
            s_retry_pending = false;
            portEXIT_CRITICAL(&s_status_lock);
            ESP_LOGE(TAG, "Could not schedule reconnect; setup AP remains available");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        char address[16];
        snprintf(address, sizeof(address), IPSTR, IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&s_status_lock);
        s_status.station_connected = true;
        strlcpy(s_status.station_ip, address, sizeof(s_status.station_ip));
        portEXIT_CRITICAL(&s_status_lock);
        s_retries = 0;
        ESP_LOGI(TAG, "LAN: http://%s/ (%s.local)", address, s_status.hostname);
        {
            esp_netif_ip_info_t ap;
            if (esp_netif_get_ip_info(s_access_point, &ap) == ESP_OK) {
                s_subnet_conflict = ((ap.ip.addr & event->ip_info.netmask.addr)
                                    == (event->ip_info.ip.addr & event->ip_info.netmask.addr));
                if (s_subnet_conflict) {
                    ESP_LOGE(TAG, "Router overlaps this module's private AP subnet; use a different LAN subnet");
                }
            }
        }
        if (tb_network_wifi_uplink()) tb_network_start_time_sync();
    }
}

static esp_err_t configure_ap_address(bool core)
{
    esp_netif_ip_info_t ip = {0};
    IP4_ADDR(&ip.ip, 192, 168, core ? 4 : 5, 1);
    IP4_ADDR(&ip.gw, 192, 168, core ? 4 : 5, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    (void)esp_netif_dhcps_stop(s_access_point);
    esp_err_t err = esp_netif_set_ip_info(s_access_point, &ip);
    if (err != ESP_OK) {
        return err;
    }
    /* .2 is reserved for Link; a provisioning client must never receive it. */
    dhcps_lease_t lease = {.enable = true};
    IP4_ADDR(&lease.start_ip, 192, 168, core ? 4 : 5, 10);
    IP4_ADDR(&lease.end_ip, 192, 168, core ? 4 : 5, 30);
    err = esp_netif_dhcps_option(s_access_point, ESP_NETIF_OP_SET,
                                ESP_NETIF_REQUESTED_IP_ADDRESS, &lease, sizeof(lease));
    if (err != ESP_OK) {
        return err;
    }
    err = esp_netif_dhcps_start(s_access_point);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
        return err;
    }
    snprintf(s_status.ap_ip, sizeof(s_status.ap_ip), IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}

esp_err_t tb_network_start(void)
{
    const bool core = tb_role() == TB_ROLE_CORE;
    const tb_config_t *cfg = tb_config();
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    s_station = esp_netif_create_default_wifi_sta();
    s_access_point = esp_netif_create_default_wifi_ap();
    if (!s_station || !s_access_point) return ESP_ERR_NO_MEM;
    if (!core && !tb_network_wifi_uplink()) {
        /* LTE PPP has a higher route priority. Keep the private connected route. */
        (void)esp_netif_set_route_prio(s_station, 10);
        (void)esp_netif_dhcpc_stop(s_station);
        esp_netif_ip_info_t ip = {0};
        IP4_ADDR(&ip.ip, 192, 168, 4, 2);
        IP4_ADDR(&ip.gw, 192, 168, 4, 1);
        IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
        err = esp_netif_set_ip_info(s_station, &ip);
        if (err != ESP_OK) return err;
    }
    err = configure_ap_address(core);
    if (err != ESP_OK) return err;

    uint8_t mac[6];
    err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;
    snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "Truckbox-%s-%02X%02X",
             core ? "Core" : "Link", mac[4], mac[5]);
    if (core) {
        strlcpy(s_status.hostname, "truckbox", sizeof(s_status.hostname));
    } else {
        snprintf(s_status.hostname, sizeof(s_status.hostname),
                 "truckbox-link-%02x%02x", mac[4], mac[5]);
    }
    esp_netif_set_hostname(s_station, s_status.hostname);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, network_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK) return err;

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_status.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_status.ap_ssid);
    strlcpy((char *)ap.ap.password, cfg->ap_password, sizeof(ap.ap.password));
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.channel = 1;
    ap.ap.pmf_cfg.capable = true;
    ap.ap.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK) return err;

    wifi_config_t station = {0};
    const char *ssid = station_ssid();
    const char *password = tb_network_wifi_uplink() ? cfg->wifi_password : cfg->core_password;
    /* SSID may be exactly 32 bytes; copy with its explicit source length. */
    memcpy(station.sta.ssid, ssid, strnlen(ssid, sizeof(station.sta.ssid)));
    memcpy(station.sta.password, password,
           strnlen(password, sizeof(station.sta.password)));
    station.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    station.sta.pmf_cfg.capable = true;
    station.sta.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_STA, &station);
    if (err != ESP_OK) return err;
    {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        sntp.start = false;
        s_sntp_initialized = esp_netif_sntp_init(&sntp) == ESP_OK;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) return err;
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    s_status.access_point_running = true;
    ESP_LOGI(TAG, "Setup/internal AP: %s at http://%s/", s_status.ap_ssid, s_status.ap_ip);
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(s_status.hostname);
        mdns_instance_name_set(core ? "Truckbox Core B1" : "Truckbox Link B1");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    return ESP_OK;
}

void tb_network_status(tb_network_status_t *status)
{
    if (!status) return;
    portENTER_CRITICAL(&s_status_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    wifi_ap_record_t ap;
    if (status->station_connected && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        status->rssi = ap.rssi;
    }
}

void tb_network_add_json(cJSON *object)
{
    tb_network_status_t status;
    tb_network_status(&status);
    cJSON *network = cJSON_AddObjectToObject(object, "network");
    if (!network) return;
    cJSON_AddBoolToObject(network, "station_connected", status.station_connected);
    cJSON_AddStringToObject(network, "station_ip", status.station_ip);
    cJSON_AddStringToObject(network, "hostname", status.hostname);
    cJSON_AddBoolToObject(network, "access_point_running", status.access_point_running);
    cJSON_AddStringToObject(network, "ap_ssid", status.ap_ssid);
    cJSON_AddStringToObject(network, "ap_ip", status.ap_ip);
    cJSON_AddNumberToObject(network, "rssi", status.rssi);
    cJSON_AddBoolToObject(network, "subnet_conflict", s_subnet_conflict);
    cJSON_AddStringToObject(network, "station_mode", tb_network_wifi_uplink() ? "ROUTER" : "CORE_PRIVATE_AP");
    if (tb_role() == TB_ROLE_CORE) {
        char link_host[16];
        tb_network_link_host(link_host, sizeof(link_host));
        cJSON_AddStringToObject(network, "link_host", link_host);
    }
}
