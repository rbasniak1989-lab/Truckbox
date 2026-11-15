#include "tb.h"
#include "tb_network_contract.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
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
static bool s_ap_ready, s_napt_active;
static uint32_t s_napt_uplink;
static const char *s_router_state = "WAITING_UPLINK";
static char s_router_dns[16];
static bool s_router_dns_fallback = true;

bool tb_network_wifi_uplink(void)
{
    /* Preserve the legacy Link's existing PPP/Wi-Fi selection. */
    return tb_role() == TB_ROLE_CORE || tb_config()->wifi_ssid[0] != '\0';
}

static const char *station_ssid(void)
{
    return tb_network_wifi_uplink() ? tb_config()->wifi_ssid : tb_config()->core_ssid;
}

static bool subnets_overlap(const esp_netif_ip_info_t *a, const esp_netif_ip_info_t *b)
{
    /* Compare the shorter prefix: a /25 inside the AP's /24 also overlaps. */
    const uint32_t common = a->netmask.addr & b->netmask.addr;
    return a->ip.addr && b->ip.addr && common &&
        (a->ip.addr & common) == (b->ip.addr & common);
}

static void router_state(const char *reason)
{
    portENTER_CRITICAL(&s_status_lock);
    s_router_state = reason;
    portEXIT_CRITICAL(&s_status_lock);
}

static void router_disable(const char *reason)
{
    if (s_napt_active) {
        esp_err_t err = esp_netif_napt_disable(s_access_point);
        if (err != ESP_OK) ESP_LOGW(TAG, "NAPT disable: %s", esp_err_to_name(err));
    }
    portENTER_CRITICAL(&s_status_lock);
    s_napt_active = false;
    s_napt_uplink = 0;
    s_router_state = reason;
    portEXIT_CRITICAL(&s_status_lock);
}

