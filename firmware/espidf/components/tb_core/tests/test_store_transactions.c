#define _POSIX_C_SOURCE 200809L
#define TB_STORE_HOST_TEST
#include "../store.c"
#include <assert.h>

void tb_fram_checkpoint(uint64_t reserved, uint64_t ack) { (void)reserved; (void)ack; }
const char *tb_fram_status(void) { return "host_not_present"; }

static char host_directory[64];

static void reset_store(void)
{
    memset(&state, 0, sizeof(state));
    memset(&host_config, 0, sizeof(host_config));
    host_nvs_ack = host_nvs_reserved = host_pending_ack = host_pending_reserved = 0;
    host_pending_clean = 0;
    host_queue_length = host_fsync_calls = host_commit_calls = 0;
    host_fail_fsync_once = host_fail_commit_once = false;
    snprintf(host_directory, sizeof(host_directory), "/tmp/tb-store-txn-XXXXXX");
    assert(mkdtemp(host_directory));
    state.base_path = host_directory;
    state.medium = "host";
    state.maximum_segments = 3;
    state.segment_bytes = 1024;
    state.next_segment_id = 1;
    state.next_sequence = 1;
    state.ready = true;host_allow_compression=true;
    state.mutex = state.queue = (void *)1;
}

static void cleanup_store(void)
{
    DIR *directory = opendir(host_directory);
    assert(directory);
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        uint32_t id;
        if (!file_id(entry->d_name, &id)) {
            if(strlen(entry->d_name)==12 && !strcmp(entry->d_name+8,".BAD")){char path[96];snprintf(path,sizeof(path),"%.64s/%.12s",host_directory,entry->d_name);assert(unlink(path)==0);}
            continue;
        }
        char path[96];
        segment_path(path, sizeof(path), id);
        assert(unlink(path) == 0);
    }
    closedir(directory);
    assert(rmdir(host_directory) == 0);
}

static void append_json(const char *json, uint64_t uptime)
{
    queued_record_t record = {.uptime_us = uptime};
    assert(strlen(json) < sizeof(record.json));
    strcpy(record.json, json);
    append_record(&record);
}

static void simulate_restart(void)
{
    state.ready = false;
    state.segment_count = state.pending_bytes = state.pending_records = 0;
    memset(state.segments, 0, sizeof(state.segments));
    state.highest_committed = state.highest_offered = 0;
    state.reserved_end = host_nvs_reserved;
    state.ack_sequence = host_nvs_ack;
    state.error[0] = 0;
    /* Only committed NVS values survive power loss. */
    host_pending_ack = host_nvs_ack;
    host_pending_reserved = host_nvs_reserved;
    assert(recover_records());
    state.ready = true;
}

static const cJSON *sample_at(const cJSON *batch, int index)
{
    return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(batch, "samples"), index);
}

