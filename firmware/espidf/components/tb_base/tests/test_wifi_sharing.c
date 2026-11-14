#include <assert.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef int esp_err_t;
typedef int esp_event_base_t;
typedef struct { uint32_t addr; } ip4_t;
typedef struct { ip4_t ip,gw,netmask; } esp_netif_ip_info_t;
typedef struct { struct { int type; union { ip4_t ip4; } u_addr; } ip; } esp_netif_dns_info_t;
typedef struct { esp_netif_ip_info_t ip_info; } ip_event_got_ip_t;
typedef struct { esp_netif_ip_info_t info; } esp_netif_t;
#define ESP_OK 0
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED 2
#define ESP_IPADDR_TYPE_V4 0
#define ESP_NETIF_OP_SET 1
#define ESP_NETIF_DOMAIN_NAME_SERVER 6
#define ESP_NETIF_DNS_MAIN 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define TB_ROLE_CORE 0
#define TB_ROLE_LINK 1
#define WIFI_EVENT 10
#define IP_EVENT 11
#define WIFI_EVENT_AP_START 1
#define WIFI_EVENT_AP_STOP 2
#define WIFI_EVENT_STA_START 3
#define WIFI_EVENT_STA_DISCONNECTED 4
#define IP_EVENT_STA_GOT_IP 5
#define IP_EVENT_STA_LOST_IP 6
#define pdPASS 1
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((unsigned char *)&((p)->addr))[0],((unsigned char *)&((p)->addr))[1],((unsigned char *)&((p)->addr))[2],((unsigned char *)&((p)->addr))[3]
#define IP4_ADDR(p,a,b,c,d) ((p)->addr=htonl(((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d)))
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
static const char *TAG="test";
static esp_netif_t ap,sta;
static esp_netif_t *s_access_point=&ap,*s_station=&sta;
static struct { bool station_connected; char station_ip[16],hostname[40]; } s_status;
static int s_status_lock;
static bool s_subnet_conflict,s_ap_ready,s_napt_active,s_router_dns_fallback=true,s_retry_pending;
static uint32_t s_napt_uplink;
static unsigned s_retries;
static const char *s_router_state="WAITING_UPLINK";
static char s_router_dns[16];
static int role,napt_on,napt_off,dns_writes,dhcp_restarts;
static int napt_result,dns_result,upstream_dns_result;
static size_t free_heap=65536,largest_heap=32768;
static const char *ssid="TEST-STARLINK";
static esp_netif_dns_info_t upstream_dns;
static size_t test_strlcpy(char *to,const char *from,size_t n) {size_t len=strlen(from);if(n)snprintf(to,n,"%s",from);return len;}
#define strlcpy test_strlcpy
static int tb_role(void) {return role;}
static const char *station_ssid(void) {return ssid;}
static bool tb_network_wifi_uplink(void) {return true;}
static void tb_network_start_time_sync(void) {}
static const char *esp_err_to_name(int err) {(void)err;return "TEST";}
static int esp_netif_napt_enable(esp_netif_t *n) {assert(n==&ap);napt_on++;return napt_result;}
static int esp_netif_napt_disable(esp_netif_t *n) {assert(n==&ap);napt_off++;return 0;}
static int esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *i) {*i=n->info;return 0;}
static int esp_netif_get_dns_info(esp_netif_t *n,int type,esp_netif_dns_info_t *d) {assert(n==&sta);(void)type;*d=upstream_dns;return upstream_dns_result;}
static int esp_netif_dhcps_stop(esp_netif_t *n) {assert(n==&ap);return 0;}
static int esp_netif_dhcps_start(esp_netif_t *n) {assert(n==&ap);dhcp_restarts++;return 0;}
static int esp_netif_dhcps_option(esp_netif_t *n,int op,int option,void *v,size_t length) {assert(n==&ap&&op==ESP_NETIF_OP_SET&&option==ESP_NETIF_DOMAIN_NAME_SERVER&&length==1&&*(uint8_t *)v==2);return 0;}
static int esp_netif_set_dns_info(esp_netif_t *n,int type,const esp_netif_dns_info_t *d) {assert(n==&ap);(void)type;assert(d->ip.u_addr.ip4.addr);dns_writes++;return dns_result;}
static size_t heap_caps_get_free_size(uint32_t caps) {(void)caps;return free_heap;}
static size_t heap_caps_get_largest_free_block(uint32_t caps) {(void)caps;return largest_heap;}
static int esp_wifi_connect(void) {return 0;}
static void reconnect_task(void *p) {(void)p;}
static int xTaskCreate(void (*f)(void *),const char *n,int stack,void *arg,int priority,void *handle) {(void)f;(void)n;(void)stack;(void)arg;(void)priority;(void)handle;return pdPASS;}
#include "router_under_test.h"

