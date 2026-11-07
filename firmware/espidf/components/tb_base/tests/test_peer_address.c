/* Compiles the actual authenticated-peer address cache against two simulated interfaces. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
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
static bool loopback_interface;
static int tb_role(void){return TB_ROLE_CORE;}
static int64_t esp_timer_get_time(void){return now_us;}
static bool ip4addr_aton(const char *text,ip4_addr_t *out){return inet_pton(AF_INET,text,&out->addr)==1;}
static int esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *out)
{assert(inet_pton(AF_INET,*netif==1?(loopback_interface?"127.0.0.2":"192.168.68.105"):"192.168.4.1",&out->ip.addr)==1);
 assert(inet_pton(AF_INET,"255.255.255.0",&out->netmask.addr)==1);return ESP_OK;}
#include "peer_under_test.h"
static void expect(const char *address){char host[16];tb_network_link_host(host,sizeof(host));assert(!strcmp(host,address));}
static void real_socket_case(int server_family,int client_family)
{
 int server=socket(server_family,SOCK_STREAM,0);assert(server>=0);
 int client=socket(client_family,SOCK_STREAM,0);assert(client>=0);
 struct sockaddr_storage target={0};socklen_t target_size;
 if(server_family==AF_INET6){
  int v6only=0;assert(setsockopt(server,IPPROTO_IPV6,IPV6_V6ONLY,&v6only,sizeof(v6only))==0);
  struct sockaddr_in6 *s=(struct sockaddr_in6 *)&target;s->sin6_family=AF_INET6;
  assert(bind(server,(struct sockaddr *)s,sizeof(*s))==0);target_size=sizeof(*s);
 }else{
  struct sockaddr_in *s=(struct sockaddr_in *)&target;s->sin_family=AF_INET;
  s->sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  assert(bind(server,(struct sockaddr *)s,sizeof(*s))==0);target_size=sizeof(*s);
 }
 assert(getsockname(server,(struct sockaddr *)&target,&target_size)==0);
 assert(listen(server,1)==0);
 if(client_family==AF_INET){
  struct sockaddr_in to={.sin_family=AF_INET,.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
  to.sin_port=server_family==AF_INET6?((struct sockaddr_in6 *)&target)->sin6_port:((struct sockaddr_in *)&target)->sin_port;
  assert(connect(client,(struct sockaddr *)&to,sizeof(to))==0);
 }else{
  struct sockaddr_in6 to={.sin6_family=AF_INET6,.sin6_addr=IN6ADDR_LOOPBACK_INIT,
                        .sin6_port=((struct sockaddr_in6 *)&target)->sin6_port};
  assert(connect(client,(struct sockaddr *)&to,sizeof(to))==0);
 }
 int accepted=accept(server,NULL,NULL);assert(accepted>=0);
 struct sockaddr_in old_buffer={0};socklen_t old_size=sizeof(old_buffer);
 assert(getpeername(accepted,(struct sockaddr *)&old_buffer,&old_size)==0);
 if(server_family==AF_INET6&&client_family==AF_INET)
  assert(old_buffer.sin_family==AF_INET6); /* Reproduces the rejected v0.2.2 path. */
 s_link_address[0]=0;
 tb_network_note_link_socket(accepted);
 expect(client_family==AF_INET?"127.0.0.1":TB_LINK_IP);
 close(accepted);close(client);close(server);
}
int main(void)
{
 expect(TB_LINK_IP);
 tb_network_note_link_address("192.168.68.20");expect("192.168.68.20");
 const char *invalid[]={"192.168.68.105","192.168.4.1","127.0.0.1","203.0.113.3","0.0.0.0","example.com","bad/address"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){tb_network_note_link_address(invalid[i]);expect("192.168.68.20");}
 now_us+=59999999;expect("192.168.68.20");++now_us;expect(TB_LINK_IP);
 tb_network_note_link_address("192.168.4.2");expect(TB_LINK_IP);
 loopback_interface=true;
 real_socket_case(AF_INET6,AF_INET);
 real_socket_case(AF_INET,AF_INET);
 real_socket_case(AF_INET6,AF_INET6);
 tb_network_note_link_socket(-1);expect(TB_LINK_IP);
 puts("Network peer cache: same LAN/private AP, own/foreign address rejection, DHCP change and stale fallback passed");
 puts("Real sockets: v0.2.2 IPv4-on-IPv6 regression reproduced; mapped IPv4 and native IPv4 learned; native IPv6/invalid FD rejected");
}
