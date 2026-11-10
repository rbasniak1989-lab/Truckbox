#define main transaction_main
#include "test_store_transactions.c"
#undef main
static void legacy_line(FILE *f,uint64_t seq,const char *data)
{
    assert(fprintf(f,"{\"seq\":%llu,\"uptime_us\":%llu,\"crc32\":%u,\"data\":%s}\n",
        (unsigned long long)seq,(unsigned long long)(seq*1000),tb_store_record_crc(seq,seq*1000,data,strlen(data)),data)>0);
}
static void legacy_fixture(unsigned mode)
{
    reset_store();state.maximum_segments=8;
    char unrelated[96];snprintf(unrelated,sizeof(unrelated),"%s/00000099.LOG.KEEP",host_directory);
    FILE *extra=fopen(unrelated,"wb");assert(extra);fputs("backup-preserved",extra);fclose(extra);
    char a[96],b[96];segment_path(a,sizeof(a),1);segment_path(b,sizeof(b),2);
    FILE *f=fopen(a,"wb");assert(f);legacy_line(f,1,"{\"boot_id\":90,\"uptime_ms\":1,\"raw\":\"valid-before\"}");
    if(mode==0)fputs("{\"seq\":2,\"uptime_us\":2000,\"crc32\":",f); /* Non-final incomplete .11 row. */
    if(mode==1){fputs("{\"seq\":2,\"uptime_us\":2000,\"crc32\":0,\"data\":{\"x\":\"",f);for(unsigned i=0;i<3000;i++)fputc('x',f);fputs("\"}}\n",f);}
    if(mode==2)fputs("{\"seq\":2,\"uptime_us\":2000,\"crc32\":0,\"data\":{\"raw\":\"bad-crc\"}}\n",f);
    legacy_line(f,3,"{\"boot_id\":90,\"uptime_ms\":3,\"raw\":\"valid-suffix\"}");assert(fclose(f)==0);
    f=fopen(b,"wb");assert(f);legacy_line(f,4,"{\"boot_id\":90,\"uptime_ms\":4,\"raw\":\"next-segment\"}");assert(fclose(f)==0);
    struct stat before;assert(stat(a,&before)==0);uint64_t checksum=0;
    f=fopen(a,"rb");int c;while((c=fgetc(f))!=EOF)checksum=checksum*31+(unsigned)c;fclose(f);
    simulate_restart();assert(state.ready && state.recovery_complete && state.pending_records==3);
    assert(state.isolated_bytes>0 && state.corrupt_records>0 && state.segments[0].quarantined);
    for(unsigned boot=0;boot<5;boot++){simulate_restart();assert(state.pending_records==3);}
    cJSON *batch=tb_store_batch(8,8192);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch,"samples"))==3);
    assert(cJSON_GetObjectItemCaseSensitive(sample_at(batch,0),"seq")->valuedouble==1);
    assert(cJSON_GetObjectItemCaseSensitive(sample_at(batch,1),"seq")->valuedouble==3);
    cJSON_Delete(batch);assert(tb_store_ack(2)==ESP_ERR_INVALID_ARG && state.pending_records==3);
    assert(tb_store_ack(4)==ESP_OK && state.pending_records==0); /* Only actually offered valid rows. */
    char archive[96];snprintf(archive,sizeof(archive),"%s/%08X.BAD",host_directory,1);
    struct stat after;assert(stat(archive,&after)==0 && after.st_size==before.st_size);
    uint64_t copied=0;f=fopen(archive,"rb");assert(f);while((c=fgetc(f))!=EOF)copied=copied*31+(unsigned)c;fclose(f);
    assert(checksum==copied);simulate_restart();assert(state.pending_records==0 && state.archived_bytes>0);
    append_json("{\"boot_id\":91,\"uptime_ms\":5,\"raw\":\"continue\"}",5000);
    assert(state.pending_records==1 && state.highest_committed>4);assert(access(unrelated,F_OK)==0);assert(unlink(unrelated)==0);cleanup_store();
}
static void powercuts(void)
{
    const char *payload="{\"sample_kind\":\"OPERATIONAL\",\"boot_id\":42,\"uptime_ms\":2000,\"time_valid\":false,\"speed_kmh\":65.5,\"rpm\":1300.25,\"gps_status\":\"NO_FIX\"}";
    reset_store();append_json(payload,2000000);
    char path[96],line[STORE_LINE_MAX];segment_path(path,sizeof(path),1);FILE *f=fopen(path,"rb");assert(f && fgets(line,sizeof(line),f));fclose(f);
    size_t n=strlen(line);cleanup_store();
    for(size_t cut=0;cut<=n;cut++){
        reset_store();
        segment_path(path,sizeof(path),1);f=fopen(path,"wb");assert(f);
        assert(fwrite(line,1,cut,f)==cut);assert(fclose(f)==0);
        simulate_restart();assert(state.ready && state.pending_records==(cut>=n-1?1u:0u));
        /* Append/reboot repeatedly to the original unmodified prefix. */
        append_json("{\"boot_id\":43,\"uptime_ms\":30,\"raw\":\"after-cut\"}",30000);
        assert(state.ready && state.pending_records==(cut>=n-1?2u:1u));
        simulate_restart();assert(state.pending_records==(cut>=n-1?2u:1u));cleanup_store();
    }
    printf("Power cuts: %zu byte boundaries, CRC-valid completion without newline, append/reboot after every cut PASS\n",n+1);
}
static void corruption_after_offer(void)
{
    reset_store();append_json("{\"raw\":\"AAAA\"}",1);append_json("{\"raw\":\"BBBB\"}",2);
    cJSON *b=tb_store_batch(8,8192);cJSON_Delete(b);
    char path[96],line[STORE_LINE_MAX];segment_path(path,sizeof(path),1);FILE *f=fopen(path,"r+b");assert(f && fgets(line,sizeof(line),f));
    char *p=strstr(line,"AAAA");assert(p && fseek(f,p-line,SEEK_SET)==0 && fputc('X',f)=='X');assert(fclose(f)==0);
    assert(tb_store_ack(2)!=ESP_OK && host_nvs_ack==0); /* Rescan before acknowledging changed evidence. */
    simulate_restart();assert(state.pending_records==1 && state.isolated_bytes>0);
    b=tb_store_batch(8,8192);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(b,"samples"))==1);cJSON_Delete(b);
    assert(tb_store_ack(2)==ESP_OK);cleanup_store();
}
static void archive_collision_and_mixed_formats(void)
{
    reset_store();state.maximum_segments=8;
    const char *sample="{\"sample_kind\":\"OPERATIONAL\",\"boot_id\":42,\"uptime_ms\":1000,\"time_valid\":false,\"gps_status\":\"NO_FIX\",\"speed_kmh\":65.5}";
    host_allow_compression=false;append_json(sample,1000000);host_allow_compression=true;append_json(sample,1000000);
    char path[96],line[STORE_LINE_MAX];segment_path(path,sizeof(path),1);
    FILE *f=fopen(path,"rb");assert(f && fgets(line,sizeof(line),f) && strstr(line,"\"uptime_us\":") && !strstr(line,"\"z2\":"));
    assert(fgets(line,sizeof(line),f) && strstr(line,"\"z2\":"));fclose(f);
    f=fopen(path,"ab");assert(f);fputs("torn-evidence",f);fclose(f);
    char bad[96];snprintf(bad,sizeof(bad),"%s/00000001.BAD",host_directory);f=fopen(bad,"wb");assert(f);fputs("older-archive",f);fclose(f);
    simulate_restart();assert(state.pending_records==2);
    cJSON *b=tb_store_batch(8,8192);assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(b,"samples"))==2);cJSON_Delete(b);
    assert(tb_store_ack(2)==ESP_OK);f=fopen(bad,"rb");assert(f && fgets(line,sizeof(line),f) && !strcmp(line,"older-archive"));fclose(f);
    snprintf(bad,sizeof(bad),"%s/00000002.BAD",host_directory);assert(access(bad,F_OK)==0);
    cleanup_store();
}
int main(void){archive_collision_and_mixed_formats();for(unsigned i=0;i<3;i++)legacy_fixture(i);powercuts();corruption_after_offer();puts("0.2.11-compatible non-final/oversized/CRC corruption, repeated resets, suffix salvage, ACK refusal and immutable evidence PASS");return 0;}
