/* Real store/CRC/recovery/ACK; POSIX file-attribute boundary plus separate
 * real-FatFs tests prove the corresponding AM_RDO/FR_DENIED behavior. */
#define main existing_store_tests
#include "test_store_transactions.c"
#undef main

static size_t bytes(const char *path,char *out,size_t cap){
    FILE *f=fopen(path,"rb");assert(f);size_t n=fread(out,1,cap,f);assert(!ferror(f) && fclose(f)==0);return n;
}
static void legacy_readonly_rollover(void){
    reset_store();append_json("{\"raw\":\"OLD_VALID\"}",1);
    char old[96],before[2048],after[2048];segment_path(old,sizeof(old),1);
    size_t length=bytes(old,before,sizeof(before));assert(chmod(old,0400)==0);
    char other[96];snprintf(other,sizeof(other),"%s/foreign.txt",host_directory);
    FILE *f=fopen(other,"wb");assert(f && fputs("FOREIGN",f)>=0 && fclose(f)==0);
    state.preserve_fence_set=false;state.ready=false;state.write_confirmed=false;
    assert(recover_after_failure());state.ready=true;
    assert(state.preserve_fence_set && state.preserve_through==1 && host_nvs_keep==1);
    assert(state.segments[0].read_only && state.segments[0].preserved);
    assert(state.recovery_successes==0 && state.recovery_scans==1 && !state.write_confirmed);
    cJSON *o=cJSON_CreateObject();tb_store_add_json(o);cJSON *status=cJSON_GetObjectItemCaseSensitive(o,"storage");
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(status,"ready")) && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(status,"read_ready")));cJSON_Delete(o);
    append_json("{\"raw\":\"NEW_DURABLE\"}",2);
    assert(state.ready && state.write_confirmed && state.recovery_successes==1 && state.write_errors==0);
    assert(state.segment_count==2 && state.segments[1].id==2);
    assert(bytes(old,after,sizeof(after))==length && !memcmp(before,after,length));
    struct stat info;assert(stat(old,&info)==0 && !(info.st_mode&S_IWUSR));
    cJSON *batch=tb_store_batch(8,12000);assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(batch,"ok")));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch,"samples"))==2);
    assert(cJSON_GetObjectItemCaseSensitive(sample_at(batch,0),"seq")->valuedouble==1);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(sample_at(batch,0),"raw")->valuestring,"OLD_VALID"));
    uint64_t ack=(uint64_t)cJSON_GetObjectItemCaseSensitive(batch,"max_seq")->valuedouble;cJSON_Delete(batch);
    assert(tb_store_ack(ack)==ESP_OK && state.pending_records==0);
    assert(access(old,F_OK)==0 && access(other,F_OK)==0 && state.segment_count==1);
    uint64_t confirmed_ack=host_nvs_ack;
    state.preserve_through=host_nvs_keep;state.preserve_fence_set=true;simulate_restart();
    assert(state.pending_records==0 && host_nvs_ack==confirmed_ack && state.segments[0].preserved && state.segments[0].read_only);
    append_json("{\"raw\":\"AFTER_REBOOT\"}",3);
    assert(state.write_confirmed && state.highest_committed>ack);
    assert(bytes(old,after,sizeof(after))==length && !memcmp(before,after,length));
    assert(unlink(other)==0);cleanup_store();
}
static void writable_legacy_is_append_only(void){
    reset_store();append_json("{\"raw\":\"VALID_PREFIX\"}",1);
    char path[96],before[2048],after[2048];segment_path(path,sizeof(path),1);
    size_t length=bytes(path,before,sizeof(before));state.preserve_fence_set=false;
    simulate_restart();assert(state.segments[0].preserved && !state.segments[0].read_only);
    append_json("{\"raw\":\"APPENDED\"}",2);
    assert(bytes(path,after,sizeof(after))>length && !memcmp(before,after,length));
    cJSON *batch=tb_store_batch(8,12000);uint64_t ack=(uint64_t)cJSON_GetObjectItemCaseSensitive(batch,"max_seq")->valuedouble;cJSON_Delete(batch);
    assert(tb_store_ack(ack)==ESP_OK && access(path,F_OK)==0);
    cleanup_store();
}
static void no_overwrite_on_name_collision(void){
    reset_store();char path[96],before[128],after[128];segment_path(path,sizeof(path),1);
    FILE *f=fopen(path,"wb");assert(f && fputs("EXISTING_FOREIGN_BYTES",f)>=0 && fclose(f)==0);
    size_t n=bytes(path,before,sizeof(before));append_json("{\"raw\":\"DO_NOT_APPEND_TO_COLLISION\"}",1);
    assert(!state.ready && state.write_errors==1 && !strcmp(state.last_io_failure.stage,"OPEN"));
    assert(state.last_io_failure.error==EEXIST && (state.last_io_failure.open_flags&O_EXCL));
    assert(!strcmp(state.last_io_failure.path,path));
    assert(bytes(path,after,sizeof(after))==n && !memcmp(before,after,n));cleanup_store();
}
static void preservation_commit_failure_does_not_prune(void){
    reset_store();append_json("{\"raw\":\"PRESERVE\"}",1);
    char path[96];segment_path(path,sizeof(path),1);state.preserve_fence_set=false;state.ready=false;
    host_fail_commit_once=true;assert(!recover_records() && !state.preserve_fence_set && host_nvs_keep==0);
    assert(access(path,F_OK)==0);assert(recover_records() && host_nvs_keep==1);state.ready=true;
    assert(state.segments[0].preserved);cleanup_store();
}
static void scans_never_count_as_write_recovery(void){
    reset_store();append_json("{\"raw\":\"DURABLE_BEFORE_FAILURE\"}",1);state.ready=false;
    for(unsigned i=0;i<24;i++){assert(recover_after_failure());state.ready=true;assert(state.recovery_successes==0 && !state.write_confirmed);}
    assert(state.recovery_scans==24 && state.recovery_attempts==24);
    append_json("{\"raw\":\"ACTUAL_DURABLE_RECOVERY\"}",2);
    assert(state.recovery_successes==1 && state.write_confirmed);cleanup_store();
}
static void failed_recovery_budget(void){
    reset_store();append_json("{\"raw\":\"KEPT\"}",1);state.ready=false;state.volume_rebind_required=true;
    host_volume_rebind_result=1;
    for(unsigned i=0;i<3;i++){assert(!recover_after_failure());assert(state.io_paused==(i==2));}
    assert(state.recovery_failures==3 && !state.write_confirmed && host_nvs_ack==0);
    assert(tb_store_retry()==ESP_OK && !state.io_paused && !state.recovery_failures);
    host_volume_rebind_result=0;assert(recover_after_failure());state.ready=true;
    append_json("{\"raw\":\"NEW\"}",2);assert(state.write_confirmed);cleanup_store();
}
static void newly_readonly_file_is_not_recycled(void){
    reset_store();append_json("{\"raw\":\"KEEP_READONLY\"}",1);
    char path[96];segment_path(path,sizeof(path),1);assert(chmod(path,0400)==0);
    simulate_restart();assert(!state.segments[0].preserved && state.segments[0].read_only);
    cJSON *batch=tb_store_batch(8,12000);cJSON_Delete(batch);
    assert(tb_store_ack(1)==ESP_OK && access(path,F_OK)==0 && state.segment_count==1);
    append_json("{\"raw\":\"NEW_SEGMENT\"}",2);assert(state.segments[1].id==2);cleanup_store();
}
int main(void){
    legacy_readonly_rollover();writable_legacy_is_append_only();no_overwrite_on_name_collision();
    preservation_commit_failure_does_not_prune();scans_never_count_as_write_recovery();
    failed_recovery_budget();
    newly_readonly_file_is_not_recycled();
    puts("Store OPEN regression: readonly legacy rolled to new exclusive segment; preserved bytes/IDs/attrs across reboot+ACK; no collision overwrite; fence commit fault; 24 scans != durable recovery PASS");return 0;
}
