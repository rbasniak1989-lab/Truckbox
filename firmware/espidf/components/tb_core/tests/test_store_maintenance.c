#define main prior_store_tests
#include "test_store_transactions.c"
#undef main

typedef struct {const char *body;int status;cJSON *reply;}httpd_req_t;
#define HTTPD_400_BAD_REQUEST 400
static bool admin,ota_busy;
static unsigned backup_calls;
static bool tb_http_require_admin(httpd_req_t *r){if(!admin)r->status=401;return admin;}
static bool tb_web_begin_maintenance(void){if(ota_busy)return false;ota_busy=true;return true;}
static void tb_web_set_ota_busy(bool busy){ota_busy=busy;}
static esp_err_t tb_store_flash_backup(httpd_req_t *r){(void)r;++backup_calls;return ESP_OK;}
static esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *status){assert(!strcmp(status,"400 Bad Request"));r->status=400;return ESP_OK;}
static esp_err_t tb_http_read_json(httpd_req_t *r,size_t n,cJSON **out){
    if(!r->body || strlen(r->body)>=n)return ESP_FAIL;
    *out=cJSON_Parse(r->body);return *out?ESP_OK:ESP_FAIL;
}
static esp_err_t tb_http_send_json(httpd_req_t *r,cJSON *owned){r->reply=owned;if(!r->status)r->status=200;return ESP_OK;}
#include "maintenance_handlers_under_test.h"

/* Destruction ONLY of a disposable test directory, after real authorization
 * code above. The production media boundary uses the actual FatFs formatter. */
static esp_err_t format_fixture(void){
    assert(ota_busy && maintenance.running && !state.ready && state.io_paused);
    if(host_format_result!=ESP_OK)return host_format_result;
    DIR *dir=opendir(host_directory);assert(dir);struct dirent *e;
    while((e=readdir(dir))){
        if(e->d_name[0]=='.')continue;
        char path[512];snprintf(path,sizeof(path),"%s/%s",host_directory,e->d_name);
        assert(unlink(path)==0);
    }
    assert(closedir(dir)==0);return ESP_OK;
}
static esp_err_t invoke(const char *body,int expected){
    httpd_req_t request={.body=body};esp_err_t result=storage_maintenance_handler(&request);
    assert(request.status==expected);cJSON_Delete(request.reply);return result;
}
static void fixture(void){
    reset_store();memset(&maintenance,0,sizeof(maintenance));
    state.medium="flash";state.nvs_open=true;state.medium_initialized=true;
    host_parked=host_maintenance_target=true;host_now_us=1234567;
    host_format_calls=backup_calls=0;host_format_result=ESP_OK;host_format_callback=format_fixture;
    admin=true;ota_busy=false;
}
static void body_for(char body[512],const char *challenge){
    snprintf(body,512,"{\"action\":\"format\",\"authorized\":true,\"accept_data_loss\":true,\"confirmation\":\"FORMATAR TBFLASH\",\"device_uid\":\"%s\",\"boot_id\":42,\"challenge\":\"%s\"}",tb_device_uid(),challenge);
}
static void prepare(char out[33]){
    httpd_req_t r={.body="{\"action\":\"prepare\",\"authorized\":true}"};
    assert(storage_maintenance_handler(&r)==ESP_OK && r.status==200 && !ota_busy);
    const cJSON *token=cJSON_GetObjectItemCaseSensitive(r.reply,"challenge");assert(cJSON_IsString(token) && strlen(token->valuestring)==32);
    memcpy(out,token->valuestring,33);cJSON_Delete(r.reply);
}
static void remove_probe(void){char path[96];snprintf(path,sizeof(path),"%s/TBPROBE.CHK",host_directory);assert(unlink(path)==0);}