static esp_netif_ip_info_t address(const char *ip,const char *mask,const char *gw) {
    esp_netif_ip_info_t r={{inet_addr(ip)},{inet_addr(gw)},{inet_addr(mask)}};return r;
}
static void reset(void) {
    role=TB_ROLE_CORE;ssid="TEST-STARLINK";s_napt_active=false;s_napt_uplink=0;s_ap_ready=true;
    s_status.station_connected=true;s_subnet_conflict=false;s_router_dns[0]=0;s_router_state="WAITING_UPLINK";
    napt_on=napt_off=dns_writes=dhcp_restarts=napt_result=dns_result=upstream_dns_result=0;
    free_heap=65536;largest_heap=32768;
    ap.info=address("192.168.4.1","255.255.255.0","192.168.4.1");
    sta.info=address("192.168.1.20","255.255.255.0","192.168.1.1");
    upstream_dns.ip.type=0;upstream_dns.ip.u_addr.ip4.addr=inet_addr("192.168.1.1");
}
int main(void) {
    reset();router_refresh();assert(s_napt_active&&napt_on==1&&dns_writes==1&&dhcp_restarts==1);
    assert(!strcmp(s_router_state,"NAPT_ACTIVE_NOT_VERIFIED"));assert(!strcmp(s_router_dns,"192.168.1.1"));assert(!s_router_dns_fallback);
    router_refresh();assert(napt_on==1&&dns_writes==1); /* DHCP renewal must not break current TCP flows. */
    network_event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(!s_napt_active&&napt_off==1&&!s_status.station_connected&&s_ap_ready);
    ip_event_got_ip_t got={sta.info};network_event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&got);assert(s_napt_active&&napt_on==2);
    network_event(NULL,IP_EVENT,IP_EVENT_STA_LOST_IP,NULL);assert(!s_napt_active&&napt_off==2&&!s_status.station_connected&&s_ap_ready);
    reset();role=TB_ROLE_LINK;router_refresh();assert(!napt_on&&!dns_writes); /* Link uplink stays unchanged. */
    reset();ssid="";router_refresh();assert(!napt_on&&!strcmp(s_router_state,"NO_UPLINK_CONFIGURED"));
    reset();sta.info.gw.addr=0;router_refresh();assert(!napt_on&&!strcmp(s_router_state,"NO_UPSTREAM_GATEWAY"));
    reset();sta.info=address("192.168.4.150","255.255.255.128","192.168.4.129");router_refresh();assert(!napt_on&&s_subnet_conflict);
    reset();sta.info=address("192.168.5.20","255.255.254.0","192.168.4.254");router_refresh();assert(!napt_on&&s_subnet_conflict);
    reset();upstream_dns_result=-1;router_refresh();assert(s_napt_active&&s_router_dns_fallback&&!strcmp(s_router_dns,"1.1.1.1"));
    reset();dns_result=-1;router_refresh();assert(!napt_on&&dhcp_restarts==1&&!strcmp(s_router_state,"DHCP_DNS_ERROR"));
    reset();free_heap=32767;router_refresh();assert(!napt_on&&!strcmp(s_router_state,"INSUFFICIENT_HEAP"));
    reset();largest_heap=8191;router_refresh();assert(!napt_on);
    reset();napt_result=-1;router_refresh();assert(!s_napt_active&&!strcmp(s_router_state,"NAPT_ENABLE_ERROR"));
    reset();router_refresh();sta.info.ip.addr=inet_addr("192.168.1.21");router_refresh();assert(napt_off==1&&napt_on==2&&s_napt_active);
    network_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_STOP,NULL);assert(!s_napt_active&&!s_ap_ready);
    reset();s_ap_ready=false;router_refresh();assert(!napt_on);network_event(NULL,WIFI_EVENT,WIFI_EVENT_AP_START,NULL);assert(napt_on==1);
    (void)esp_err_to_name(0);
    puts("Wi-Fi sharing: routing, DNS, overlap, disconnect/lost IP, recovery and RAM guards passed (host stubs; physical NAT pending).");
    return 0;
}
