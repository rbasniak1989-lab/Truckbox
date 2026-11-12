#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "tb_json.h"
#include "../../tb_base/tests/json_heap.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define MALLOC_CAP_8BIT 1
typedef int esp_err_t;
typedef struct {char response[24000];size_t used,chunks,largest_chunk,fail_chunk;bool finished;const char *query,*status;}httpd_req_t;
static const char *tb_role_name(void){return "core";}
static const char *tb_device_uid(void){return "synthetic-device";}
static const char *tb_version(void){return "0.2.13";}
static int64_t esp_timer_get_time(void){return 123456789;}
static unsigned free_bytes=41596;
static unsigned esp_get_free_heap_size(void){return free_bytes;}
static uint32_t tb_boot_id(void){return 308;}
static unsigned esp_get_minimum_free_heap_size(void){return 17848;}
static size_t heap_caps_get_largest_free_block(int cap){(void)cap;return 16000;}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){(void)r;(void)key;(void)value;}
static void httpd_resp_set_type(httpd_req_t *r,const char *type){(void)r;assert(strstr(type,"application/json"));}
static void httpd_resp_set_status(httpd_req_t *r,const char *status){r->status=status;}
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *bytes,size_t n)
{assert(n<sizeof(r->response)-r->used);memcpy(r->response+r->used,bytes,n);r->used+=n;r->response[r->used]=0;return ESP_OK;}
static esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *bytes,size_t n)
{
    if(++r->chunks==r->fail_chunk)return ESP_FAIL;
    if(n>r->largest_chunk)r->largest_chunk=n;
    if(!n){r->finished=true;return ESP_OK;}
    return httpd_resp_send(r,bytes,n);
}
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *buffer,size_t n)
{if(!r->query || strlen(r->query)>=n)return ESP_FAIL;strcpy(buffer,r->query);return ESP_OK;}
static esp_err_t httpd_query_key_value(const char *q,const char *key,char *buffer,size_t n)
{if(strcmp(key,"compact") || (strcmp(q,"compact=1") && strcmp(q,"compact=0")) || n<2)return ESP_FAIL;strcpy(buffer,q+8);return ESP_OK;}
static void tb_base_add_json(cJSON *o){cJSON_AddStringToObject(o,"role","core");}
static void tb_network_add_json(cJSON *o){cJSON_AddObjectToObject(o,"network");}
static void tb_application_add_json(cJSON *o){cJSON_AddStringToObject(o,"mode","full");}
static void tb_application_add_json_compact(cJSON *o){cJSON_AddStringToObject(o,"mode","compact");}
static bool tb_web_ota_busy(void){return false;}
#include "http_json.h"
static cJSON *fixture(void)
{
    cJSON *o=cJSON_CreateObject(),*a=cJSON_AddArrayToObject(o,"values");assert(a);
    for(unsigned i=0;i<220;i++)assert(tb_json_add_array(a,cJSON_CreateString("Ação UTF-8 \\ \" synthetic measurements: [1,2,null]")));
    return o;
}
static void chunked_equivalence_and_disconnect(void)
{
    heap_reset();cJSON *o=fixture();char *expected=cJSON_PrintUnformatted(o);assert(expected);
    httpd_req_t r={0};size_t calls=heap_calls;heap_budget=heap_used;
    assert(tb_http_send_json(&r,o)==ESP_OK);assert(heap_calls==calls && r.finished && r.largest_chunk<=1024 && r.chunks>2);
    assert(!strcmp(r.response,expected));cJSON_free(expected);assert(!heap_used);
    size_t chunks=r.chunks;
    for(size_t fail=1;fail<=chunks;fail++){
        heap_reset();r=(httpd_req_t){.fail_chunk=fail};o=fixture();calls=heap_calls;
        assert(tb_http_send_json(&r,o)==ESP_FAIL);assert(!heap_used && calls==heap_calls);
    }
    puts("Production HTTP: chunked wire equivalence, maximum 1024-byte staging, zero serializer allocations and all client disconnect points free the tree");
}
static void health_and_state(void)
{
    heap_reset();httpd_req_t r={0};heap_budget=0;assert(health_handler(&r)==ESP_OK && !heap_calls);
    heap_budget=SIZE_MAX;cJSON *decoded=cJSON_Parse(r.response);assert(decoded);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(decoded,"ok")));
    assert(cJSON_GetObjectItemCaseSensitive(decoded,"heap_largest_block")->valueint==16000);cJSON_Delete(decoded);assert(!heap_used);
    heap_reset();r=(httpd_req_t){0};heap_budget=0;assert(state_handler(&r)==ESP_OK);
    assert(r.status && !strcmp(r.status,"503 Service Unavailable") && strstr(r.response,"RAM") && strstr(r.response,"diagnostic_partial"));assert(!heap_used);
    heap_reset();r=(httpd_req_t){.query="compact=1"};assert(state_handler(&r)==ESP_OK);
    assert(strstr(r.response,"\"mode\":\"compact\"") && strstr(r.response,"\"diagnostic_compact\":true") && !heap_used);
    r=(httpd_req_t){0};assert(state_handler(&r)==ESP_OK && strstr(r.response,"\"mode\":\"compact\""));assert(!heap_used);
    r=(httpd_req_t){.query="compact=0"};assert(state_handler(&r)==ESP_OK && strstr(r.response,"\"full_diagnostic_deferred\":true"));assert(!heap_used);
    free_bytes=128*1024;r=(httpd_req_t){.query="compact=0"};assert(state_handler(&r)==ESP_OK && strstr(r.response,"\"mode\":\"full\""));assert(!heap_used);
    puts("Production HTTP health remains available without cJSON allocations; state OOM returns RAM metadata; full endpoint and opt-in compact endpoint preserved");
}
int main(void){chunked_equivalence_and_disconnect();health_and_state();return 0;}
