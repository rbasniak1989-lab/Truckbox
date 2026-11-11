#include "tbx_j1708_active.h"
#include "tb_json.h"
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
static void test_guard_diagnostic(void)
{
    heap_reset();request_event_t item={.guard=true,.field=true};
    tbx_rx_decision_t *d=&item.guard_decision;
    d->at_us=1577473000;d->window=(tbx_rx_window_t){1567473000,1577473000,{599,6,0,0,0,0}};
    d->reference=(tbx_rx_window_t){1537473000,1567473000,{1800,1,0,0,0,0}};
    d->cause=TBX_GUARD_RX_QUALITY;d->streak=3;d->before_flags=3;d->stopped=true;
    d->predicates=TBX_GUARD_CRC_ELEVATED|TBX_GUARD_PERSISTENT|TBX_GUARD_REFERENCE_READY;
    d->sent=318;d->echoes=318;d->candidates=318;d->totals.valid=90000;
    stored_rows=0;request_store_event(&item);assert(stored_rows==1 && !heap_used);
    size_t bytes=strlen(stored),calls=heap_calls;
    cJSON *o=cJSON_Parse(stored);assert(o);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(o,"source")->valuestring,"LIVE_PROTECTION_DECISION"));
    assert(!cJSON_GetObjectItemCaseSensitive(o,"before_frames"));
    assert(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(o,"w"),2)->valueint==599);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"crc_hit")));
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"candidate_only")));
    cJSON_Delete(o);assert(!heap_used);
    for(size_t fail=1;fail<=calls;fail++){
        heap_reset();heap_fail_call=fail;stored_rows=0;s.active.field.audit_lost=false;
        request_store_event(&item);assert(!heap_used);
        if(!stored_rows)assert(s.active.field.audit_lost);
    }
    heap_reset();
    /* Deliberately generous per-window input counts for 9600 baud, with
     * cumulative uint32 wrap extremes and a long monotonic uptime. */
    d->at_us=UINT64_C(9007199254740991);d->window.start_us=d->at_us-10000000;d->window.end_us=d->at_us;
    d->reference.start_us=d->at_us-40000000;d->reference.end_us=d->at_us-10000000;
    d->window.count=(tbx_rx_counts_t){10000,10000,10000,10000,10000,10000};
    d->reference.count=(tbx_rx_counts_t){30000,30000,30000,30000,30000,30000};
    d->totals=(tbx_rx_counts_t){UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
    d->sent=d->candidates=d->timeouts=d->echoes=d->collisions=d->echo_timeouts=d->tx_errors=UINT32_MAX;
    d->pid=250;d->target_mid=255;item.local_mid=172;d->timeout_ms=5000;d->spacing_ms=1200;d->request_failures=3;
    stored_rows=0;request_store_event(&item);assert(stored_rows==1 && !heap_used);
    printf("Guard upper-range fixture fits %zu bytes (limit 767); ",strlen(stored));
    heap_reset();printf("Guard record: %zu bytes, live/ref counters and actual predicate/mode flags, atomic arrays and allocation failure ownership PASS\n",bytes);
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
    test_guard_diagnostic();return 0;
}
