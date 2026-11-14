#define main prior_store_tests
#include "test_store_transactions.c"
#undef main
typedef struct {const char *body;int status;cJSON *reply;}httpd_req_t;
#define HTTPD_400_BAD_REQUEST 400
static bool admin,busy;
static bool tb_http_require_admin(httpd_req_t *r){if(!admin)r->status=401;return admin;}
static bool tb_web_ota_busy(void){return busy;}
static esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *text){(void)text;r->status=status;return ESP_FAIL;}
static esp_err_t tb_http_read_json(httpd_req_t *r,size_t n,cJSON **out){
    if(!r->body || strlen(r->body)>=n)return ESP_FAIL;
    *out=cJSON_Parse(r->body);return *out?ESP_OK:ESP_FAIL;
}
static esp_err_t tb_http_send_json(httpd_req_t *r,cJSON *owned){r->reply=owned;r->status=200;return ESP_OK;}
#include "retry_handler_under_test.h"
int main(void){
    reset_store();append_json("{\"raw\":\"PENDING\"}",1);
    state.io_paused=true;state.consecutive_io_errors=3;state.write_confirmed=false;state.write_errors=3;
    host_queue_length=64;uint64_t reserved=host_nvs_reserved,next=state.next_sequence;
    httpd_req_t request={.body="{\"authorized\":true}"};
    admin=false;assert(storage_retry_handler(&request)==ESP_OK && request.status==401 && state.io_paused);
    admin=true;busy=true;assert(storage_retry_handler(&request)!=ESP_OK && state.io_paused);
    busy=false;request.body="{";assert(storage_retry_handler(&request)!=ESP_OK && state.io_paused);
    request.body="{\"authorized\":false}";assert(storage_retry_handler(&request)!=ESP_OK && state.io_paused);
    request.body="{\"authorized\":true}";host_mutex_busy=true;
    assert(storage_retry_handler(&request)!=ESP_OK && state.io_paused);host_mutex_busy=false;
    assert(storage_retry_handler(&request)==ESP_OK && request.status==200 && !state.io_paused);
    assert(!state.ready && !state.write_confirmed && state.write_errors==3 && state.pending_records==1);
    assert(host_queue_length==64 && state.next_sequence==next && host_nvs_reserved==reserved && host_nvs_ack==0);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(request.reply,"storage"),"ready")));
    cJSON_Delete(request.reply);cleanup_store();
    puts("Storage retry route: admin, explicit authorization, JSON/OTA/mutex guards; actual retry preserves RAM queue, record IDs, durable pending, errors and NVS; no restart or acquisition control PASS");return 0;
}
