/* Actual backup function; only partition/HTTP/RTOS boundaries are mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NO_MEM 0x101
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_FAT 2
#define pdTRUE 1
#define pdMS_TO_TICKS(v) (v)
typedef int esp_err_t;
typedef struct{uint32_t address,size;bool readonly;}esp_partition_t;
typedef struct{size_t bytes;bool complete;}httpd_req_t;
static esp_partition_t partition={0x620000,0x1e0000,false};
static struct{void *mutex;}state={(void *)1};
static unsigned held,reads,sends;static bool parked=true,busy,oom;static int fail_read=-1,fail_send=-1;
static const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label){
    assert(type==1 && subtype==2 && !strcmp(label,"storage"));return &partition;
}
static bool tb_capture_storage_stationary(void){return parked;}
static bool xSemaphoreTake(void *mutex,unsigned wait){assert(mutex && wait==200 && !held);if(busy)return false;held=1;return true;}
static void xSemaphoreGive(void *mutex){assert(mutex && held==1);held=0;}
static void vTaskDelay(unsigned ticks){assert(ticks==1 && held);}
static void httpd_resp_set_type(httpd_req_t *r,const char *type){(void)r;assert(!strcmp(type,"application/octet-stream"));}
static void httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value){(void)r;assert(name && value);}
static esp_err_t esp_partition_read(const esp_partition_t *p,size_t offset,void *out,size_t length){
    assert(held && p==&partition && p->address==0x620000 && p->size==0x1e0000);
    assert(length<=1024 && offset+length<=p->size);++reads;
    if(fail_read>=0 && offset>=(size_t)fail_read)return ESP_FAIL;
    for(size_t i=0;i<length;i++)((unsigned char *)out)[i]=(unsigned char)((offset+i)*37u);
    return ESP_OK;
}
static esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t length){
    assert(held);++sends;if(fail_send>=0 && r->bytes>=(size_t)fail_send)return ESP_FAIL;
    if(!p){assert(!length);r->complete=true;return ESP_OK;}
    for(size_t i=0;i<length;i++)assert((unsigned char)p[i]==(unsigned char)((r->bytes+i)*37u));
    r->bytes+=length;return ESP_OK;
}
static void *backup_malloc(size_t n){return oom?NULL:malloc(n);}
#define malloc backup_malloc
#include "backup_under_test.h"
#undef malloc
int main(void){
    httpd_req_t r={0};assert(tb_store_flash_backup(&r)==ESP_OK && r.bytes==0x1e0000 && r.complete && !held);
    unsigned before=reads;partition.address=0x9000;r=(httpd_req_t){0};assert(tb_store_flash_backup(&r)!=ESP_OK && !r.bytes && reads==before);partition.address=0x620000;
    partition.readonly=true;assert(tb_store_flash_backup(&r)!=ESP_OK);partition.readonly=false;
    parked=false;assert(tb_store_flash_backup(&r)!=ESP_OK);parked=true;
    busy=true;assert(tb_store_flash_backup(&r)==ESP_ERR_TIMEOUT);busy=false;
    oom=true;assert(tb_store_flash_backup(&r)==ESP_ERR_NO_MEM && !held);oom=false;
    fail_read=4096;r=(httpd_req_t){0};assert(tb_store_flash_backup(&r)!=ESP_OK && r.bytes==4096 && !r.complete && !held);fail_read=-1;
    fail_send=2048;r=(httpd_req_t){0};assert(tb_store_flash_backup(&r)!=ESP_OK && r.bytes==2048 && !r.complete && !held);
    puts("Raw backup: real bounded stream, exact storage/WL bytes, wrong partition/read-only/movement/mutex/OOM/read/send failure guards, released mutex, no writes PASS");return 0;
}
