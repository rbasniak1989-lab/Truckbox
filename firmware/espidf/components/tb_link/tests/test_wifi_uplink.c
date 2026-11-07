/* Executes the real Link uploader/HTTPS setup with only hardware/HTTP boundaries mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <net/if.h>
#include "cJSON.h"

typedef int esp_err_t;
typedef int esp_netif_t;
typedef struct { bool station_connected; } tb_network_status_t;
typedef struct { char uplink_url[257],uplink_token[129]; bool modem_enabled; char apn[65]; } tb_config_t;
typedef struct {int event_id,data_len;void *data,*user_data;} esp_http_client_event_t;
typedef struct {
 const char *url;int method,timeout_ms;void *crt_bundle_attach;bool disable_auto_redirect;
 esp_err_t (*event_handler)(esp_http_client_event_t *);void *user_data;
 int buffer_size,buffer_size_tx;bool is_async;struct ifreq *if_name;
} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_SIZE 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_NO_MEM 5
#define ESP_ERR_HTTP_EAGAIN 6
#define ESP_ERR_INVALID_RESPONSE 7
#define HTTP_EVENT_ON_DATA 1
#define HTTP_METHOD_POST 1
#define TB_UPLINK_MAX (32*1024)
#define TB_JSON_SAFE_INTEGER 9007199254740991.0
#define TB_PPP_CONNECTED 1
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define esp_crt_bundle_attach ((void *)1)

static int s_state_lock,s_uplink_lock=1,s_ppp_events=1;
static esp_netif_t wifi_netif=1,ppp_netif=2;
static esp_netif_t *s_ppp_netif=&ppp_netif;
static struct {unsigned uplink_ok,uplink_failed;char uplink_error[48];int last_http_status;} s_state;
static tb_config_t cfg;
static bool wifi_configured,station_connected,ppp_connected;
static int http_status=200,http_calls,host_interface;
static const char *http_response;
static esp_http_client_config_t active_http;
static const char *posted_payload;
static size_t live_json_allocations;
static void *json_pointers[4096];

static void *tracked_malloc(size_t n)
{void *p=malloc(n);if(p){size_t i=0;while(i<4096&&json_pointers[i])++i;assert(i<4096);json_pointers[i]=p;++live_json_allocations;}return p;}
static void tracked_free(void *p)
{if(p){size_t i=0;while(i<4096&&json_pointers[i]!=p)++i;assert(i<4096&&live_json_allocations);json_pointers[i]=NULL;--live_json_allocations;free(p);}}
static void host_free(void *p)
{for(size_t i=0;i<4096;++i)if(p&&json_pointers[i]==p){tracked_free(p);return;}free(p);}
static size_t host_strlcpy(char *dst,const char *src,size_t capacity)
{size_t n=strlen(src);if(capacity){size_t k=n<capacity-1?n:capacity-1;memcpy(dst,src,k);dst[k]=0;}return n;}
static const tb_config_t *tb_config(void){return &cfg;}
static bool tb_network_wifi_uplink(void){return wifi_configured;}
static void tb_network_status(tb_network_status_t *n){n->station_connected=station_connected;}
static esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{assert(!strcmp(key,"WIFI_STA_DEF"));return &wifi_netif;}
static int xEventGroupGetBits(int group){assert(group);return ppp_connected?TB_PPP_CONNECTED:0;}
static bool xSemaphoreTake(int lock,unsigned ticks){(void)ticks;return lock!=0;}
static void xSemaphoreGive(int lock){assert(lock);}
static int64_t esp_timer_get_time(void){return 1000000;}
static void vTaskDelay(unsigned ticks){(void)ticks;}
static esp_err_t esp_netif_get_netif_impl_name(esp_netif_t *netif,char *name)
{host_interface=*netif;strcpy(name,*netif==1?"st1":"pp1");return ESP_OK;}
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *http)
{assert(http->crt_bundle_attach&&http->disable_auto_redirect&&http->if_name);active_http=*http;return (void *)1;}
static void esp_http_client_set_header(void *client,const char *name,const char *value)
{assert(client&&name&&value);}
static void esp_http_client_set_post_field(void *client,const char *data,int size)
{assert(client&&size==(int)strlen(data));posted_payload=data;}
static esp_err_t esp_http_client_perform(void *client)
{
 assert(client);++http_calls;
 /* Both input/envelope JSON trees must have been freed before a potentially slow TLS request. */
 assert(live_json_allocations==1);
 cJSON *post=cJSON_Parse(posted_payload);assert(post);
 assert(!cJSON_GetObjectItemCaseSensitive(post,"lat"));
 const cJSON *rows=cJSON_GetObjectItemCaseSensitive(post,"samples");assert(cJSON_GetArraySize(rows)==2);
 assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(rows,0),"seq")->valuedouble==10);
 cJSON_Delete(post);
 esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_DATA,.data=(void *)http_response,
   .data_len=(int)strlen(http_response),.user_data=active_http.user_data};
 return active_http.event_handler(&event);
}
static int esp_http_client_get_status_code(void *client){assert(client);return http_status;}
static void esp_http_client_cleanup(void *client){assert(client);}

