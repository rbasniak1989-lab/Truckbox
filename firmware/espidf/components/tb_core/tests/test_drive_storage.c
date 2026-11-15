/* Drive replay through the real CRC writer, recovery, outbox and ACK code.
 * The remote endpoint is an idempotent in-memory mock, never production. */
#define main existing_store_suite_main
#include "test_store_transactions.c"
#undef main
#define MAX_DRIVE_ROWS 5000
static cJSON *expected[MAX_DRIVE_ROWS];
static uint64_t ids[MAX_DRIVE_ROWS];
static bool remote_seen[MAX_DRIVE_ROWS];
static size_t rows,unique_rows,deliveries,peak_pending,peak_bytes;
static bool lose_ack=true;
static void reconnect(void)
{
    while(state.pending_records){
        cJSON *batch=tb_store_batch(8,8192);assert(batch);
        assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(batch,"ok")));
        const cJSON *samples=cJSON_GetObjectItemCaseSensitive(batch,"samples"),*sample;
        assert(cJSON_GetArraySize(samples)>0);uint64_t ack=0;
        cJSON_ArrayForEach(sample,samples){
            uint64_t id=(uint64_t)cJSON_GetObjectItemCaseSensitive(sample,"seq")->valuedouble;
            size_t i=0;while(i<rows && ids[i]!=id)++i;assert(i<rows);
            cJSON *copy=cJSON_Duplicate(sample,true);assert(copy);
            cJSON_DeleteItemFromObjectCaseSensitive(copy,"seq");
            assert(cJSON_Compare(copy,expected[i],true));cJSON_Delete(copy);
            if(!remote_seen[i]){remote_seen[i]=true;++unique_rows;}
            ++deliveries;ack=id;
        }
        cJSON_Delete(batch);
        if(lose_ack){lose_ack=false;simulate_restart();continue;}
        assert(tb_store_ack(ack)==ESP_OK && tb_store_ack(ack)==ESP_OK);
    }
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);reset_store();
    state.segment_bytes=48u*1024u;state.maximum_segments=37;
    state.total_bytes=0x1e0000;state.recovery_complete=true;
    unsigned power_cuts=0;uint64_t previous_elapsed=0;char text[TB_RECORD_MAX+2];
    while(fgets(text,sizeof(text),f)){
        size_t n=strlen(text);assert(n && text[n-1]=='\n');text[--n]=0;
        assert(rows<MAX_DRIVE_ROWS);expected[rows]=cJSON_Parse(text);assert(expected[rows]);
        const cJSON *elapsed=cJSON_GetObjectItemCaseSensitive(expected[rows],"replay_elapsed_ms");
        const cJSON *uptime=cJSON_GetObjectItemCaseSensitive(expected[rows],"uptime_ms");
        assert(cJSON_IsNumber(elapsed));uint64_t now=(uint64_t)elapsed->valuedouble;
        assert(now>=previous_elapsed);previous_elapsed=now;
        append_json(text,uptime?(uint64_t)uptime->valuedouble*1000:0);
        assert(state.ready && !state.dropped_full && !state.write_errors);
        ids[rows]=state.highest_committed;
        if(rows)assert(ids[rows]>ids[rows-1]);
        ++rows;
        if(state.pending_records>peak_pending)peak_pending=state.pending_records;
        if(state.pending_bytes>peak_bytes)peak_bytes=state.pending_bytes;
        if(power_cuts<2 && now>=(power_cuts?7200000u:3600000u)){
            char path[96];segment_path(path,sizeof(path),state.segments[state.segment_count-1].id);
            FILE *tail=fopen(path,"ab");assert(tail);
            assert(fputs("{\"seq\":999999,\"uptime_us\":0,\"crc32\":",tail)>=0 && fclose(tail)==0);
            size_t pending=state.pending_records;simulate_restart();++power_cuts;
            assert(state.pending_records==pending && state.isolated_bytes>0);
        }
        if(now<1800000u || now>=9000000u)reconnect(); /* Two hours offline. */
    }
    assert(fclose(f)==0 && previous_elapsed==12000000 && rows>=1200 && power_cuts==2);
    reconnect();simulate_restart();assert(state.ready && state.pending_records==0);
    assert(unique_rows==rows && deliveries>rows && !state.dropped_full && !state.write_errors);
    for(size_t i=0;i<rows;i++){assert(remote_seen[i]);cJSON_Delete(expected[i]);}
    printf("Drive300 outbox: offline=7200s rows=%zu unique_mock_Cloud=%zu deliveries=%zu duplicates_in_Cloud=0 peak_pending=%zu peak_bytes=%zu torn_write_reboots=%u valid_pending=0 evidence_preserved=%llu PASS\n",
        rows,unique_rows,deliveries,peak_pending,peak_bytes,power_cuts,(unsigned long long)state.archived_bytes);
    cleanup_store();return 0;
}
