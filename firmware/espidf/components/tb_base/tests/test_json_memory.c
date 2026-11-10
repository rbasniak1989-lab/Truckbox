#include "tb_json.h"
#include "json_heap.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct { char bytes[32768];size_t used,calls,fail_call; } sink_t;
static bool sink(const char *bytes,size_t length,void *context)
{
    sink_t *s=context;
    if(++s->calls==s->fail_call)return false;
    assert(length<sizeof(s->bytes)-s->used);
    memcpy(s->bytes+s->used,bytes,length);s->used+=length;s->bytes[s->used]=0;return true;
}
static cJSON *fixture(void)
{
    cJSON *o=cJSON_CreateObject();assert(o);
    assert(cJSON_AddStringToObject(o,"quote\"\\\n\x01","Ação UTF-8: \"\\\b\f\n\r\t\x01\x1f"));
    cJSON *a=cJSON_AddArrayToObject(o,"values");assert(a);
    double numbers[]={0,-0.0,1,1.23456789,-1e-10,1e30,9007199254740991.0,NAN,INFINITY,-INFINITY};
    for(unsigned i=0;i<sizeof(numbers)/sizeof(*numbers);i++)assert(tb_json_add_array(a,cJSON_CreateNumber(numbers[i])));
    assert(tb_json_add_array(a,cJSON_CreateBool(true)));
    assert(tb_json_add_array(a,cJSON_CreateBool(false)));
    assert(tb_json_add_array(a,cJSON_CreateNull()));
    assert(tb_json_add_object(o,"raw",cJSON_CreateRaw("{\"raw\":[1,null,true]}")));
    assert(cJSON_AddObjectToObject(o,"empty"));
    return o;
}
static void equivalence(void)
{
    heap_reset();cJSON *o=fixture();char *expected=cJSON_PrintUnformatted(o);assert(expected);
    size_t length=strlen(expected);assert(tb_json_measure(o,SIZE_MAX)==length);
    assert(tb_json_measure(o,length)==length);assert(tb_json_measure(o,length-1)==SIZE_MAX);
    size_t allocations=heap_calls;sink_t result={0};assert(tb_json_write_owned(o,sink,&result));
    assert(heap_calls==allocations && !strcmp(result.bytes,expected));
    cJSON_free(expected);assert(heap_used==0);
    o=fixture();expected=cJSON_PrintUnformatted(o);allocations=heap_calls;
    char *encoded=tb_json_print_owned(o,32768);assert(encoded && !strcmp(encoded,expected));
    assert(heap_calls==allocations+1);cJSON_free(expected);cJSON_free(encoded);assert(!heap_used);
}
static void reference_lifetime(void)
{
    heap_reset();cJSON *borrowed=fixture();size_t baseline=heap_used;
    cJSON *o=cJSON_CreateObject();assert(cJSON_AddItemReferenceToObject(o,"ref",borrowed));
    assert(tb_json_add_object(o,"array",cJSON_CreateArrayReference(cJSON_GetObjectItemCaseSensitive(borrowed,"values")->child)));
    char *expected=cJSON_PrintUnformatted(o);sink_t result={0};
    assert(tb_json_write_owned(o,sink,&result));assert(!strcmp(expected,result.bytes));cJSON_free(expected);
    assert(heap_used==baseline);char *intact=cJSON_PrintUnformatted(borrowed);assert(intact);cJSON_free(intact);
    cJSON_Delete(borrowed);assert(!heap_used);
}
static void interrupted_output(void)
{
    heap_reset();sink_t complete={0};assert(tb_json_write_owned(fixture(),sink,&complete));assert(!heap_used);
    for(size_t i=1;i<=complete.calls;i++){
        sink_t failing={.fail_call=i};assert(!tb_json_write_owned(fixture(),sink,&failing));assert(!heap_used);
    }
    assert(!tb_json_write_owned(fixture(),NULL,NULL));assert(!heap_used);
    cJSON *deep=cJSON_CreateArray(),*next=deep;
    for(unsigned i=0;i<34;i++){cJSON *child=cJSON_CreateArray();assert(tb_json_add_array(next,child));next=child;}
    assert(tb_json_measure(deep,SIZE_MAX)==SIZE_MAX);assert(!tb_json_print_owned(deep,SIZE_MAX));assert(!heap_used);
    assert(!tb_json_print_owned(fixture(),2));assert(!heap_used);
    cJSON *o=fixture();heap_fail_call=heap_calls+1;assert(!tb_json_print_owned(o,SIZE_MAX));assert(!heap_used);heap_fail_call=0;
}
static void low_memory_regression(void)
{
    heap_reset();cJSON *o=cJSON_CreateArray();
    for(unsigned i=0;i<180;i++)assert(tb_json_add_array(o,cJSON_CreateString("synthetic telemetry only: bounded output without a second growing JSON buffer")));
    size_t length=tb_json_measure(o,SIZE_MAX),baseline=heap_used;
    heap_budget=heap_used+256;
    char *old=cJSON_PrintUnformatted(o);assert(!old); /* old path needs a growing contiguous buffer */
    size_t calls=heap_calls;sink_t output={0};assert(tb_json_write_owned(o,sink,&output));
    assert(output.used==length && heap_calls==calls && !heap_used);
    printf("JSON memory: %zu-byte native tree / %zu-byte wire output: old print fails with 256 free bytes; streaming uses zero new cJSON allocations\n",baseline,length);
    heap_budget=SIZE_MAX;
}
static void failed_insert_ownership(void)
{
    heap_reset();assert(!tb_json_add_array(NULL,cJSON_CreateNumber(1)));assert(!heap_used);
    assert(!tb_json_add_object(NULL,"x",fixture()));assert(!heap_used);
    cJSON *o=cJSON_CreateObject(),*v=fixture();heap_fail_call=heap_calls+1;
    assert(!tb_json_add_object(o,"allocated-key",v));cJSON_Delete(o);assert(!heap_used);heap_fail_call=0;
}
int main(void)
{
    equivalence();reference_lifetime();interrupted_output();low_memory_regression();failed_insert_ownership();
    puts("JSON equivalence, references, all sink failures, depth/size limits, OOM and insertion ownership passed");return 0;
}