#define free host_free
#define strlcpy host_strlcpy
#include "uplink_under_test.h"
#undef free
#undef strlcpy

static void invoke(bool expected_success,const char *expected_error)
{
 const char *batch="{\"device_uid\":\"test-core\",\"boot_id\":2,\"device_uptime_ms\":4000,\"samples\":[{\"seq\":10,\"sample_kind\":\"RAW\",\"time_valid\":false},{\"seq\":20,\"sample_kind\":\"DECODED\",\"readings\":[]}]}";
 char *response=NULL;
 esp_err_t result=tb_link_uplink(batch,strlen(batch),&response);
 assert((result==ESP_OK)==expected_success&&response);
 cJSON *parsed=cJSON_Parse(response);assert(parsed);
 assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(parsed,"ok"))==expected_success);
 if(expected_error){assert(!strcmp(cJSON_GetObjectItemCaseSensitive(parsed,"error")->valuestring,expected_error));
   assert(!cJSON_GetObjectItemCaseSensitive(parsed,"ack_seq"));}
 cJSON_Delete(parsed);cJSON_free(response);assert(live_json_allocations==0);
}

int main(void)
{
 cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);
 strcpy(cfg.uplink_url,"https://test.invalid/ingest");strcpy(cfg.uplink_token,"test-only-token");
 /* Wi-Fi upload must work while modem_enabled=false, APN empty and PPP disconnected. */
 wifi_configured=true;station_connected=true;
 http_response="{\"ok\":true,\"ack_seq\":20}";invoke(true,NULL);assert(host_interface==1);
 /* Sequence 15 is below the maximum but was never offered: no success/ACK. */
 http_response="{\"ok\":true,\"ack_seq\":15}";invoke(false,"backend_ack_not_offered");
 http_response="{\"ok\":true,\"ack_seq\":30}";invoke(false,"backend_ack_required");
 http_response="{\"ok\":true,\"ack_seq\":0}";invoke(false,"backend_ack_required");
 http_response="{\"ok\":true}";invoke(false,"backend_ack_required");
 http_response="{\"ok\":true,\"ack_seq\":10}";invoke(true,NULL);
 http_status=503;invoke(false,"backend_rejected_batch");http_status=200;
 /* A private STA link is not a Wi-Fi Internet connection. Legacy LTE remains usable. */
 wifi_configured=false;invoke(false,"wifi_or_cellular_not_connected");
 ppp_connected=true;invoke(true,NULL);assert(host_interface==2);
 ppp_connected=false;station_connected=false;invoke(false,"wifi_or_cellular_not_connected");
 assert(http_calls==8);
 puts("Link Wi-Fi: LTE disabled, TLS interface binding, raw/no-GPS forwarding, exact offered ACK, failure retention and bounded JSON lifetime passed");
 return 0;
}
