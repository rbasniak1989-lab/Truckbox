/* Real on-disk writer/recovery/CRC/batch/ACK, simulated clock and HTTPS endpoint. */
#define main existing_store_suite_main
#include "test_store_transactions.c"
#undef main
#define MAX_ROWS 15000
static cJSON *expected[MAX_ROWS];
static uint64_t ids[MAX_ROWS];
static bool cloud_seen[MAX_ROWS];
int main(int argc,char **argv)
{
    assert(argc==2);FILE *input=fopen(argv[1],"rb");assert(input);reset_store();
    state.segment_bytes=48u*1024u;state.maximum_segments=37;
    state.total_bytes=0x1e0000;state.recovery_complete=true;
    size_t count=0,bytes_in=0;unsigned reboots=0;char row[TB_RECORD_MAX+2];
    while(fgets(row,sizeof(row),input)){
        size_t n=strlen(row);assert(n && row[n-1]=='\n');row[--n]=0;
        assert(count<MAX_ROWS);expected[count]=cJSON_Parse(row);assert(expected[count]);
        const cJSON *stamp=cJSON_GetObjectItemCaseSensitive(expected[count],"uptime_ms");
        uint64_t now=stamp?(uint64_t)stamp->valuedouble*1000:0;
        append_json(row,now);if(!state.ready || state.dropped_full || state.write_errors){fprintf(stderr,"offline failed row=%zu segments=%zu pending=%zu bytes=%zu full=%llu errors=%llu reason=%s\n",count,state.segment_count,state.pending_records,state.pending_bytes,(unsigned long long)state.dropped_full,(unsigned long long)state.write_errors,state.error);abort();}
        ids[count++]=state.highest_committed;bytes_in+=n;
        if(count%400==0){
            char path[96];segment_path(path,sizeof(path),state.segments[state.segment_count-1].id);
            FILE *f=fopen(path,"ab");assert(f);
            assert(fputs("{\"seq\":999999,\"uptime_us\":0,\"crc32\":",f)>=0 && fclose(f)==0);
            simulate_restart();++reboots;
            assert(state.pending_records==count);
        }
    }
    fclose(input);assert(count>4000 && reboots>=10);
    uint64_t physical=0;for(size_t i=0;i<state.segment_count;i++)physical+=state.segments[i].bytes;
    printf("Offline storage: duration=7200s input_records=%zu original_JSON_bytes=%zu physical_bytes=%llu budget_bytes=%zu segments=%zu reboots=%u dropped_full=%llu\n",
        count,bytes_in,(unsigned long long)physical,state.maximum_segments*state.segment_bytes,state.segment_count,reboots,(unsigned long long)state.dropped_full);
    size_t unique=0,delivered=0;bool lost_ack=true;
    while(state.pending_records){
        cJSON *batch=tb_store_batch(8,8192);assert(batch && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(batch,"ok")));
        const cJSON *samples=cJSON_GetObjectItemCaseSensitive(batch,"samples");
        assert(cJSON_GetArraySize(samples)>0);uint64_t ack=0;
        const cJSON *sample;
        cJSON_ArrayForEach(sample,samples){
            uint64_t seq=(uint64_t)cJSON_GetObjectItemCaseSensitive(sample,"seq")->valuedouble;
            size_t i=0;while(i<count && ids[i]!=seq)++i;assert(i<count);
            cJSON *copy=cJSON_Duplicate(sample,true);cJSON_DeleteItemFromObjectCaseSensitive(copy,"seq");
            assert(cJSON_Compare(copy,expected[i],true));cJSON_Delete(copy);
            if(!cloud_seen[i]){cloud_seen[i]=true;++unique;}++delivered;ack=seq;
        }
        cJSON_Delete(batch);
        if(lost_ack){simulate_restart();lost_ack=false;continue;} /* Remote committed; ACK lost. */
        assert(tb_store_ack(ack)==ESP_OK);
        assert(tb_store_ack(ack)==ESP_OK); /* Retry ACK is idempotent. */
    }
    assert(unique==count && delivered>unique);
    for(size_t i=0;i<count;i++){assert(cloud_seen[i]);cJSON_Delete(expected[i]);}
    simulate_restart();assert(state.pending_records==0 && state.ready);
    printf("Preserved archive evidence after ACK: %llu bytes; remains on disk and reduces future offline space until explicit review\n",(unsigned long long)state.archived_bytes);
    append_json("{\"sample_kind\":\"BOOT_DIAGNOSTIC\",\"boot_id\":43}",7201000000ULL);
    assert(state.pending_records==1 && state.highest_committed>ids[count-1]);
    printf("Reconnect: unique_Cloud_rows=%zu deliveries_including_retries=%zu duplicates_in_Cloud=0 valid_pending=0 before new boot; isolated evidence preserved\n",unique,delivered);
    cleanup_store();return 0;
}
