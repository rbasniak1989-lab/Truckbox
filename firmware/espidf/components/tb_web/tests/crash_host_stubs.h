#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_COREDUMP 3
#define HTTPD_RESP_USE_STRLEN -1
typedef int esp_err_t;
typedef struct {uint32_t address,size;}esp_partition_t;
typedef struct {unsigned char bytes[60000];size_t used,max_chunk;char status[40];bool finished;}httpd_req_t;
static esp_partition_t partition={0x12000,0xe000};
static unsigned char flash_bytes[0xe000];
static bool admin=true,busy,partition_present=true;
static int image_error,check_error,read_error;
static unsigned flash_reads,check_calls,reason_calls;
static size_t image_address=0x12000,image_length=3000;
static bool tb_http_require_admin(httpd_req_t *r){if(!admin)strcpy(r->status,"401 Unauthorized");return admin;}
static bool tb_web_ota_busy(void){return busy;}
static unsigned tb_boot_id(void){return 553;}
static const esp_partition_t *esp_partition_find_first(int type,int sub,const char *label){assert(type==1 && sub==3 && !label);return partition_present?&partition:NULL;}
static esp_err_t esp_core_dump_image_get(size_t *a,size_t *n){*a=image_address;*n=image_length;return image_error;}
static esp_err_t esp_core_dump_image_check(void){++check_calls;return check_error;}
static esp_err_t esp_core_dump_get_panic_reason(char *p,size_t n){++reason_calls;snprintf(p,n,"assert: \"measured reason\"");return ESP_OK;}
static esp_err_t esp_partition_read(const esp_partition_t *p,size_t off,void *out,size_t n){
    assert(p==&partition && off+n<=p->size);++flash_reads;
    if(read_error)return ESP_FAIL;
    memcpy(out,flash_bytes+off,n);return ESP_OK;
}
static void httpd_resp_set_status(httpd_req_t *r,const char *s){snprintf(r->status,sizeof(r->status),"%s",s);}
static void httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;}
static esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t n){
    assert(n<=512 && r->used+n<=sizeof(r->bytes));
    if(n){memcpy(r->bytes+r->used,p,n);r->used+=n;if(n>r->max_chunk)r->max_chunk=n;}else r->finished=true;return ESP_OK;
}
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *p,int n){
    if(n<0)n=(int)strlen(p);
    assert(r->used+(size_t)n<sizeof(r->bytes));memcpy(r->bytes+r->used,p,(size_t)n);r->used+=(size_t)n;r->finished=true;return ESP_OK;
}
static esp_err_t tb_http_send_json(httpd_req_t *r,cJSON *o){
    char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);if(!s)return ESP_FAIL;
    esp_err_t result=httpd_resp_send(r,s,-1);cJSON_free(s);return result;
}