static void test_no_gps_reboot_ack_and_late_gps(void)
{
    reset_store();
    append_json("{\"sample_kind\":\"RAW_J1939\",\"boot_id\":7,\"uptime_ms\":10,\"time_valid\":false,\"gps_status\":\"NO_FIX\",\"raw\":\"0102\"}", 10000);
    append_json("{\"sample_kind\":\"J1708\",\"boot_id\":7,\"uptime_ms\":20,\"time_valid\":false,\"pid\":190,\"value\":1000}", 20000);
    assert(state.pending_records == 2 && state.highest_committed == 2);
    assert(host_fsync_calls == 2);
    assert(tb_store_ack(2) == ESP_ERR_INVALID_ARG); /* not yet offered */
    simulate_restart();
    assert(state.highest_committed == 2 && state.next_sequence == 257);
    append_json("{\"sample_kind\":\"OPERATIONAL\",\"boot_id\":42,\"uptime_ms\":30,\"lat\":-23.5,\"lon\":-46.6}", 30000);
    assert(state.highest_committed == 257);
    cJSON *batch = tb_store_batch(32, 24576);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch, "samples")) == 3);
    assert(!cJSON_GetObjectItemCaseSensitive(sample_at(batch, 0), "lat"));
    assert(!cJSON_GetObjectItemCaseSensitive(sample_at(batch, 1), "lat"));
    assert(cJSON_GetObjectItemCaseSensitive(sample_at(batch, 0), "boot_id")->valuedouble == 7);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(sample_at(batch, 0), "time_valid")));
    assert(cJSON_GetObjectItemCaseSensitive(sample_at(batch, 2), "lat")->valuedouble == -23.5);
    cJSON_Delete(batch);
    assert(tb_store_ack(128) == ESP_ERR_INVALID_ARG); /* reservation hole */
    assert(state.ready && state.pending_records == 3 && host_nvs_ack == 0);
    host_fail_commit_once = true;
    assert(tb_store_ack(2) == ESP_FAIL);
    assert(state.pending_records == 3 && state.ack_sequence == 0 && host_nvs_ack == 0);
    assert(tb_store_ack(1) == ESP_OK && state.pending_records == 2);
    assert(tb_store_ack(1) == ESP_OK && state.pending_records == 2);
    assert(tb_store_ack(257) == ESP_OK && state.pending_records == 0);
    assert(state.segment_count == 0 && host_nvs_ack == 257);
    cleanup_store();
}

static void test_interrupted_tail_preserves_confirmed_records(void)
{
    reset_store();
    append_json("{\"raw\":\"18F00400\",\"time_valid\":false}", 11);
    char path[64];
    segment_path(path, sizeof(path), state.segments[0].id);
    FILE *file = fopen(path, "ab");
    assert(file);
    const char torn[] = "{\"seq\":2,\"uptime_us\":22,\"crc32\":";
    assert(fwrite(torn, 1, strlen(torn), file) == strlen(torn));
    assert(fclose(file) == 0);
    simulate_restart();
    assert(state.pending_records == 1 && state.highest_committed == 1);
    assert(state.isolated_bytes == strlen(torn) && state.corrupt_records>0);
    cJSON *batch = tb_store_batch(8, 12000);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch, "samples")) == 1);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(sample_at(batch, 0), "raw")->valuestring, "18F00400"));
    cJSON_Delete(batch);
    cleanup_store();
}

static void test_fsync_failure_never_offered_until_recovery(void)
{
    reset_store();
    host_fail_fsync_once = true;
    append_json("{\"raw\":\"AABB\"}", 1);
    assert(!state.ready && state.write_errors == 1 && state.highest_committed == 0);
    cJSON *batch = tb_store_batch(8, 12000);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(batch, "ok")));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch, "samples")) == 0);
    cJSON_Delete(batch);
    /* The bytes happened to reach this host filesystem; recovery verifies them
     * before they become offerable. Real media power loss remains physical. */
    simulate_restart();
    assert(state.highest_committed == 1 && state.pending_records == 1);
    cleanup_store();
}

static void test_complete_corruption_preserved(void)
{
    reset_store();
    append_json("{\"raw\":\"AAAA\"}", 1);
    char path[64];
    segment_path(path, sizeof(path), state.segments[0].id);
    FILE *file = fopen(path, "r+b");
    assert(file);
    char line[STORE_LINE_MAX];
    assert(fgets(line, sizeof(line), file));
    char *raw = strstr(line, "AAAA");
    assert(raw);
    long offset = raw - line;
    assert(fseek(file, offset, SEEK_SET) == 0 && fputc('B', file) == 'B');
    assert(fclose(file) == 0);
    struct stat before, after;
    assert(stat(path, &before) == 0);
    state.segment_count = state.pending_records = state.pending_bytes = 0;
    memset(state.segments, 0, sizeof(state.segments));
    assert(recover_records() && state.corrupt_records >= 1);
    assert(state.pending_records==0 && state.isolated_bytes>0);
    assert(stat(path, &after) == 0 && before.st_size == after.st_size);
    cleanup_store();
}

