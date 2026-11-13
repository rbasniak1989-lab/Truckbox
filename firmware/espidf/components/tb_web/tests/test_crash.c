#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TB_CRASH_HOST_TEST
#include "../tb_crash.c"
static size_t allocations,fail_allocation;
static void *test_malloc(size_t n){++allocations;if(allocations==fail_allocation)return NULL;return malloc(n);}
static httpd_req_t response;
static cJSON *info(void){
    memset(&response,0,sizeof(response));assert(tb_crash_info_handler(&response)==ESP_OK);
    cJSON *o=cJSON_Parse((const char *)response.bytes);assert(o);return o;
}
int main(void){
    for(size_t i=0;i<sizeof(flash_bytes);i++)flash_bytes[i]=(unsigned char)(i*19);
    admin=false;assert(tb_crash_download_handler(&response)==ESP_OK && !flash_reads && !response.used);
    assert(tb_crash_info_handler(&response)==ESP_OK && !check_calls);
    admin=true;busy=true;memset(&response,0,sizeof(response));assert(tb_crash_download_handler(&response)==ESP_OK && !flash_reads && strstr(response.status,"409"));
    busy=false;cJSON *o=info();assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"image_valid")));
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(o,"dump_boot_id")));
    assert(strstr(cJSON_GetObjectItemCaseSensitive(o,"panic_reason")->valuestring,"measured reason"));cJSON_Delete(o);
    memset(&response,0,sizeof(response));assert(tb_crash_download_handler(&response)==ESP_OK);
    assert(response.used==partition.size && response.max_chunk==512 && response.finished && !memcmp(response.bytes,flash_bytes,partition.size));
    check_error=ESP_FAIL;unsigned old=reason_calls;o=info();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"image_valid")) && reason_calls==old);cJSON_Delete(o);
    image_address=0x9000;old=check_calls;o=info();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"image_present")) && check_calls==old);cJSON_Delete(o);
    image_address=partition.address;image_length=partition.size+1;o=info();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"image_present")));cJSON_Delete(o);
    image_error=ESP_ERR_NOT_FOUND;o=info();assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"image_present")));cJSON_Delete(o);
    partition_present=false;memset(&response,0,sizeof(response));assert(tb_crash_download_handler(&response)==ESP_OK && strstr(response.status,"404"));
    partition_present=true;read_error=ESP_FAIL;memset(&response,0,sizeof(response));assert(tb_crash_download_handler(&response)==ESP_FAIL && !response.finished);
    read_error=check_error=image_error=0;image_length=3000;
    cJSON_Hooks hooks={test_malloc,free};cJSON_InitHooks(&hooks);allocations=0;o=info();cJSON_Delete(o);size_t total=allocations;
    for(size_t fail=1;fail<=total;fail++){
        allocations=0;fail_allocation=fail;memset(&response,0,sizeof(response));
        esp_err_t result=tb_crash_info_handler(&response);fail_allocation=0;
        if(result!=ESP_OK)continue;
        o=cJSON_Parse((const char *)response.bytes);assert(o);
        if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"ok")))assert(cJSON_GetObjectItemCaseSensitive(o,"panic_reason") && cJSON_GetObjectItemCaseSensitive(o,"download"));
        else assert(strstr(response.status,"503"));
        cJSON_Delete(o);
    }
    cJSON_InitHooks(NULL);
    puts("Crash evidence: admin/OTA guards, 512-byte streaming, CRC/bounds, unknown boot association, incomplete read refusal and allocation faults PASS");return 0;
}
