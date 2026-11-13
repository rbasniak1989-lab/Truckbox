/* On-demand, admin-only, read-only access to retained crash evidence. This
 * partition is independent of the telemetry filesystem. Never erase a dump
 * or assume it belongs to the current boot/firmware. No background upload. */
#ifndef TB_CRASH_HOST_TEST
#include "tb.h"
#include "tb_crash.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#else
#include "tests/crash_host_stubs.h"
#endif
#include <stddef.h>
#include <stdint.h>

static esp_err_t unavailable(httpd_req_t *request,const char *status,const char *message)
{
    httpd_resp_set_status(request,status);
    httpd_resp_set_type(request,"application/json");
    return httpd_resp_send(request,message,HTTPD_RESP_USE_STRLEN);
}

esp_err_t tb_crash_info_handler(httpd_req_t *request)
{
    if(!tb_http_require_admin(request))return ESP_OK;
    if(tb_web_ota_busy())return unavailable(request,"409 Conflict","{\"ok\":false,\"error\":\"ota_busy\"}");
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_COREDUMP,NULL);
    size_t address=0,length=0;
    esp_err_t found=esp_core_dump_image_get(&address,&length);
    bool bounded=found==ESP_OK && partition && address>=partition->address &&
        address-partition->address<=partition->size && length>0 && length<=partition->size-(address-partition->address);
    esp_err_t checked=bounded?esp_core_dump_image_check():found==ESP_OK?ESP_ERR_INVALID_SIZE:found;
    cJSON *body=cJSON_CreateObject();
    if(!body)return unavailable(request,"503 Service Unavailable","{\"ok\":false,\"error\":\"RAM\"}");
    bool complete=cJSON_AddBoolToObject(body,"ok",true) &&
        cJSON_AddNumberToObject(body,"current_boot_id",tb_boot_id()) &&
        cJSON_AddBoolToObject(body,"partition_available",partition!=NULL) &&
        cJSON_AddBoolToObject(body,"image_present",bounded) &&
        cJSON_AddBoolToObject(body,"image_valid",bounded && checked==ESP_OK) &&
        cJSON_AddNumberToObject(body,"image_result",checked) &&
        cJSON_AddNullToObject(body,"dump_boot_id") &&
        cJSON_AddStringToObject(body,"boot_association","UNKNOWN_RETAINED_DUMP") &&
        cJSON_AddStringToObject(body,"download","/api/crash.bin");
    if(partition)complete=complete && cJSON_AddNumberToObject(body,"partition_bytes",partition->size);
    char reason[160]={0};
    if(bounded && checked==ESP_OK && esp_core_dump_get_panic_reason(reason,sizeof(reason))==ESP_OK)
        complete=complete && cJSON_AddStringToObject(body,"panic_reason",reason);
    else complete=complete && cJSON_AddNullToObject(body,"panic_reason");
    if(!complete){cJSON_Delete(body);return unavailable(request,"503 Service Unavailable","{\"ok\":false,\"error\":\"RAM\"}");}
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    return tb_http_send_json(request,body);
}

esp_err_t tb_crash_download_handler(httpd_req_t *request)
{
    if(!tb_http_require_admin(request))return ESP_OK;
    if(tb_web_ota_busy())return unavailable(request,"409 Conflict","{\"ok\":false,\"error\":\"ota_busy\"}");
    const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_COREDUMP,NULL);
    if(!partition)return unavailable(request,"404 Not Found","{\"ok\":false,\"error\":\"no_coredump_partition\"}");
    httpd_resp_set_type(request,"application/octet-stream");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    httpd_resp_set_hdr(request,"Content-Disposition","attachment; filename=truckbox-coredump.bin");
    /* Export the whole bounded partition even when its header/CRC is damaged.
     * Never turn corrupted evidence into a claimed valid crash or read NVS. */
    unsigned char buffer[512];
    for(size_t offset=0;offset<partition->size;){
        size_t n=partition->size-offset;if(n>sizeof(buffer))n=sizeof(buffer);
        esp_err_t error=esp_partition_read(partition,offset,buffer,n);
        if(error!=ESP_OK)return ESP_FAIL; /* close incomplete HTTP response */
        error=httpd_resp_send_chunk(request,(const char *)buffer,n);
        if(error!=ESP_OK)return error;
        offset+=n;
    }
    return httpd_resp_send_chunk(request,NULL,0);
}