static void test_full_queue_retains_unacknowledged_files(void)
{
    reset_store();
    state.segment_bytes = 80;
    state.maximum_segments = 2;
    append_json("{\"raw\":\"0102\"}", 1);
    append_json("{\"raw\":\"0304\"}", 2);
    assert(state.segment_count == 2 && state.pending_records == 2);
    append_json("{\"raw\":\"0506\"}", 3);
    assert(state.dropped_full == 1 && state.highest_committed == 2);
    cJSON *batch = tb_store_batch(8, 12000);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(batch, "samples")) == 2);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(sample_at(batch, 0), "raw")->valuestring, "0102"));
    cJSON_Delete(batch);
    cleanup_store();
}

static void test_bounded_startup_buffer(void)
{
    reset_store();
    state.ready = false;
    state.initializing = true;
    for (unsigned i = 0; i < STORE_QUEUE; ++i)
        assert(tb_store_submit("{\"raw\":\"AABB\",\"time_valid\":false}") == ESP_OK);
    assert(host_queue_length == STORE_QUEUE && state.buffered_during_recovery == STORE_QUEUE);
    assert(tb_store_submit("{\"raw\":\"CCDD\"}") == ESP_ERR_NO_MEM);
    assert(state.dropped_queue == 1 && state.highest_committed == 0);
    state.initializing = false;
    assert(tb_store_submit("{\"raw\":\"EEFF\"}") == ESP_ERR_NO_MEM);
    const char *saved=state.base_path;state.base_path=NULL;
    assert(tb_store_submit("{\"raw\":\"EEFF\"}")==ESP_ERR_INVALID_STATE);state.base_path=saved;
    assert(state.dropped_unavailable == 1 && host_queue_length == STORE_QUEUE);
    cleanup_store();
}

static void test_operator_discard_preserves_nvs_sequence_and_nonlog_files(void)
{
    reset_store();
    append_json("{\"sample_kind\":\"RAW_J1939\",\"raw\":\"AAAA\"}", 100);
    append_json("{\"sample_kind\":\"RAW_J1708\",\"raw\":\"BBBB\"}", 200);
    assert(state.pending_records == 2 && state.segment_count == 1);
    uint64_t reserved = host_nvs_reserved, ack = host_nvs_ack;
    unsigned commits = host_commit_calls;
    char log_path[96], keep_path[96];
    segment_path(log_path, sizeof(log_path), state.segments[0].id);
    snprintf(keep_path, sizeof(keep_path), "%s/KEEP.TXT", host_directory);
    FILE *file = fopen(keep_path, "wb");
    assert(file && fputs("leave untouched", file) >= 0 && fclose(file) == 0);
    assert(discard_log_files());
    assert(state.purged_files == 1);
    assert(access(log_path, F_OK) == -1 && access(keep_path, F_OK) == 0);
    assert(host_nvs_reserved == reserved && host_nvs_ack == ack);
    assert(host_commit_calls == commits);
    assert(unlink(keep_path) == 0);
    simulate_restart();
    assert(state.pending_records == 0 && state.segment_count == 0);
    assert(state.next_sequence == reserved + 1);
    cleanup_store();
}

int main(void)
{
    test_no_gps_reboot_ack_and_late_gps();
    test_interrupted_tail_preserves_confirmed_records();
    test_fsync_failure_never_offered_until_recovery();
    test_complete_corruption_preserved();
    test_full_queue_retains_unacknowledged_files();
    test_bounded_startup_buffer();
    test_operator_discard_preserves_nvs_sequence_and_nonlog_files();
    puts("Storage transactions: no GPS/offline/reboot, late GPS immutability, ACK gaps/idempotence/NVS fault, fsync fault, torn tail, corruption, capacity and startup queue passed");
    return 0;
}
