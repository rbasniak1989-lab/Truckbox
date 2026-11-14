/* Fault boundaries only; exercise the real writer, recovery, batch and ACK. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
enum fault_kind { NONE, OPEN, FDOPEN, WRITE, FLUSH, SYNC, CLOSE, SEEK_TAIL, READ_TAIL, FAULT_SEEK_END, SEPARATOR };
static enum fault_kind fault;
static bool secondary_close;
static int io_open(const char *path,int flags,mode_t mode){
    if(fault==OPEN){errno=EACCES;return -1;}return open(path,flags,mode);
}
static FILE *io_fdopen(int fd,const char *mode){
    if(fault==FDOPEN){errno=ENOMEM;return NULL;}return fdopen(fd,mode);
}
static FILE *io_fopen(const char *path,const char *mode){
    if(fault==OPEN && !strcmp(mode,"a+b")){errno=EACCES;return NULL;}
    return fopen(path,mode);
}
static size_t io_fwrite(const void *p,size_t size,size_t count,FILE *f){
    if(fault==WRITE){size_t n=fwrite(p,size,count/2,f);errno=ENOSPC;return n;}
    return fwrite(p,size,count,f);
}
static int io_fflush(FILE *f){if(fault==FLUSH){errno=EIO;return EOF;}return fflush(f);}
static int io_fclose(FILE *f){
    int r=fclose(f);
    if(fault==CLOSE || secondary_close){errno=ENOSPC;return EOF;}return r;
}
static int io_fseek(FILE *f,long offset,int origin){
    if((fault==SEEK_TAIL && offset==-1 && origin==SEEK_END) ||
       (fault==FAULT_SEEK_END && offset==0 && origin==SEEK_END)){errno=EIO;return -1;}
    return fseek(f,offset,origin);
}
static int io_fgetc(FILE *f){if(fault==READ_TAIL){errno=EIO;return EOF;}return fgetc(f);}
static int io_fputc(int c,FILE *f){if(fault==SEPARATOR){errno=EIO;return EOF;}return fputc(c,f);}
#define fopen io_fopen
#define open io_open
#define fdopen io_fdopen
#define fwrite io_fwrite
#define fflush io_fflush
#define fclose io_fclose
#define fseek io_fseek
#define fgetc io_fgetc
#define fputc io_fputc
#define main store_existing_tests
#include "test_store_transactions.c"
#undef main

static void check_failure(enum fault_kind kind,const char *stage){
    fault=NONE;secondary_close=false;reset_store();state.medium="flash";
    host_volume_rebind_calls=0;host_volume_rebind_result=0;
    append_json("{\"raw\":\"CONFIRMED\"}",1);
    if(kind>=SEEK_TAIL){
        char path[64];segment_path(path,sizeof(path),state.segments[0].id);
        FILE *f=fopen(path,"ab");assert(f);assert(fputc('!',f)!=EOF);assert(fclose(f)==0);
        ++state.segments[0].bytes;state.segments[0].quarantined=true;
    }
    fault=kind;secondary_close=kind==FLUSH;
    host_fail_fsync_once=kind==SYNC;
    append_json("{\"raw\":\"RETAINED\"}",2);
    assert(!state.ready && state.write_errors==1 && state.highest_committed==1);
    assert(state.last_io_failure.stage && !strcmp(state.last_io_failure.stage,stage));
    int expected=kind==OPEN?EACCES:kind==FDOPEN?ENOMEM:(kind==WRITE || kind==CLOSE)?ENOSPC:EIO;
    assert(state.last_io_failure.error==expected && state.volume_rebind_required);
    if(kind==FLUSH)assert(state.last_io_failure.close_error==ENOSPC); /* first errno survives close */
    cJSON *batch=tb_store_batch(8,12000);assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(batch,"ok")));cJSON_Delete(batch);
    cJSON *o=cJSON_CreateObject();tb_store_add_json(o);
    cJSON *storage=cJSON_GetObjectItemCaseSensitive(o,"storage");
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(storage,"pending_records")));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(storage,"last_io_failure"),"stage")->valuestring,stage));cJSON_Delete(o);
    fault=NONE;secondary_close=false;
    /* Simulate a volume that remains unavailable: never mark it ready, prune
     * pending bytes, or manufacture ACKs just because retry was attempted. */
    host_volume_rebind_result=1;
    assert(!recover_after_failure() && !state.ready && state.volume_rebind_required);
    assert(state.ack_sequence==0 && host_nvs_ack==0);
    host_volume_rebind_result=0;assert(recover_after_failure());state.ready=true;
    assert(host_volume_rebind_calls==2 && state.volume_rebinds==1 && state.recovery_attempts==2 && state.recovery_successes==0);
    /* Exactly the production retained-row rule: recover a complete ambiguous
     * write, otherwise append the retained payload once using a new sequence. */
    if(state.highest_committed<state.last_io_failure.sequence)append_json("{\"raw\":\"RETAINED\"}",2);
    append_json("{\"raw\":\"AFTER_RECOVERY\"}",3);
    assert(state.write_confirmed && state.recovery_successes==1);
    batch=tb_store_batch(8,12000);cJSON *samples=cJSON_GetObjectItemCaseSensitive(batch,"samples");
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(batch,"ok")) && cJSON_GetArraySize(samples)==3);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(sample_at(batch,0),"raw")->valuestring,"CONFIRMED"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(sample_at(batch,1),"raw")->valuestring,"RETAINED"));
    uint64_t target=(uint64_t)cJSON_GetObjectItemCaseSensitive(batch,"max_seq")->valuedouble;cJSON_Delete(batch);
    assert(tb_store_ack(target)==ESP_OK && state.pending_records==0);cleanup_store();
}
static void repeated_failure_then_recover(void){
    fault=NONE;reset_store();state.medium="flash";host_volume_rebind_result=0;host_volume_rebind_calls=0;
    for(unsigned i=0;i<3;i++){
        fault=OPEN;append_json("{\"raw\":\"BUFFERED\"}",3);assert(!state.ready);
        assert(state.committed_this_boot==0 && host_nvs_ack==0);
        fault=NONE;assert(recover_after_failure());state.ready=true;
    }
    assert(state.write_errors==3 && state.io_paused && !state.write_confirmed && state.recovery_successes==0);
    uint64_t before=state.next_sequence;
    for(unsigned i=0;i<24;i++)append_json("{\"raw\":\"STILL_BUFFERED\"}",3);
    assert(state.write_errors==3 && state.next_sequence==before && state.recovery_successes==0);
    assert(tb_store_retry()==ESP_OK && !state.io_paused);
    assert(recover_after_failure());state.ready=true;
    append_json("{\"raw\":\"BUFFERED\"}",3);
    assert(state.ready && state.pending_records==1 && state.committed_this_boot==1);
    assert(host_nvs_reserved==256 && host_commit_calls==1 && state.highest_committed==4);
    simulate_restart();assert(state.next_sequence==257); /* no reuse across boots */
    cleanup_store();
}
static void recovered_reservation_commit_must_be_retried(void){
    fault=NONE;reset_store();append_json("{\"raw\":\"BASE\"}",1);
    char path[64];segment_path(path,sizeof(path),state.segments[0].id);
    const char data[]="{\"raw\":\"RECOVERED_HIGH_SEQUENCE\"}";
    uint32_t crc=tb_store_record_crc(1000,1000,data,strlen(data));
    FILE *f=fopen(path,"ab");assert(f);
    assert(fprintf(f,"{\"seq\":1000,\"uptime_us\":1000,\"crc32\":%" PRIu32 ",\"data\":%s}\n",crc,data)>0);
    assert(fclose(f)==0);state.ready=false;
    host_fail_commit_once=true;
    assert(!recover_after_failure() && !state.ready);
    assert(state.reserved_end==256 && host_nvs_reserved==256 && host_nvs_ack==0);
    assert(tb_store_ack(1000)!=ESP_OK);
    assert(recover_after_failure());state.ready=true;
    assert(state.reserved_end==1000 && host_nvs_reserved==1000 && state.next_sequence==1001);
    append_json("{\"raw\":\"NEXT\"}",2);
    cJSON *batch=tb_store_batch(8,12000);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch,"samples"))==3);cJSON_Delete(batch);
    assert(tb_store_ack(1001)==ESP_OK && host_nvs_reserved>=host_nvs_ack);
    cleanup_store();
}
int main(void){
    for(enum fault_kind f=OPEN;f<=SEPARATOR;f++){
        static const char *const names[]={"NONE","OPEN","FDOPEN","WRITE","FLUSH","SYNC","CLOSE","SEEK_TAIL","READ_TAIL","SEEK_END","SEPARATOR"};
        check_failure(f,names[f]);
    }
    repeated_failure_then_recover();
    recovered_reservation_commit_must_be_retried();
    puts("Store IO: 10 failing stages, first/close errno, 3-attempt pause, no false recovery success, retained CRC recovery, NVS retry, no duplicate and ACK-only recycling PASS");
    return 0;
}