int main(void){
    fixture();append_json("{\"sample_kind\":\"OPERATIONAL\",\"speed\":0}",1);
    char old_path[96];segment_path(old_path,sizeof(old_path),state.segments[0].id);
    struct stat old;assert(stat(old_path,&old)==0);
    char challenge[33],body[512];
    admin=false;invoke("{}",401);assert(!host_format_calls);admin=true;
    invoke("{",0); /* invalid JSON never reaches formatting */
    invoke("{\"action\":\"format\",\"authorized\":true}",400);
    host_parked=false;invoke("{\"action\":\"prepare\",\"authorized\":true}",200);
    assert(!host_format_calls); /* storage preparation is allowed in motion */
    host_maintenance_target=false;invoke("{\"action\":\"prepare\",\"authorized\":true}",400);host_maintenance_target=true;
    ota_busy=true;invoke("{\"action\":\"prepare\",\"authorized\":true}",400);assert(ota_busy);ota_busy=false;
    prepare(challenge);assert(!host_format_calls && stat(old_path,&old)==0);
    body_for(body,challenge);host_now_us+=MAINTENANCE_TTL_US;invoke(body,400);assert(!host_format_calls);
    prepare(challenge);body_for(body,challenge);
    host_mutex_busy=true;invoke(body,400);host_mutex_busy=false;assert(!host_format_calls);
    cJSON *invalid=cJSON_Parse(body);cJSON_ReplaceItemInObject(invalid,"accept_data_loss",cJSON_CreateFalse());
    char *wire=cJSON_PrintUnformatted(invalid);invoke(wire,400);cJSON_free(wire);cJSON_Delete(invalid);
    invalid=cJSON_Parse(body);cJSON_ReplaceItemInObject(invalid,"boot_id",cJSON_CreateNumber(41));
    wire=cJSON_PrintUnformatted(invalid);invoke(wire,400);cJSON_free(wire);cJSON_Delete(invalid);
    invalid=cJSON_Parse(body);cJSON_ReplaceItemInObject(invalid,"device_uid",cJSON_CreateString("OTHER"));
    wire=cJSON_PrintUnformatted(invalid);invoke(wire,400);cJSON_free(wire);cJSON_Delete(invalid);
    invalid=cJSON_Parse(body);cJSON_ReplaceItemInObject(invalid,"confirmation",cJSON_CreateString("FORMATAR TUDO"));
    wire=cJSON_PrintUnformatted(invalid);invoke(wire,400);cJSON_free(wire);cJSON_Delete(invalid);
    invalid=cJSON_Parse(body);cJSON_ReplaceItemInObject(invalid,"authorized",cJSON_CreateFalse());
    wire=cJSON_PrintUnformatted(invalid);invoke(wire,400);cJSON_free(wire);cJSON_Delete(invalid);
    invalid=cJSON_Parse(body);cJSON_ReplaceItemInObject(invalid,"challenge",cJSON_CreateString("00000000000000000000000000000000"));
    wire=cJSON_PrintUnformatted(invalid);invoke(wire,400);cJSON_free(wire);cJSON_Delete(invalid);
    assert(!host_format_calls && stat(old_path,&old)==0);
    uint64_t reserved=host_nvs_reserved,ack=host_nvs_ack;host_queue_length=64;
    state.preserve_through=host_pending_keep=host_nvs_keep=20;
    host_fail_commit_once=true;invoke(body,400);assert(!host_format_calls && stat(old_path,&old)==0 && !ota_busy);
    invoke(body,400);assert(!host_format_calls); /* nonce consumed before failed commit */
    prepare(challenge);body_for(body,challenge);invoke(body,200);
    assert(host_format_calls==1 && !ota_busy && access(old_path,F_OK)!=0);
    assert(maintenance.probe_durable && maintenance.successes==1 && !state.write_confirmed);
    assert(host_nvs_reserved==reserved && host_nvs_ack==ack && host_nvs_keep==0 && host_queue_length==64);
    assert(state.ready && state.recovery_complete && !state.io_paused && !state.pending_records);
    invoke(body,400);assert(host_format_calls==1); /* lost HTTP response cannot reformat */
    append_json("{\"sample_kind\":\"OPERATIONAL\",\"fuel_l\":614.27}",2);
    assert(state.write_confirmed && state.highest_committed>reserved);
    uint64_t sequence=state.highest_committed;cJSON *batch=tb_store_batch(8,8192);
    assert(cJSON_GetObjectItemCaseSensitive(sample_at(batch,0),"seq")->valuedouble==(double)sequence);cJSON_Delete(batch);
    assert(tb_store_ack(sequence)==ESP_OK && host_nvs_ack==sequence);
    memset(&maintenance,0,sizeof(maintenance));simulate_restart();
    assert(!maintenance.challenge[0] && !maintenance.attempts && host_format_calls==1 && host_nvs_ack==sequence);
    invoke(body,400);assert(host_format_calls==1);
    httpd_req_t backup={0};admin=false;storage_backup_handler(&backup);assert(!backup_calls);admin=true;
    host_parked=false;assert(storage_backup_handler(&backup)==ESP_OK && backup_calls==1);
    assert(storage_backup_handler(&backup)==ESP_OK && backup_calls==2 && !ota_busy);
    remove_probe();cleanup_store();

    fixture();prepare(challenge);body_for(body,challenge);host_format_result=ESP_FAIL;
    invoke(body,400);assert(host_format_calls==1 && state.io_paused && !state.ready && !maintenance.probe_durable);
    assert(tb_store_retry()==ESP_OK && host_format_calls==1); /* retry NEVER formats */
    host_format_result=ESP_OK;prepare(challenge);body_for(body,challenge);host_fail_fsync_once=true;
    invoke(body,400);assert(!state.write_confirmed && !maintenance.probe_durable && state.io_paused);
    remove_probe();cleanup_store();
    puts("Storage maintenance: movement allowed, no automatic wipe; admin/target/OTA/protocol/UID/boot/nonce/expiry guards; format/probe/NVS failure; replay rejection; retained RAM and sequence/ACK; telemetry/ACK/reboot; JSON errors and backup authorization PASS");return 0;
}
