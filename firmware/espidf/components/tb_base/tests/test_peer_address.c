/* Compiles the actual authenticated-peer address cache against two simulated interfaces. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
typedef struct {uint32_t addr;} ip4_addr_t;
typedef struct {ip4_addr_t ip,netmask;} esp_netif_ip_info_t;
typedef int esp_netif_t;
#define TB_ROLE_CORE 0
#define TB_LINK_IP "192.168.4.2"
#define ESP_OK 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define ip4_addr_get_u32(p) ((p)->addr)
static esp_netif_t station=1,ap=2,*s_station=&station,*s_access_point=&ap;
static int s_status_lock;
static char s_link_address[16];
static int64_t s_link_seen_us,now_us=100000000;
static int tb_role(void){return TB_ROLE_CORE;}
static int64_t esp_timer_get_time(void){return now_us;}
static bool ip4addr_aton(const char *text,ip4_addr_t *out){return inet_pton(AF_INET,text,&out->addr)==1;}
static int esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *out)
{assert(inet_pton(AF_INET,*netif==1?"192.168.68.105":"192.168.4.1",&out->ip.addr)==1);
 assert(inet_pton(AF_INET,"255.255.255.0",&out->netmask.addr)==1);return ESP_OK;}
#include "peer_under_test.h"
static void expect(const char *address){char host[16];tb_network_link_host(host,sizeof(host));assert(!strcmp(host,address));}
int main(void)
{
 expect(TB_LINK_IP);
 tb_network_note_link_address("192.168.68.20");expect("192.168.68.20");
 const char *invalid[]={"192.168.68.105","192.168.4.1","127.0.0.1","203.0.113.3","0.0.0.0","example.com","bad/address"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){tb_network_note_link_address(invalid[i]);expect("192.168.68.20");}
 now_us+=59999999;expect("192.168.68.20");++now_us;expect(TB_LINK_IP);
 tb_network_note_link_address("192.168.4.2");expect(TB_LINK_IP);
 puts("Network peer cache: same LAN/private AP, own/foreign address rejection, DHCP change and stale fallback passed");
}