static esp_err_t router_dns(const esp_netif_dns_info_t *dns, bool fallback)
{
    char address[16];
    snprintf(address, sizeof(address), IPSTR, IP2STR(&dns->ip.u_addr.ip4));
    if (!strcmp(address, s_router_dns)) {
        portENTER_CRITICAL(&s_status_lock);
        s_router_dns_fallback = fallback;
        portEXIT_CRITICAL(&s_status_lock);
        return ESP_OK;
    }
    /* DHCP only: no Wi-Fi restart, no AP kick, no NVS writes. */
    (void)esp_netif_dhcps_stop(s_access_point);
    uint8_t offer = 0x02; /* DHCPS_OFFER_DNS, as in ESP-IDF 5.5.2 softap_sta. */
    esp_err_t err = esp_netif_dhcps_option(s_access_point, ESP_NETIF_OP_SET,
        ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    if (err == ESP_OK) err = esp_netif_set_dns_info(s_access_point, ESP_NETIF_DNS_MAIN, dns);
    esp_err_t started = esp_netif_dhcps_start(s_access_point);
    if (started != ESP_OK && started != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) return started;
    if (err == ESP_OK) {
        portENTER_CRITICAL(&s_status_lock);
        strlcpy(s_router_dns, address, sizeof(s_router_dns));
        s_router_dns_fallback = fallback;
        portEXIT_CRITICAL(&s_status_lock);
    }
    return err;
}

static void router_refresh(void)
{
    if (tb_role() != TB_ROLE_CORE) return;
    if (!s_ap_ready) { router_disable("AP_NOT_READY"); return; }
    if (!station_ssid()[0]) { router_disable("NO_UPLINK_CONFIGURED"); return; }
    if (!s_status.station_connected) { router_disable("WAITING_UPLINK"); return; }
    esp_netif_ip_info_t ap = {0}, sta = {0};
    if (esp_netif_get_ip_info(s_access_point, &ap) != ESP_OK ||
        esp_netif_get_ip_info(s_station, &sta) != ESP_OK || !sta.ip.addr || !sta.gw.addr) {
        router_disable("NO_UPSTREAM_GATEWAY"); return;
    }
    const bool conflict = subnets_overlap(&ap, &sta);
    portENTER_CRITICAL(&s_status_lock);
    s_subnet_conflict = conflict;
    portEXIT_CRITICAL(&s_status_lock);
    if (conflict) { router_disable("SUBNET_CONFLICT"); return; }
    if (s_napt_active && s_napt_uplink != sta.ip.addr) router_disable("UPLINK_CHANGED");
    esp_netif_dns_info_t dns = {0};
    bool fallback = esp_netif_get_dns_info(s_station, ESP_NETIF_DNS_MAIN, &dns) != ESP_OK ||
        dns.ip.type != ESP_IPADDR_TYPE_V4 || !dns.ip.u_addr.ip4.addr;
    if (fallback) {
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        IP4_ADDR(&dns.ip.u_addr.ip4, 1, 1, 1, 1);
    }
    if (router_dns(&dns, fallback) != ESP_OK) { router_disable("DHCP_DNS_ERROR"); return; }
    if (s_napt_active) return;
    /* lwIP allocates/asserts internally. Keep ample headroom for capture/HTTP. */
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(caps) < 32768 || heap_caps_get_largest_free_block(caps) < 8192) {
        router_state("INSUFFICIENT_HEAP"); return;
    }
    const esp_err_t err = esp_netif_napt_enable(s_access_point);
    if (err != ESP_OK) {
        router_state("NAPT_ENABLE_ERROR");
        ESP_LOGW(TAG, "NAPT enable: %s", esp_err_to_name(err));
        return;
    }
    portENTER_CRITICAL(&s_status_lock);
    s_napt_active = true;
    s_napt_uplink = sta.ip.addr;
    s_router_state = "NAPT_ACTIVE_NOT_VERIFIED";
    portEXIT_CRITICAL(&s_status_lock);
    ESP_LOGI(TAG, "Core AP forwarding enabled; Internet reachability not verified");
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

void tb_network_note_link_socket(int socket_fd)
{
    /* IDF HTTPD uses AF_INET6 when IPv6 is enabled. lwIP then returns IPv4
     * clients as ::ffff:a.b.c.d; a sockaddr_in buffer cannot represent them. */
    struct sockaddr_storage peer = {0};
    socklen_t length = sizeof(peer);
    if (getpeername(socket_fd, (struct sockaddr *)&peer, &length) != 0) return;
    struct in_addr ipv4;
    if (peer.ss_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
        ipv4 = ((const struct sockaddr_in *)&peer)->sin_addr;
#if CONFIG_LWIP_IPV6
    } else if (peer.ss_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *v6 = (const struct sockaddr_in6 *)&peer;
        static const unsigned char mapped_prefix[12] = {0,0,0,0,0,0,0,0,0,0,255,255};
        if (memcmp(v6->sin6_addr.s6_addr, mapped_prefix, sizeof(mapped_prefix)) != 0) return;
        memcpy(&ipv4, v6->sin6_addr.s6_addr + 12, sizeof(ipv4));
#endif
    } else return;
    char address[16];
    if (inet_ntop(AF_INET, &ipv4, address, sizeof(address)))
        tb_network_note_link_address(address);
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
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        s_ap_ready = true;
        router_refresh();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STOP) {
        s_ap_ready = false;
        router_disable("AP_STOPPED");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.station_connected = false;
        s_status.station_ip[0] = '\0';
        portEXIT_CRITICAL(&s_status_lock);
        router_disable("UPLINK_IP_LOST");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        const char *ssid = station_ssid();
        if (ssid[0]) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&s_status_lock);
        s_status.station_connected = false;
        s_status.station_ip[0] = '\0';
        portEXIT_CRITICAL(&s_status_lock);
        router_disable("UPLINK_DISCONNECTED");
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
                s_subnet_conflict = subnets_overlap(&ap, &event->ip_info);
                if (s_subnet_conflict) {
                    ESP_LOGE(TAG, "Router overlaps this module's private AP subnet; use a different LAN subnet");
                }
            }
        }
        router_refresh();
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
    if (core) {
        /* A client joining before Starlink connects can resolve once the uplink returns.
         * DNS is replaced with the upstream DHCP DNS on GOT_IP. */
        uint8_t offer = 0x02;
        uint32_t lease_minutes = 1;
        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        IP4_ADDR(&dns.ip.u_addr.ip4, 1, 1, 1, 1);
        err = esp_netif_dhcps_option(s_access_point, ESP_NETIF_OP_SET,
            ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
        if (err == ESP_OK) err = esp_netif_set_dns_info(s_access_point, ESP_NETIF_DNS_MAIN, &dns);
        if (err == ESP_OK) err = esp_netif_dhcps_option(s_access_point, ESP_NETIF_OP_SET,
            ESP_NETIF_IP_ADDRESS_LEASE_TIME, &lease_minutes, sizeof(lease_minutes));
        if (err != ESP_OK) return err;
        strlcpy(s_router_dns, "1.1.1.1", sizeof(s_router_dns));
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
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, network_event, NULL);
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

size_t tb_network_health_fields(char *buffer,size_t capacity)
{
    char station_ip[16],ap_ip[16],dns[16];bool connected,ap_ready,active,conflict;const char *reason;
    portENTER_CRITICAL(&s_status_lock);
    connected=s_status.station_connected;ap_ready=s_status.access_point_running;
    active=s_napt_active;conflict=s_subnet_conflict;reason=s_router_state;
    memcpy(station_ip,s_status.station_ip,sizeof(station_ip));memcpy(ap_ip,s_status.ap_ip,sizeof(ap_ip));memcpy(dns,s_router_dns,sizeof(dns));
    portEXIT_CRITICAL(&s_status_lock);
    if(!buffer || !capacity)return 0;
    int n=snprintf(buffer,capacity,",\"network\":{\"station_connected\":%s,\"station_ip\":\"%.15s\",\"access_point_running\":%s,\"ap_ip\":\"%.15s\",\"subnet_conflict\":%s,\"internet_sharing\":{\"build_contract\":\"%s\",\"napt_active\":%s,\"internet_verified\":false,\"status\":\"%s\",\"dns_server\":\"%.15s\",\"max_connections\":64}}",
        connected?"true":"false",station_ip,ap_ready?"true":"false",ap_ip,conflict?"true":"false",TB_CORE_NETWORK_BUILD_CONTRACT,
        active?"true":"false",reason?reason:"WAITING_UPLINK",dns);
    if(n<=0 || (size_t)n>=capacity){buffer[0]=0;return 0;}return (size_t)n;
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
        bool active, fallback;
        const char *reason;
        char dns[16];
        portENTER_CRITICAL(&s_status_lock);
        active = s_napt_active;
        fallback = s_router_dns_fallback;
        reason = s_router_state;
        strlcpy(dns, s_router_dns, sizeof(dns));
        portEXIT_CRITICAL(&s_status_lock);
        cJSON *sharing = cJSON_AddObjectToObject(network, "internet_sharing");
        if (sharing) {
            cJSON_AddStringToObject(sharing,"build_contract",TB_CORE_NETWORK_BUILD_CONTRACT);
            cJSON_AddBoolToObject(sharing, "napt_active", active);
            cJSON_AddBoolToObject(sharing, "internet_verified", false);
            cJSON_AddStringToObject(sharing, "status", reason);
            cJSON_AddStringToObject(sharing, "dns_server", dns);
            cJSON_AddStringToObject(sharing, "dns_source", fallback ? "FALLBACK" : "UPSTREAM_DHCP");
            cJSON_AddNumberToObject(sharing, "max_connections", 64);
        }
        char link_host[16];
        tb_network_link_host(link_host, sizeof(link_host));
        cJSON_AddStringToObject(network, "link_host", link_host);
    }
}
