#include "tbx_j1708_active.h"
#include "../../tb_base/tests/json_heap.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define TB_RECORD_MAX 768
static struct {tbx_j1708_active_t active;uint32_t request_event_dropped;} s;
static char stored[TB_RECORD_MAX];
static unsigned stored_rows;
static uint32_t tb_boot_id(void){return 42;}
static int tb_store_submit(const char *json){assert(strlen(json)<sizeof(stored));strcpy(stored,json);++stored_rows;return 0;}
#include "audit_under_test.h"
static void assert_no_external_fields(void)
{
    cJSON *o=cJSON_Parse(stored);assert(o);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(o,"classification")->valuestring,"OPERATIONAL_INDIRECT"));
    assert(!cJSON_GetObjectItemCaseSensitive(o,"tx_observed_hex"));
    assert(!cJSON_GetObjectItemCaseSensitive(o,"response_observed_hex"));
    assert(!cJSON_GetObjectItemCaseSensitive(o,"external_capture_verified"));cJSON_Delete(o);
}
int main(void)
{
    heap_reset();request_event_t item={.field=true,.field_started_us=1000000,.uptime_us=2000000,.pid=26,.local_mid=172,.target_mid=128};
    strcpy(item.event,"FIELD_CONTROL");strcpy(item.detail,"MATCHED_DURATION_WITHOUT_TRANSMISSION");
    request_store_event(&item);assert(stored_rows==1 && !strstr(stored,"tx_frame") && !strstr(stored,"tx_start_us"));assert_no_external_fields();assert(!heap_used);
    item.pid=0;strcpy(item.event,"FIELD_STARTED");request_store_event(&item);
    assert(!strstr(stored,"\"pid\"") && !strstr(stored,"target_mid") && !strstr(stored,"tx_frame"));
    assert_no_external_fields();assert(!heap_used);
    item.field_health=(tbx_j1708_field_health_t){.before_frames=1800,.after_frames=1810,.before_duration_us=30000000,.after_duration_us=30010000};
    strcpy(item.event,"FIELD_SATISFACTORY");request_store_event(&item);
    assert(strstr(stored,"before_frames") && strstr(stored,"after_duration_ms"));assert_no_external_fields();assert(!heap_used);item.pid=26;
    strcpy(item.event,"RESPONSE_FRAME");item.response_size=4;uint8_t reply[]={128,26,1,101};memcpy(item.response,reply,4);
    request_store_event(&item);assert(strstr(stored,"801A0165") && strstr(stored,"checksum_valid"));assert_no_external_fields();assert(!heap_used);
    item.response_size=0;item.field_result=true;strcpy(item.event,"FIELD_PID_RESULT");
    item.field_pid=(tbx_j1708_field_pid_t){.controls=3,.sent=3,.echoes=3,.replies=3,.latency_min_us=205000,.latency_max_us=210000};
    heap_reset();request_store_event(&item);size_t calls=heap_calls;assert(!heap_used);size_t bytes=strlen(stored);
    for(size_t fail=1;fail<=calls;fail++){
        heap_reset();heap_fail_call=fail;stored_rows=0;s.active.field.audit_lost=false;
        request_store_event(&item);assert(!heap_used);
        if(!stored_rows)assert(s.active.field.audit_lost);else{heap_fail_call=0;assert_no_external_fields();assert(!heap_used);}
    }
    printf("Actual field audit: %zu-byte PID result, measured raw response, no fabricated external capture or control TX, exhaustive allocation failures preserve ownership and block lost evidence PASS\n",bytes);
    return 0;
}
