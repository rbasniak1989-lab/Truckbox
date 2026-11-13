#ifndef TB_STORE_HOST_TEST
#include "tb.h"
#endif
#include "store_internal.h"
#include "store_codec.h"
#include "tb_task_budget.h"
#include "fram.h"

#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef TB_STORE_HOST_TEST
#include "tests/store_host_stubs.h"
#else
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdmmc_cmd.h"
#include "wear_levelling.h"
#include "diskio_wl.h"
#include "ff.h"
#endif

#define STORE_FLASH_SEGMENT_BYTES (48U * 1024U)
#define STORE_SD_SEGMENT_BYTES (1024U * 1024U)
#define STORE_SEGMENTS 128U
#define STORE_QUEUE 64U
#define STORE_LINE_MAX (TB_RECORD_MAX + 192U)
#define STORE_RESERVATION 256U
#define STORE_MAX_SEQUENCE UINT64_C(9007199254740991)

typedef struct {
    uint32_t id;
    uint64_t minimum;
    uint64_t maximum;
    size_t bytes;
    size_t records;
    size_t pending_offset;
    bool quarantined;
} segment_t;

typedef struct {
    uint64_t uptime_us;
    char json[TB_RECORD_MAX];
} queued_record_t;

typedef struct {
    const char *stage;
    int error, close_error;
    uint32_t segment;
    uint64_t sequence, at_us;
    size_t requested, written;
} store_io_failure_t;

typedef struct {
    SemaphoreHandle_t mutex;
    QueueHandle_t queue;
    nvs_handle_t nvs;
    bool nvs_open;
    bool medium_clean;
    volatile bool ready;
    volatile bool initializing;
    bool spi_ready,medium_initialized;
    const char *base_path;
    const char *medium;
    char error[96];
    char sd_error[64];
    char previous_medium[8];
    char previous_card_id[32];
    char card_id[32];
    char sd_directory[64];
    const char *fram_gate_status;
    bool fram_attempted;
    segment_t segments[STORE_SEGMENTS];
    size_t segment_count;
    size_t maximum_segments;
    size_t segment_bytes;
    uint32_t next_segment_id;
    uint64_t next_sequence;
    uint64_t reserved_end;
    uint64_t ack_sequence;
    uint64_t highest_committed;
    uint64_t highest_offered;
    size_t pending_bytes;
    size_t pending_records;
    uint64_t committed_this_boot;
    uint64_t recovered_tail_bytes;
    uint64_t corrupt_records;
    uint64_t isolated_bytes, isolated_files, recovered_records;
    bool recovery_complete,capacity_blocked;
    volatile bool rollback_paused,rollback_drained;
    uint64_t total_bytes, available_bytes, oldest_pending_uptime_us, archived_bytes;
    uint64_t oldest_pending_sequence;
    uint32_t oldest_pending_boot;
    char oldest_pending_ts[32];
    bool oldest_pending_known;
    uint64_t last_ack_us, last_append_us, stored_bytes_this_boot;
    uint64_t write_errors;
    store_io_failure_t last_io_failure;
    bool volume_rebind_required;
    uint64_t recovery_attempts, recovery_successes, volume_rebinds;
    uint64_t last_recovery_us;
    int volume_result;
    uint64_t dropped_queue;
    uint64_t dropped_unavailable;
    uint64_t dropped_full;
    uint64_t invalid_input;
    uint64_t scheduling_yields;
    uint64_t buffered_during_recovery;
    volatile uint32_t recovery_segments;
    bool purged_this_boot;
    uint32_t purged_files;
    wl_handle_t flash_handle;
    sdmmc_card_t *sd_card;
} store_state_t;

static store_state_t state;
static const char *TAG = "tb_store";
static portMUX_TYPE counter_lock = portMUX_INITIALIZER_UNLOCKED;

static void increment(uint64_t *counter)
{
    portENTER_CRITICAL(&counter_lock);
    ++*counter;
    portEXIT_CRITICAL(&counter_lock);
}

static uint64_t counter_value(const uint64_t *counter)
{
    portENTER_CRITICAL(&counter_lock);
    uint64_t result = *counter;
    portEXIT_CRITICAL(&counter_lock);
    return result;
}

static void fail_store(const char *message)
{
    state.ready = false;
    snprintf(state.error, sizeof(state.error), "%s", message);
    ESP_LOGE(TAG, "%s", message);
}

static void failed_record_io(const char *stage, int error, int close_error,
                             uint32_t segment, uint64_t sequence,
                             size_t requested, size_t written)
{
    state.last_io_failure=(store_io_failure_t){
        .stage=stage,.error=error,.close_error=close_error,
        .segment=segment,.sequence=sequence,.requested=requested,.written=written,
        .at_us=(uint64_t)esp_timer_get_time()
    };
    /* A read-only scan cannot clear FatFs' in-memory window or sharing locks
     * left by an unsuccessful close. Rebind this FLASH volume before scanning;
     * do not reset the ESP32, change media or format an existing filesystem. */
    if(state.medium && !strcmp(state.medium,"flash"))state.volume_rebind_required=true;
    ++state.write_errors;
    fail_store("record write failed; stopped to preserve data");
    ESP_LOGE(TAG,"record IO stage=%s errno=%d close_errno=%d seg=%" PRIu32
        " seq=%" PRIu64 " bytes=%u/%u boot=%" PRIu32,
        stage,error,close_error,segment,sequence,(unsigned)written,(unsigned)requested,tb_boot_id());
}

static void segment_path(char *path, size_t capacity, uint32_t id)
{
    snprintf(path, capacity, "%s/%08" PRIX32 ".LOG", state.base_path, id);
}

static bool file_id(const char *name, uint32_t *id)
{
    if (strlen(name) != 12 || strcasecmp(name + 8, ".LOG") != 0) {
        return false;
    }
    uint32_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned digit;
        if (name[i] >= '0' && name[i] <= '9') {
            digit = (unsigned)(name[i] - '0');
        } else if (name[i] >= 'A' && name[i] <= 'F') {
            digit = (unsigned)(name[i] - 'A') + 10;
        } else if (name[i] >= 'a' && name[i] <= 'f') {
            digit = (unsigned)(name[i] - 'a') + 10;
        } else {
            return false;
        }
        value = (value << 4) | digit;
    }
    *id = value;
    return true;
}

static int compare_segments(const void *left, const void *right)
{
    const segment_t *a = left;
    const segment_t *b = right;
    return a->id < b->id ? -1 : a->id > b->id;
}

static void oldest_metadata(const cJSON *data,uint64_t seq)
{
    uint64_t u=0,boot=0;
    const cJSON *stamp=cJSON_GetObjectItemCaseSensitive(data,"ts");
    state.oldest_pending_sequence=seq;
    /* get_u64_json is defined below; valid generated samples use numbers. */
    const cJSON *uptime=cJSON_GetObjectItemCaseSensitive(data,"uptime_ms");
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(data,"boot_id");
    bool valid=cJSON_IsNumber(uptime) && isfinite(uptime->valuedouble) && uptime->valuedouble>=0 && uptime->valuedouble<=(double)STORE_MAX_SEQUENCE/1000.0 &&
        cJSON_IsNumber(id) && id->valuedouble>=0 && id->valuedouble<=UINT32_MAX;
    if(valid){u=(uint64_t)uptime->valuedouble;boot=(uint64_t)id->valuedouble;}
    state.oldest_pending_uptime_us=u*1000u;state.oldest_pending_boot=(uint32_t)boot;
    state.oldest_pending_known=valid;
    snprintf(state.oldest_pending_ts,sizeof(state.oldest_pending_ts),"%s",cJSON_IsString(stamp)?stamp->valuestring:"");
}

static bool get_u64_json(const cJSON *object, const char *name, uint64_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
        item->valuedouble < 0 || item->valuedouble > (double)STORE_MAX_SEQUENCE) {
        return false;
    }
    uint64_t parsed = (uint64_t)item->valuedouble;
    if ((double)parsed != item->valuedouble) {
        return false;
    }
    *value = parsed;
    return true;
}

/* Our fixed wrapper has numeric metadata before data, so the first data marker
 * is unambiguous. Verify CRC against the stored byte span, not re-serialized
 * floating point numbers, preserving compatibility across cJSON upgrades. */
static cJSON *read_record(const char *line, size_t length, uint64_t *sequence)
{
    /* Compact format2: the CRC covers the encoded bytes and numeric metadata.
     * Expand back to the v1 in-memory shape before any uploader sees it. */
    const char *first_comma=length>7?memchr(line+7,',',length-7):NULL;
    if(first_comma && !strncmp(first_comma,",\"c\":",5)){
        uint64_t seq=0;uint32_t crc=0;int start=0;
        if(sscanf(line,"{\"seq\":%" SCNu64 ",\"c\":%" SCNu32 ",\"z2\":\"%n",&seq,&crc,&start)!=2 || start<=0 || !seq || seq>STORE_MAX_SEQUENCE)return NULL;
        size_t offset=(size_t)start;
        if(length<offset+3)return NULL;
        const char *end=strchr(line+offset,'"');
        if(!end || strcmp(end,"\"}\n") || (size_t)(end-line-offset)>=TB_RECORD_MAX)return NULL;
        char packed[TB_RECORD_MAX],raw[TB_RECORD_MAX];size_t n=(size_t)(end-line)-offset;
        memcpy(packed,line+offset,n);packed[n]=0;
        if((tb_store_record_crc(seq,0,packed,n)^UINT32_C(0xa21c5e37))!=crc || !tb_store_unpack(packed,raw,sizeof(raw)))return NULL;
        cJSON *data=cJSON_ParseWithOpts(raw,NULL,true);if(!data){errno=ENOMEM;return NULL;}
        uint64_t uptime=0,boot=0;
        if(!cJSON_IsObject(data) || !get_u64_json(data,"uptime_ms",&uptime) || !get_u64_json(data,"boot_id",&boot) || uptime>STORE_MAX_SEQUENCE/1000u){cJSON_Delete(data);return NULL;}
        cJSON *record=cJSON_CreateObject();
        if(!record || !cJSON_AddNumberToObject(record,"seq",(double)seq) || !cJSON_AddNumberToObject(record,"uptime_us",(double)uptime*1000.0) || !cJSON_AddItemToObject(record,"data",data)){
            cJSON_Delete(data);cJSON_Delete(record);errno=ENOMEM;return NULL;
        }
        *sequence=seq;return record;
    }
    const char *data = NULL;
    size_t payload_length = 0;
    if (length >= STORE_LINE_MAX ||
        !tb_store_payload_span(line, length, &data, &payload_length)) {
        return NULL;
    }
    if (!payload_length || payload_length >= TB_RECORD_MAX) {
        return NULL;
    }
    uint64_t meta_seq=0,meta_time=0;uint32_t meta_crc=0;
    if(sscanf(line,"{\"seq\":%" SCNu64 ",\"uptime_us\":%" SCNu64 ",\"crc32\":%" SCNu32,
              &meta_seq,&meta_time,&meta_crc)!=3)return NULL;
    bool compact=strstr(line,",\"v\":2,\"data\":") &&
        strstr(line,",\"v\":2,\"data\":")<data;
    uint32_t expected=tb_store_record_crc(meta_seq,meta_time,data,payload_length);
    if(compact)expected^=UINT32_C(0xa21c5e37);
    if(expected!=meta_crc)return NULL;
    cJSON *object = cJSON_ParseWithOpts(line, NULL, true);
    if(!object){errno=ENOMEM;return NULL;}
    uint64_t crc = 0;
    uint64_t uptime = 0;
    bool valid = cJSON_IsObject(object) &&
                 get_u64_json(object, "seq", sequence) && *sequence > 0 &&
                 get_u64_json(object, "uptime_us", &uptime) &&
                 get_u64_json(object, "crc32", &crc) && crc <= UINT32_MAX &&
                 cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(object, "data")) &&
                 (tb_store_record_crc(*sequence, uptime, data, payload_length)^
                     (compact?UINT32_C(0xa21c5e37):0))==(uint32_t)crc;
    if (!valid) {
        cJSON_Delete(object);
        return NULL;
    }
    if(compact){
        const cJSON *z=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(object,"data"),"z");
        char raw[TB_RECORD_MAX];
        if(!cJSON_IsString(z) || !tb_store_unpack(z->valuestring,raw,sizeof(raw))){cJSON_Delete(object);return NULL;}
        cJSON *expanded=cJSON_ParseWithOpts(raw,NULL,true);
        if(!expanded){cJSON_Delete(object);errno=ENOMEM;return NULL;}
        if(!cJSON_IsObject(expanded)){cJSON_Delete(expanded);cJSON_Delete(object);return NULL;}
        cJSON_ReplaceItemInObjectCaseSensitive(object,"data",expanded);
    }
    return object;
}

/* Read-only salvage. Original files are never truncated or rewritten. A complete
 * CRC-valid wrapper can survive a missing newline, even in a non-final segment.
 * Malformed prefixes are resynchronized to the next bounded wrapper. */
typedef struct { long start,end;size_t bytes;bool skipped; } record_span_t;
static cJSON *next_record(FILE *file,uint64_t *sequence,record_span_t *span)
{
    static const char prefix[]={123,34,115,101,113,34,58,0};
    bool skipped=false;unsigned matched=0;size_t searched=0;
    int ch;
    while((ch=fgetc(file))!=EOF){
        if(++searched%4096==0)vTaskDelay(1);
        if(ch==(unsigned char)prefix[matched])++matched;
        else {skipped=true;matched=ch=='{'?1u:0u;}
        if(matched!=sizeof(prefix)-1)continue;
        long start=ftell(file)-(long)(sizeof(prefix)-1);
        char line[STORE_LINE_MAX];size_t used=sizeof(prefix)-1;
        memcpy(line,prefix,used);unsigned depth=1;bool quoted=false,escaped=false;
        while(used+2<sizeof(line) && depth && (ch=fgetc(file))!=EOF){
            line[used++]=(char)ch;
            if(quoted){if(escaped)escaped=false;else if(ch=='\\')escaped=true;else if(ch=='"')quoted=false;}
            else if(ch=='"')quoted=true;
            else if(ch=='{' || ch=='[')++depth;
            else if(ch=='}' || ch==']')--depth;
        }
        long end=ftell(file);
        if(!depth){
            ch=fgetc(file);
            if(ch=='\n')end=ftell(file);else if(ch!=EOF)ungetc(ch,file);
            line[used++]='\n';line[used]=0;errno=0;
            cJSON *record=read_record(line,used,sequence);
            if(record){*span=(record_span_t){.start=start,.end=end,.bytes=(size_t)(end-start),.skipped=skipped};return record;}
            if(errno==ENOMEM)return NULL;
        }else line[used]=0;
        skipped=true;
        /* A torn write followed by a new complete record need not have a
         * newline. Preserve the bad bytes, retry any nested wrapper prefix. */
        const char *nested=strstr(line+1,prefix);
        long resume=nested?start+(long)(nested-line):ftell(file);
        if(depth && !nested && used>=sizeof(prefix))resume=start+(long)used-(long)sizeof(prefix)+1;
        clearerr(file);
        if(fseek(file,resume,SEEK_SET)!=0)return NULL;
        matched=0;
    }
    span->skipped=skipped;return NULL;
}

#ifndef TB_STORE_HOST_TEST
static bool partition_erased(const esp_partition_t *partition)
{
    unsigned char *buffer = malloc(4096);
    if (!buffer) {
        return false;
    }
    bool erased = true;
    for (size_t offset = 0; offset < partition->size && erased; offset += 4096) {
        size_t length = partition->size - offset;
        if (length > 4096) {
            length = 4096;
        }
        if (esp_partition_read(partition, offset, buffer, length) != ESP_OK) {
            erased = false;
            break;
        }
        for (size_t i = 0; i < length; ++i) {
            if (buffer[i] != 0xff) {
                erased = false;
                break;
            }
        }
        if ((offset & UINT32_C(0x1ffff)) == 0) {
            vTaskDelay(1);
        }
    }
    free(buffer);
    return erased;
}

static esp_err_t initialize_spi(void)
{
    if (state.spi_ready) {
        return ESP_OK;
    }
    gpio_set_level(GPIO_NUM_3, 1);
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 1);
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    spi_bus_config_t bus = {
        .mosi_io_num = 7,
        .miso_io_num = 8,
        .sclk_io_num = 6,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    esp_err_t error = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (error == ESP_OK) {
        state.spi_ready = true;
    }
    return error;
}

static bool ensure_directory(const char *path)
{
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    struct stat info;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool sd_device_directory(void)
{
    /* Keep cards moved between devices from relabelling old telemetry with a
     * new device UID. Both MAC halves fit FAT 8.3 directory names. */
    const char *uid = tb_device_uid();
    size_t length = strlen(uid);
    if (length < 12) {
        return false;
    }
    const char *mac = uid + length - 12;
    for (unsigned i = 0; i < 12; ++i) {
        if (!isxdigit((unsigned char)mac[i])) {
            return false;
        }
    }
    char first[64];
    snprintf(first, sizeof(first), "/tbsd/TRUCKBOX/%.6s", mac);
    snprintf(state.sd_directory, sizeof(state.sd_directory),
             "/tbsd/TRUCKBOX/%.6s/%.6s", mac, mac + 6);
    return ensure_directory("/tbsd/TRUCKBOX") &&
           ensure_directory(first) && ensure_directory(state.sd_directory);
}

static bool mount_storage(void)
{
    const tb_config_t *config = tb_config();
    bool require_sd = !state.medium_clean &&
                      strcmp(state.previous_medium, "sd") == 0;
    bool require_flash = !state.medium_clean &&
                         strcmp(state.previous_medium, "flash") == 0;
    if (require_sd && (!config->sd_enabled || !config->repaired_power_gates)) {
        snprintf(state.error, sizeof(state.error),
                 "pending SD data requires SD and confirmed repaired power gates");
        return false;
    }
    if (!require_flash && config->sd_enabled && config->repaired_power_gates) {
        /* This output is forbidden on the unreworked B1: its MOSFET G/S pads
         * are mirrored. The user must explicitly attest corrected hardware. */
        gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_17, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_err_t error = initialize_spi();
        if (error == ESP_OK) {
            sdmmc_host_t host = SDSPI_HOST_DEFAULT();
            host.slot = SPI2_HOST;
            host.max_freq_khz = 10000;
            sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
            slot.host_id = SPI2_HOST;
            slot.gpio_cs = 3;
            esp_vfs_fat_mount_config_t mount = {
                .format_if_mount_failed = false,
                .max_files = 5,
                .allocation_unit_size = 4096,
            };
            error = esp_vfs_fat_sdspi_mount("/tbsd", &host, &slot, &mount,
                                            &state.sd_card);
        }
        if (error == ESP_OK) {
            snprintf(state.card_id, sizeof(state.card_id), "%02x-%04x-%08" PRIx32,
                     (unsigned)state.sd_card->cid.mfg_id,
                     (unsigned)state.sd_card->cid.oem_id,
                     (uint32_t)state.sd_card->cid.serial);
            if (require_sd &&
                (!state.previous_card_id[0] ||
                 strcmp(state.card_id, state.previous_card_id) != 0)) {
                snprintf(state.error, sizeof(state.error),
                         "pending SD data requires the original card");
                return false;
            }
            if (sd_device_directory()) {
                state.base_path = state.sd_directory;
                state.medium = "sd";
                state.maximum_segments = STORE_SEGMENTS;
                state.segment_bytes = STORE_SD_SEGMENT_BYTES;
                return true;
            }
            error = ESP_FAIL;
        }
        snprintf(state.sd_error, sizeof(state.sd_error), "%s", esp_err_to_name(error));
        if (require_sd) {
            snprintf(state.error, sizeof(state.error),
                     "pending SD data unavailable; no unsafe backend switch");
            return false;
        }
        ESP_LOGW(TAG, "SD unavailable (%s); trying internal flash", state.sd_error);
    } else if (require_flash && config->sd_enabled) {
        snprintf(state.sd_error, sizeof(state.sd_error),
                 "retaining flash backlog; acknowledge it before switching");
    } else if (config->sd_enabled) {
        snprintf(state.sd_error, sizeof(state.sd_error), "B1 power gates not confirmed repaired");
    }

    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "storage");
    if (!partition) {
        snprintf(state.error, sizeof(state.error), "storage partition missing");
        return false;
    }
    /* Check BEFORE calling WL mount: WL initialization itself writes metadata
     * even if the FAT partition is otherwise completely erased. */
    bool blank = partition_erased(partition);
    esp_vfs_fat_mount_config_t mount = {
        .format_if_mount_failed = blank,
        .max_files = 5,
        .allocation_unit_size = 4096,
    };
    esp_err_t error = esp_vfs_fat_spiflash_mount_rw_wl(
        "/tbflash", "storage", &mount, &state.flash_handle);
    if (error != ESP_OK) {
        snprintf(state.error, sizeof(state.error), "flash mount: %s; existing data not formatted",
                 esp_err_to_name(error));
        return false;
    }
    state.base_path = "/tbflash";
    state.medium = "flash";
    /* Use the existing partition only. Keep128 KiB for FAT/WL metadata,
     * cluster rounding and filesystem headroom; never migrate partitions OTA. */
    size_t reserve=128u*1024u;
    size_t flash_max=partition->size>reserve?(partition->size-reserve)/STORE_FLASH_SEGMENT_BYTES:24;
    if(flash_max>STORE_SEGMENTS)flash_max=STORE_SEGMENTS;
    if(flash_max<24)flash_max=24;
    (void)esp_vfs_fat_info(state.base_path,&state.total_bytes,&state.available_bytes);
    state.maximum_segments = flash_max;
    state.segment_bytes = STORE_FLASH_SEGMENT_BYTES;
    return true;
}

/* All storage FILEs/DIRs have been closed and state.mutex is held. Keep the
 * existing VFS and wear-levelling registration, but invalidate FatFs' cached
 * volume/locks and mount the SAME filesystem again. f_mount never formats.
 * Confirmed rows already crossed fflush/fsync/close; the only ambiguous row
 * stays in the worker's retained buffer until a fresh CRC scan finds it. */
static int rebind_flash_volume(wl_handle_t handle)
{
    unsigned char drive=ff_diskio_get_pdrv_wl(handle);
    if(drive==0xff)return FR_INVALID_DRIVE;
    char path[5];snprintf(path,sizeof(path),"%u:",(unsigned)drive);
    FATFS *fs=NULL;DWORD free_clusters=0;
    FRESULT result=f_getfree(path,&free_clusters,&fs);
    /* f_getfree can return a read error after giving us the registered fs.
     * Without that pointer, leave the registration untouched and retry later. */
    if(!fs)return result==FR_OK?FR_INVALID_OBJECT:result;
    return f_mount(fs,path,1);
}

#endif

/* Delete ONLY our named telemetry .LOG segments, never other files or FAT/NVS.
 * Performed at boot, before recovery and before storage starts accepting data.
 * The NVS marker survives a power failure; a subsequent boot resumes deletion.
 * Sequence reservations remain unchanged so old cloud IDs are never reused. */
static bool __attribute__((unused)) discard_log_files(void)
{
    DIR *directory = opendir(state.base_path);
    if (!directory) { fail_store("discard: cannot enumerate storage"); return false; }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        uint32_t id;
        if (!file_id(entry->d_name, &id)) continue;
        char path[96];
        segment_path(path, sizeof(path), id);
        if (unlink(path) != 0 && errno != ENOENT) {
            closedir(directory);
            fail_store("discard: cannot delete telemetry segment; retry at next boot");
            return false;
        }
        ++state.purged_files;
    }
    if (closedir(directory) != 0) {
        fail_store("discard: cannot close storage directory");
        return false;
    }
    return true;
}

static bool recover_records(void)
{
    state.recovery_complete=false;
    state.segment_count=state.pending_bytes=state.pending_records=0;
    state.oldest_pending_uptime_us=0;state.oldest_pending_boot=0;state.oldest_pending_known=false;
    state.oldest_pending_sequence=0;state.oldest_pending_ts[0]=0;
    state.corrupt_records=state.isolated_bytes=state.isolated_files=state.recovered_records=state.archived_bytes=0;
    state.highest_offered=0;
    memset(state.segments,0,sizeof(state.segments));
    DIR *directory=opendir(state.base_path);
    if(!directory){fail_store("cannot enumerate storage");return false;}
    uint32_t largest_id=0;struct dirent *entry;
    while((entry=readdir(directory))){
        if(strlen(entry->d_name)!=12)continue; /* Preserve unrelated/backup files without treating a prefix as a LOG. */
        uint32_t id;char name[13];snprintf(name,sizeof(name),"%.12s",entry->d_name);
        bool bad=strlen(name)==12 && !strcasecmp(name+8,".BAD");
        if(bad)memcpy(name+8,".LOG",5);
        if(!file_id(name,&id))continue;
        if(id>largest_id)largest_id=id;
        if(bad){
            char path[128];struct stat info;snprintf(path,sizeof(path),"%.64s/%.12s",state.base_path,entry->d_name);
            if(stat(path,&info)==0){++state.isolated_files;state.isolated_bytes+=(uint64_t)info.st_size;state.archived_bytes+=(uint64_t)info.st_size;}
            continue;
        }
        if(state.segment_count>=STORE_SEGMENTS){closedir(directory);fail_store("too many segments; pending count unknown");return false;}
        state.segments[state.segment_count++].id=id;
    }
    closedir(directory);
    qsort(state.segments,state.segment_count,sizeof(state.segments[0]),compare_segments);
    uint64_t previous=0;
    for(size_t i=0;i<state.segment_count;i++){
        segment_t *segment=&state.segments[i];char path[96];segment_path(path,sizeof(path),segment->id);
        FILE *file=fopen(path,"rb");struct stat info;
        if(!file || fstat(fileno(file),&info)!=0){if(file)fclose(file);fail_store("cannot read existing segment");return false;}
        segment->bytes=(size_t)info.st_size;
        uint64_t seq;size_t valid_bytes=0;bool damaged=false;record_span_t span={0};
        cJSON *record;errno=0;
        while((record=next_record(file,&seq,&span))){
            if(seq<=previous){damaged=true;++state.corrupt_records;cJSON_Delete(record);continue;}
            if(span.skipped){damaged=true;++state.corrupt_records;}

            previous=seq;valid_bytes+=span.bytes;
            if(!segment->records)segment->minimum=seq;
            if(seq>state.ack_sequence){
                if(!state.pending_records){
                    oldest_metadata(cJSON_GetObjectItemCaseSensitive(record,"data"),seq);
                }
                if(segment->maximum<=state.ack_sequence)segment->pending_offset=(size_t)span.start;
                ++state.pending_records;state.pending_bytes+=span.bytes;
            }
            segment->maximum=seq;++segment->records;cJSON_Delete(record);
            if((segment->records&63u)==0)vTaskDelay(1);
            errno=0;
        }
        bool error=ferror(file)!=0 || errno==ENOMEM;
        fclose(file);
        if(error){fail_store("recovery read/allocation failed; pending count unknown");return false;}
        if(valid_bytes<segment->bytes){damaged=true;state.isolated_bytes+=segment->bytes-valid_bytes;}
        if(span.skipped || (!segment->records && segment->bytes)){++state.corrupt_records;damaged=true;}
        if(damaged){segment->quarantined=true;++state.isolated_files;state.recovered_records+=segment->records;}
        if(segment->maximum<=state.ack_sequence)segment->pending_offset=segment->bytes;
        state.recovery_segments=(uint32_t)(i+1);vTaskDelay(1);
    }
    state.highest_committed=previous>state.ack_sequence?previous:state.ack_sequence;
    state.next_segment_id=largest_id+1;
    if(!state.next_segment_id){fail_store("segment identifier exhausted");return false;}
    if(state.highest_committed>state.reserved_end){
        if(nvs_set_u64(state.nvs,"reserved",state.highest_committed)!=ESP_OK || nvs_commit(state.nvs)!=ESP_OK){fail_store("cannot reserve recovered sequences");return false;}
        state.reserved_end=state.highest_committed;
    }
    state.next_sequence=state.reserved_end+1;
    if(tb_config()->sequence_floor>state.next_sequence)state.next_sequence=tb_config()->sequence_floor;
    state.recovery_complete=true;state.error[0]=0;return true;
}

static bool __attribute__((unused)) recover_after_failure(void)
{
    ++state.recovery_attempts;
    state.last_recovery_us=(uint64_t)esp_timer_get_time();
    uint64_t next_before=state.next_sequence;
    if(state.volume_rebind_required){
        state.recovery_complete=false;
        state.volume_result=rebind_flash_volume(state.flash_handle);
        if(state.volume_result!=0){
            fail_store("flash volume rebind failed; data preserved");
            return false;
        }
        ++state.volume_rebinds;
        state.volume_rebind_required=false;
    }
    if(!recover_records())return false;
    /* This is the SAME boot with one writer: consume the unused part of its
     * already durable reservation, never the failed sequence itself. A retry
     * every 5s must not reserve another 256 IDs/commit NVS each time. Startup
     * still skips the persisted reservation, and newly discovered sequences
     * or a changed floor always win. */
    if(next_before>state.highest_committed && next_before<=state.reserved_end+1 &&
       next_before>=tb_config()->sequence_floor)state.next_sequence=next_before;
    ++state.recovery_successes;
    return true;
}

/* Called only under state.mutex, and only after the ack has been committed. */
static void prune_segments(void)
{
    size_t removed = 0;
    for (size_t i = 0; i < state.segment_count; ++i) {
        segment_t *segment = &state.segments[i];
        if (segment->records && segment->maximum > state.ack_sequence) {
            break;
        }
        char path[64];
        segment_path(path, sizeof(path), segment->id);
        char bad[96];snprintf(bad,sizeof(bad),"%s/%08" PRIX32 ".BAD",state.base_path,segment->id);
        /* Older firmware can reuse a LOG id after rollback. Never overwrite
         * an existing forensic archive when upgrading again. */
        while(segment->quarantined && access(bad,F_OK)==0){
            if(!state.next_segment_id){snprintf(state.error,sizeof(state.error),"archive identifier exhausted");return;}
            snprintf(bad,sizeof(bad),"%s/%08" PRIX32 ".BAD",state.base_path,state.next_segment_id++);
        }
        int result=segment->quarantined?rename(path,bad):unlink(path);
        if (result != 0 && errno != ENOENT) {
            snprintf(state.error, sizeof(state.error), "cannot prune acknowledged segment");
            break;
        }
        if(segment->quarantined)state.archived_bytes+=segment->bytes;
        ++removed;
    }
    if (removed) {
        memmove(state.segments, state.segments + removed,
                (state.segment_count - removed) * sizeof(state.segments[0]));
        state.segment_count -= removed;
    }
}

static esp_err_t reserve_sequence(uint64_t *sequence)
{
    if (state.next_sequence > STORE_MAX_SEQUENCE - STORE_RESERVATION) {
        fail_store("sequence identifier exhausted");
        return ESP_ERR_INVALID_STATE;
    }
    if (state.next_sequence > state.reserved_end) {
        uint64_t end = state.next_sequence + STORE_RESERVATION - 1;
        esp_err_t error = nvs_set_u64(state.nvs, "reserved", end);
        if (error == ESP_OK) {
            error = nvs_commit(state.nvs);
        }
        if (error != ESP_OK) {
            fail_store("cannot persist sequence reservation");
            return error;
        }
        state.reserved_end = end;
        if (state.fram_attempted) {
            tb_fram_checkpoint(state.reserved_end, state.ack_sequence);
        }
    }
    *sequence = state.next_sequence++;
    return ESP_OK;
}

static bool compression_allowed(void)
{
#ifndef TB_STORE_HOST_TEST
    const esp_partition_t *p=esp_ota_get_running_partition();esp_ota_img_states_t image;
    if(!p)return false;
    esp_err_t err=esp_ota_get_state_partition(p,&image);
    if(err==ESP_ERR_NOT_FOUND)return true; /* First USB image: no rollback entry. */
    return err==ESP_OK && image!=ESP_OTA_IMG_NEW && image!=ESP_OTA_IMG_PENDING_VERIFY;
#else
    return host_allow_compression;
#endif
}

static void append_record(const queued_record_t *item)
{
    if (!state.ready) {
        increment(&state.dropped_unavailable);
        return;
    }
    uint64_t sequence = state.next_sequence;
    char line[STORE_LINE_MAX],encoded[TB_RECORD_MAX];
    size_t packed=compression_allowed() && strstr(item->json,"\"uptime_ms\":") && strstr(item->json,"\"boot_id\":")?tb_store_pack(item->json,strlen(item->json),encoded,sizeof(encoded)):0;
    const char *data=item->json;
    if(packed)data=encoded;
    uint32_t crc=tb_store_record_crc(sequence,packed?0:item->uptime_us,data,strlen(data));
    if(packed)crc^=UINT32_C(0xa21c5e37);
    int written=packed?snprintf(line,sizeof(line),"{\"seq\":%" PRIu64 ",\"c\":%" PRIu32 ",\"z2\":\"%s\"}\n",sequence,crc,data):snprintf(line,sizeof(line),
        "{\"seq\":%" PRIu64 ",\"uptime_us\":%" PRIu64 ",\"crc32\":%" PRIu32 "%s,\"data\":%s}\n",
        sequence,item->uptime_us,crc,packed?",\"v\":2":"",data);
    if (written <= 0 || (size_t)written >= sizeof(line)) {
        increment(&state.invalid_input);
        return;
    }
    size_t length = (size_t)written;
    /* Archives are never recycled. Leave filesystem/WL headroom, and stop
     * accepting new rows explicitly when evidence occupies the outbox budget. */
    uint64_t occupied=state.archived_bytes;
    for(size_t i=0;i<state.segment_count;i++)occupied+=state.segments[i].bytes;
    if(occupied+length+1>state.maximum_segments*state.segment_bytes){
        prune_segments();occupied=state.archived_bytes;
        for(size_t i=0;i<state.segment_count;i++)occupied+=state.segments[i].bytes;
        if(occupied+length+1>state.maximum_segments*state.segment_bytes){state.capacity_blocked=true;snprintf(state.error,sizeof(state.error),"outbox full; pending and isolated files preserved");increment(&state.dropped_full);return;}
    }
#ifndef TB_STORE_HOST_TEST
    if(state.base_path && esp_vfs_fat_info(state.base_path,&state.total_bytes,&state.available_bytes)==ESP_OK && state.available_bytes<length+4096u){
        state.capacity_blocked=true;increment(&state.dropped_full);snprintf(state.error,sizeof(state.error),"filesystem full; pending and isolated files preserved");return;
    }
#endif
    segment_t *segment = state.segment_count
        ? &state.segments[state.segment_count - 1] : NULL;
    if (!segment || segment->bytes + length + 1 > state.segment_bytes) {
        prune_segments();
        if (state.segment_count >= state.maximum_segments || !state.next_segment_id) {
            state.capacity_blocked=true;snprintf(state.error,sizeof(state.error),"segment capacity full; pending files preserved");
            increment(&state.dropped_full);
            return;
        }
        segment = &state.segments[state.segment_count++];
        *segment = (segment_t){.id = state.next_segment_id++};
    }
    /* Reserve only after finding space. A full data queue must not consume
     * sequence ranges or repeatedly wear NVS merely to drop new records. */
    if (reserve_sequence(&sequence) != ESP_OK) {
        increment(&state.dropped_unavailable);
        return;
    }
    if (state.medium_clean) {
        /* A persisted clean flag allows switching backend after reboot. Clear
         * it BEFORE the first write; otherwise power loss could orphan an
         * unacknowledged SD/flash backlog when the preferred medium changes. */
        if (nvs_set_u8(state.nvs, "clean", 0) != ESP_OK ||
            nvs_commit(state.nvs) != ESP_OK) {
            fail_store("cannot lock storage backend before writing");
            increment(&state.dropped_unavailable);
            return;
        }
        state.medium_clean = false;
    }
    char path[64];
    segment_path(path, sizeof(path), segment->id);
    errno=0;
    FILE *file = fopen(path, "a+b");
    const char *failed=file?NULL:"OPEN";
    int io_error=file?0:errno,close_error=0;
    size_t separator=0,stored=0;
    if(file && segment->quarantined && segment->bytes){
        errno=0;
        if(fseek(file,-1,SEEK_END)!=0){failed="SEEK_TAIL";io_error=errno;}
        int last=EOF;
        if(!failed){errno=0;last=fgetc(file);if(last==EOF){failed="READ_TAIL";io_error=errno;}}
        if(!failed){errno=0;if(fseek(file,0,SEEK_END)!=0){failed="SEEK_END";io_error=errno;}}
        if(!failed && last!='\n'){
            errno=0;if(fputc('\n',file)==EOF){failed="SEPARATOR";io_error=errno;}else separator=1;
        }
    }
    if(file && !failed){
        errno=0;stored=fwrite(line,1,length,file);
        if(stored!=length){failed="WRITE";io_error=errno;}
    }
    if(file && !failed){errno=0;if(fflush(file)!=0){failed="FLUSH";io_error=errno;}}
    if(file && !failed){errno=0;if(fsync(fileno(file))!=0){failed="SYNC";io_error=errno;}}
    if(file){
        errno=0;
        if(fclose(file)!=0){close_error=errno;if(!failed){failed="CLOSE";io_error=close_error;}}
    }
    if(failed){
        /* Keep the FIRST error even if close also fails or changes errno.
         * errno==0 is reported as unknown; never invent a measured errno. */
        failed_record_io(failed,io_error,close_error,segment->id,sequence,length,stored);
        return;
    }
    if(state.capacity_blocked){state.capacity_blocked=false;state.error[0]=0;}
    if (!segment->records) {
        segment->minimum = sequence;
    }
    segment->maximum = sequence;
    segment->bytes += length+separator;
    state.isolated_bytes+=separator;
    ++segment->records;
    ++state.pending_records;
    state.pending_bytes += length;
    state.stored_bytes_this_boot+=length;state.last_append_us=(uint64_t)esp_timer_get_time();
    if(state.pending_records==1){cJSON *sample=cJSON_Parse(item->json);oldest_metadata(sample,sequence);cJSON_Delete(sample);}
    ++state.committed_this_boot;
    state.highest_committed = sequence;
}

#ifndef TB_STORE_HOST_TEST
static bool initialize_fram(void)
{
    state.fram_gate_status = "disabled";
    if (!tb_config()->fram_enabled) {
        return true;
    }
    if (!tb_config()->repaired_power_gates) {
        state.fram_gate_status = "power_gates_not_confirmed_repaired";
        return true;
    }
    /* SD and FRAM share SPI. Keep the SD rail powered before driving any SPI
     * pin, even if logging uses internal flash, to avoid phantom powering SD
     * through its I/O protection diodes. */
    gpio_set_level(GPIO_NUM_17, 0);
    gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (initialize_spi() != ESP_OK) {
        state.fram_gate_status = "spi_init_failed";
        return true;
    }
    state.fram_attempted = true;
    state.fram_gate_status = NULL;
    if (!tb_fram_start()) {
        return true;
    }
    uint64_t reserved = 0, mirror_ack = 0;
    if (tb_fram_get_metadata(&reserved, &mirror_ack) &&
        reserved > state.reserved_end) {
        /* NVS remains the ACK authority. Only restore the reservation bound:
         * losing ACK metadata may duplicate an upload, never discard data. */
        if (nvs_set_u64(state.nvs, "reserved", reserved) != ESP_OK ||
            nvs_commit(state.nvs) != ESP_OK) {
            fail_store("cannot persist restored FRAM reservation");
            return false;
        }
        state.reserved_end = reserved;
    }
    return true;
}

static bool initialize_medium(void)
{
    if (!state.base_path && (!initialize_fram() || !mount_storage())) {
        ESP_LOGE(TAG, "%s", state.error);
        return false;
    }
    /* Marker can only be written by the confirmed 0.2.4 admin endpoint.
     * Keep it durable until every log is deleted, including after power loss. */
    uint8_t discard_requested = 0;
    esp_err_t request_err = nvs_get_u8(state.nvs, "discard_logs", &discard_requested);
    if (request_err != ESP_OK && request_err != ESP_ERR_NVS_NOT_FOUND) {
        fail_store("discard: cannot read NVS marker");
        return false;
    }
    if(discard_requested){
        /* Cancel old deferred deletion; an OTA rollback must not resurrect it. */
        if(nvs_set_u8(state.nvs,"discard_logs",0)!=ESP_OK || nvs_commit(state.nvs)!=ESP_OK){fail_store("cannot cancel legacy deferred discard");return false;}
    }
    if (!recover_records()) {
        return false;
    }
    if (state.fram_attempted && state.reserved_end) {
        tb_fram_checkpoint(state.reserved_end, state.ack_sequence);
    }
    prune_segments();
    /* NVS commits do not make multiple keys one atomic transaction. A backend
     * transition must be pessimistically dirty before changing its name. */
    esp_err_t metadata_error = nvs_set_u8(state.nvs, "clean", 0);
    if (metadata_error == ESP_OK) metadata_error = nvs_commit(state.nvs);
    if (metadata_error == ESP_OK && strcmp(state.medium, "sd") == 0) {
        /* Card identity must precede the backend name, so a crash cannot
         * produce a dirty SD backend whose new card identity was never saved. */
        metadata_error = nvs_set_str(state.nvs, "card_id", state.card_id);
        if (metadata_error == ESP_OK) metadata_error = nvs_commit(state.nvs);
    }
    if (metadata_error == ESP_OK) {
        metadata_error = nvs_set_str(state.nvs, "medium", state.medium);
        if (metadata_error == ESP_OK) metadata_error = nvs_commit(state.nvs);
    }
    if (metadata_error != ESP_OK) {
        fail_store("cannot persist selected storage backend");
        return false;
    }
    state.medium_clean = false;
    if (!state.pending_records && !state.isolated_files &&
        nvs_set_u8(state.nvs, "clean", 1) == ESP_OK &&
        nvs_commit(state.nvs) == ESP_OK) {
        state.medium_clean = true;
    }
    state.ready = true;state.medium_initialized=true;
    ESP_LOGI(TAG, "Storage %s: %u pending records, %" PRIu64 " last ack",
             state.medium, (unsigned)state.pending_records, state.ack_sequence);
    return true;
}

static void storage_task(void *argument)
{
    (void)argument;
    xSemaphoreTake(state.mutex, portMAX_DELAY);
    initialize_medium();
    state.initializing = false;
    xSemaphoreGive(state.mutex);
    queued_record_t item;
    bool retained=false;
    uint64_t retained_sequence=0;
    tb_task_budget_t budget;
    tb_task_budget_reset(&budget, esp_timer_get_time());
    for (;;) {
        /* Retry a mounted medium read-only; never format or erase it. Keep
         * the in-flight row, and requeue only if recovery did not find its
         * CRC-valid sequence. This prevents duplicates after ambiguous fsync. */
        if (!state.ready) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            xSemaphoreTake(state.mutex,portMAX_DELAY);
            state.initializing=true;
            if(!state.base_path || !state.medium_initialized){initialize_medium();}
            else if(recover_after_failure()){
                state.ready=true;prune_segments();
                if(retained && state.highest_committed>=retained_sequence)retained=false;
            }
            state.initializing=false;
            xSemaphoreGive(state.mutex);
            continue;
        }
        if(state.rollback_paused && !retained && !uxQueueMessagesWaiting(state.queue)){
            xSemaphoreTake(state.mutex,portMAX_DELAY);state.rollback_drained=true;xSemaphoreGive(state.mutex);
            vTaskDelay(pdMS_TO_TICKS(100));continue;
        }
        if (retained || xQueueReceive(state.queue, &item, pdMS_TO_TICKS(250)) == pdTRUE) {
            xSemaphoreTake(state.mutex, portMAX_DELAY);
            retained_sequence=state.next_sequence;
            append_record(&item);
            retained=!state.ready;
            xSemaphoreGive(state.mutex);
            /* Yield outside the media lock, including a full-medium fast
             * rejection loop. The decoder and writer must both leave time
             * for IDLE, startup, USB and other lower-priority services. */
            if (tb_task_budget_due(&budget, esp_timer_get_time())) {
                increment(&state.scheduling_yields);
                vTaskDelay(TB_TASK_REST_TICKS);
                tb_task_budget_reset(&budget, esp_timer_get_time());
            }
        }
    }
}

esp_err_t tb_store_schedule_discard(void)
{
    /* Rev0.2.12 recovery never authorizes a wipe. ACK-only recycling remains. */
    return ESP_ERR_INVALID_STATE;
}

esp_err_t tb_store_init(void)
{
    if (state.mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    state.medium = "unavailable";
    state.flash_handle = WL_INVALID_HANDLE;
    state.mutex = xSemaphoreCreateMutex();
    state.queue = xQueueCreate(STORE_QUEUE, sizeof(queued_record_t));
    if (!state.mutex || !state.queue) {
        fail_store("storage allocation failed");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = nvs_open("tb_store", NVS_READWRITE, &state.nvs);
    if (error != ESP_OK) {
        fail_store("storage NVS unavailable");
        return error;
    }
    state.nvs_open = true;
    size_t medium_length = sizeof(state.previous_medium);
    error = nvs_get_str(state.nvs, "medium", state.previous_medium, &medium_length);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        fail_store("storage backend metadata cannot be read");
        return error;
    }
    if (state.previous_medium[0] &&
        strcmp(state.previous_medium, "flash") != 0 &&
        strcmp(state.previous_medium, "sd") != 0) {
        fail_store("unknown previous storage backend; data preserved");
        return ESP_ERR_INVALID_STATE;
    }
    size_t card_length = sizeof(state.previous_card_id);
    error = nvs_get_str(state.nvs, "card_id", state.previous_card_id, &card_length);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        fail_store("SD identity metadata cannot be read");
        return error;
    }
    uint8_t clean = state.previous_medium[0] ? 0 : 1;
    error = nvs_get_u8(state.nvs, "clean", &clean);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        fail_store("storage backend clean flag cannot be read");
        return error;
    }
    if (clean > 1) {
        fail_store("storage backend clean flag invalid");
        return ESP_ERR_INVALID_STATE;
    }
    state.medium_clean = clean != 0;
    error = nvs_get_u64(state.nvs, "reserved", &state.reserved_end);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        fail_store("sequence reservation cannot be read");
        return error;
    }
    error = nvs_get_u64(state.nvs, "ack", &state.ack_sequence);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        fail_store("acknowledgement cannot be read");
        return error;
    }
    if (state.reserved_end > STORE_MAX_SEQUENCE ||
        state.ack_sequence > state.reserved_end) {
        fail_store("persistent sequence metadata invalid");
        return ESP_ERR_INVALID_STATE;
    }
    state.initializing = true;
    if (xTaskCreate(storage_task, "tb_store", 9216, NULL, 4, NULL) != pdPASS) {
        state.initializing = false;
        fail_store("storage worker allocation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif

esp_err_t tb_store_prepare_rollback(void)
{
    if(!state.mutex)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&counter_lock);state.rollback_paused=true;portEXIT_CRITICAL(&counter_lock);
    return ESP_OK;
}

esp_err_t tb_store_submit(const char *record_json)
{
    /* Capture starts while media recovery is asynchronous. Buffer that window
     * in the same bounded queue; ESP_OK still means enqueued, not durable. */
    if (!record_json || !state.queue || (!state.ready && !state.initializing && !state.base_path)) {
        increment(&state.dropped_unavailable);
        return ESP_ERR_INVALID_STATE;
    }
    if(state.rollback_paused)return ESP_ERR_INVALID_STATE;
    size_t length = strnlen(record_json, TB_RECORD_MAX);
    if (!length || length >= TB_RECORD_MAX) {
        increment(&state.invalid_input);
        return ESP_ERR_INVALID_SIZE;
    }
    cJSON *parsed = cJSON_ParseWithOpts(record_json, NULL, true);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        increment(&state.invalid_input);
        return ESP_ERR_INVALID_ARG;
    }
    char *canonical = cJSON_PrintUnformatted(parsed);
    cJSON_Delete(parsed);
    if (!canonical) {
        return ESP_ERR_NO_MEM;
    }
    length = strlen(canonical);
    if (length >= TB_RECORD_MAX) {
        cJSON_free(canonical);
        increment(&state.invalid_input);
        return ESP_ERR_INVALID_SIZE;
    }
    queued_record_t item = {.uptime_us = (uint64_t)esp_timer_get_time()};
    memcpy(item.json, canonical, length + 1);
    cJSON_free(canonical);
    portENTER_CRITICAL(&counter_lock);
    bool paused=state.rollback_paused;
    bool accepted=!paused && xQueueSend(state.queue,&item,0)==pdTRUE;
    portEXIT_CRITICAL(&counter_lock);
    if(paused)return ESP_ERR_INVALID_STATE;
    if (!accepted) {
        increment(&state.dropped_queue);
        return ESP_ERR_NO_MEM;
    }
    if (state.initializing) increment(&state.buffered_during_recovery);
    return ESP_OK;
}

cJSON *tb_store_batch(size_t limit, size_t max_bytes)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *samples = cJSON_CreateArray();
    if (!result || !samples) {
        cJSON_Delete(result);
        cJSON_Delete(samples);
        return NULL;
    }
    if(!cJSON_AddItemToObject(result,"samples",samples)){cJSON_Delete(samples);cJSON_Delete(result);return NULL;}
    cJSON_AddStringToObject(result, "device_uid",
                           tb_config()->cloud_device_uid[0]
                               ? tb_config()->cloud_device_uid : tb_device_uid());
    cJSON_AddNumberToObject(result, "boot_id", tb_boot_id());
    cJSON_AddNumberToObject(result, "device_uptime_ms",
                           (double)(esp_timer_get_time() / 1000));
    struct timeval now;
    if (gettimeofday(&now, NULL) == 0 && now.tv_sec >= 1704067200) {
        cJSON_AddNumberToObject(result, "device_time_ms",
                               (double)now.tv_sec * 1000.0 + now.tv_usec / 1000);
    }
    if (state.initializing || !state.mutex ||
        xSemaphoreTake(state.mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        cJSON_AddBoolToObject(result, "ok", false);
        cJSON_AddStringToObject(result, "error",
                               state.initializing ? "storage_initializing" : "storage_busy");
        cJSON_AddNumberToObject(result, "count", 0);
        cJSON_AddNumberToObject(result, "max_seq", 0);
        cJSON_AddNullToObject(result, "pending_bytes");
        cJSON_AddNullToObject(result, "last_ack_seq");
        return result;
    }
    if (!state.ready) {
        cJSON_AddBoolToObject(result, "ok", false);
        cJSON_AddStringToObject(result, "error", state.error);
        cJSON_AddNumberToObject(result, "count", 0);
        cJSON_AddNumberToObject(result, "max_seq", 0);
        cJSON_AddNullToObject(result, "pending_bytes");
        cJSON_AddNullToObject(result, "last_ack_seq");
        xSemaphoreGive(state.mutex);
        return result;
    }
    if (limit < 1) limit = 1;
    if (limit > 32) limit = 32;
    if (max_bytes < 2048) max_bytes = 2048;
    if (max_bytes > 24576) max_bytes = 24576;
    size_t wire_bytes = 512;
    size_t count = 0;
    uint64_t maximum = 0, previous=state.ack_sequence;
    bool failed = false, full=false;
    for (size_t index = 0; index < state.segment_count && count < limit && !failed && !full; ++index) {
        if (state.segments[index].maximum <= state.ack_sequence) {
            continue;
        }
        char path[64];
        segment_path(path, sizeof(path), state.segments[index].id);
        FILE *file = fopen(path, "rb");
        if (!file) {
            failed = true;
            break;
        }
        if (fseek(file, (long)state.segments[index].pending_offset, SEEK_SET) != 0) {
            fclose(file);
            failed = true;
            break;
        }
        uint64_t sequence=0;record_span_t span={0};cJSON *record;
        errno=0;
        while(count<limit && (record=next_record(file,&sequence,&span))){
            if(span.skipped && !state.segments[index].quarantined){cJSON_Delete(record);state.ready=false;failed=true;break;}
            if (sequence <= previous) {
                cJSON_Delete(record);
                continue;
            }
            if (wire_bytes + TB_RECORD_MAX + 32 > max_bytes) {
                cJSON_Delete(record);
                full=true;
                break;
            }
            previous=sequence;
            /* The Cloud ingest contract expects flat samples. The on-disk
             * CRC wrapper is an implementation detail, not the upload shape.
             * Preserve the sample's original boot_id/uptime_ms across reboot. */
            cJSON *sample = cJSON_Duplicate(
                cJSON_GetObjectItemCaseSensitive(record, "data"), true);
            cJSON_Delete(record);
            if (!sample) {
                failed = true;
                break;
            }
            cJSON_DeleteItemFromObjectCaseSensitive(sample, "seq");
            if (!cJSON_AddNumberToObject(sample, "seq", (double)sequence) ||
                !cJSON_AddItemToArray(samples, sample)) {
                cJSON_Delete(sample);
                failed = true;
                break;
            }
            wire_bytes += TB_RECORD_MAX + 32;
            maximum = sequence;
            ++count;
        }
        if(ferror(file) || errno==ENOMEM)failed=true;
        if(span.skipped && !state.segments[index].quarantined){state.ready=false;failed=true;}
        fclose(file);
    }
    if(!count && state.pending_records && !failed){state.ready=false;failed=true;}
    if (failed) {
        if(!state.ready)state.recovery_complete=false;
        snprintf(state.error,sizeof(state.error),"batch read/allocation unavailable; data preserved");
        cJSON_DeleteItemFromObjectCaseSensitive(result, "samples");
        cJSON_AddArrayToObject(result, "samples");
        count = 0;
        maximum = 0;
    }
    if (!failed && maximum > state.highest_offered) {
        state.highest_offered = maximum;
    }
    cJSON_AddBoolToObject(result, "ok", state.ready && !failed);
    cJSON_AddNumberToObject(result, "count", (double)count);
    cJSON_AddNumberToObject(result, "max_seq", (double)maximum);
    cJSON_AddNumberToObject(result, "pending_bytes", (double)state.pending_bytes);
    cJSON_AddNumberToObject(result, "last_ack_seq", (double)state.ack_sequence);
    if (!state.ready) {
        cJSON_AddStringToObject(result, "error", state.error);
    }
    xSemaphoreGive(state.mutex);
    return result;
}

static bool count_acknowledged(uint64_t previous, uint64_t target,
                                size_t *records, size_t *bytes,
                                size_t pending_offsets[STORE_SEGMENTS],
                                bool *target_found)
{
    *records = 0;
    *bytes = 0;
    *target_found = false;
    uint64_t scan_previous=previous;
    for (size_t index = 0; index < state.segment_count; ++index) {
        pending_offsets[index] = state.segments[index].pending_offset;
        if (state.segments[index].maximum <= previous ||
            state.segments[index].minimum > target) {
            continue;
        }
        char path[64];
        segment_path(path, sizeof(path), state.segments[index].id);
        FILE *file = fopen(path, "rb");
        if (!file) {
            return false;
        }
        if (fseek(file, (long)state.segments[index].pending_offset, SEEK_SET) != 0) {
            fclose(file);
            return false;
        }
        uint64_t sequence=0;record_span_t span={0};cJSON *record;errno=0;
        while((record=next_record(file,&sequence,&span))){
            size_t length=span.bytes;
            cJSON_Delete(record);
            if(span.skipped && !state.segments[index].quarantined){
                state.ready=false;state.recovery_complete=false;fclose(file);return false;
            }
            if(sequence<=scan_previous)continue;
            scan_previous=sequence;
            if (sequence == target) *target_found = true;
            if (sequence > previous && sequence <= target) {
                ++*records;
                *bytes += length;
                long position = ftell(file);
                if (position < 0) {
                    fclose(file);
                    return false;
                }
                pending_offsets[index] = (size_t)position;
            } else if (sequence > target) {
                break;
            }
        }
        bool error = ferror(file) != 0 || errno==ENOMEM;
        fclose(file);
        if (error) {
            return false;
        }
    }
    return true;
}

static void refresh_oldest(void)
{
    state.oldest_pending_known=false;state.oldest_pending_sequence=0;state.oldest_pending_ts[0]=0;
    if(!state.pending_records)return;
    for(size_t i=0;i<state.segment_count;i++){
        if(state.segments[i].maximum<=state.ack_sequence)continue;
        char path[96];segment_path(path,sizeof(path),state.segments[i].id);
        FILE *f=fopen(path,"rb");if(!f)return;
        if(fseek(f,(long)state.segments[i].pending_offset,SEEK_SET)!=0){fclose(f);return;}
        uint64_t seq=0;record_span_t span={0};cJSON *row;
        while((row=next_record(f,&seq,&span))){
            if(seq>state.ack_sequence){oldest_metadata(cJSON_GetObjectItemCaseSensitive(row,"data"),seq);cJSON_Delete(row);fclose(f);return;}
            cJSON_Delete(row);
        }
        fclose(f);
    }
}

esp_err_t tb_store_ack(uint64_t sequence)
{
    if (state.initializing) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!state.mutex || xSemaphoreTake(state.mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t error = ESP_OK;
    if (!state.ready) {
        error = ESP_ERR_INVALID_STATE;
    } else if (sequence <= state.ack_sequence) {
        /* Retried acknowledgements are idempotent. */
    } else if (sequence > state.highest_offered ||
               sequence > state.highest_committed) {
        error = ESP_ERR_INVALID_ARG;
    } else {
        size_t records = 0, bytes = 0;
        size_t offsets[STORE_SEGMENTS];
        bool target_found = false;
        if (!count_acknowledged(state.ack_sequence, sequence, &records, &bytes, offsets, &target_found) ||
            records > state.pending_records || bytes > state.pending_bytes) {
            fail_store("cannot verify acknowledgement range");
            error = ESP_FAIL;
        } else if (!target_found) {
            /* Sequence reservations leave gaps after reboot. A watermark below
             * highest_offered is insufficient: the target must be a record. */
            error = ESP_ERR_INVALID_ARG;
        } else {
            error = nvs_set_u64(state.nvs, "ack", sequence);
            if (error == ESP_OK) {
                error = nvs_commit(state.nvs);
            }
            if (error == ESP_OK) {
                state.ack_sequence = sequence;state.last_ack_us=(uint64_t)esp_timer_get_time();
                state.pending_records -= records;
                state.pending_bytes -= bytes;
                for (size_t i = 0; i < state.segment_count; ++i) {
                    state.segments[i].pending_offset = offsets[i];
                }
                if (state.fram_attempted) {
                    tb_fram_checkpoint(state.reserved_end, state.ack_sequence);
                }
                prune_segments();refresh_oldest();
                /* ACK must already be durable before clean becomes durable.
                 * A crash between these commits leaves a conservative dirty
                 * flag, never a clean flag with a stale acknowledgement. */
                state.medium_clean = false;
                if (!state.pending_records && !state.isolated_files &&
                    nvs_set_u8(state.nvs, "clean", 1) == ESP_OK &&
                    nvs_commit(state.nvs) == ESP_OK) {
                    state.medium_clean = true;
                }
            }
        }
    }
    xSemaphoreGive(state.mutex);
    return error;
}

void tb_store_add_json(cJSON *object)
{
    cJSON *storage = cJSON_AddObjectToObject(object, "storage");
    if (!storage) {
        return;
    }
    if (state.initializing) {
        cJSON_AddBoolToObject(storage, "ready", false);
        cJSON_AddBoolToObject(storage, "initializing", true);
        cJSON_AddStringToObject(storage,"phase","RECOVERING");
        cJSON_AddBoolToObject(storage,"pending_known",false);
        cJSON_AddNullToObject(storage,"pending_records");
        cJSON_AddNullToObject(storage,"pending_bytes");
        cJSON_AddNumberToObject(storage, "recovered_segments", state.recovery_segments);
        cJSON_AddNumberToObject(storage, "queue_capacity", STORE_QUEUE);
        cJSON_AddNumberToObject(storage, "queue_depth",
                               state.queue ? uxQueueMessagesWaiting(state.queue) : 0);
        cJSON_AddBoolToObject(storage, "queued_is_durable", false);
        cJSON_AddNumberToObject(storage, "buffered_during_recovery",
                               (double)counter_value(&state.buffered_during_recovery));
        cJSON_AddNumberToObject(storage, "dropped_queue",
                               (double)counter_value(&state.dropped_queue));
        return;
    }
    if (!state.mutex || xSemaphoreTake(state.mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        cJSON_AddBoolToObject(storage,"ready",state.mutex && state.ready);
        cJSON_AddBoolToObject(storage,"busy",state.mutex!=NULL);
        cJSON_AddStringToObject(storage,"phase",state.mutex && state.ready?"BUSY":"RECOVERY_REQUIRED");
        cJSON_AddBoolToObject(storage,"pending_known",false);
        cJSON_AddNullToObject(storage,"pending_records");
        cJSON_AddStringToObject(storage,"error",state.mutex?"storage_busy":"storage_uninitialized");
        return;
    }
    cJSON_AddBoolToObject(storage, "ready", state.ready);
    cJSON_AddBoolToObject(storage, "initializing", false);
    cJSON_AddStringToObject(storage, "medium", state.medium ? state.medium : "unavailable");
    cJSON_AddStringToObject(storage, "directory", state.base_path ? state.base_path : "");
    cJSON_AddStringToObject(storage, "error", state.error);
    cJSON_AddStringToObject(storage, "sd_error", state.sd_error);
    cJSON_AddBoolToObject(storage, "backend_clean", state.medium_clean);
    cJSON_AddStringToObject(storage, "durability", "fflush_fsync_close_before_offer");
    cJSON_AddBoolToObject(storage, "queued_is_durable", false);
    cJSON_AddNumberToObject(storage, "scheduling_yields", (double)counter_value(&state.scheduling_yields));
    cJSON_AddNumberToObject(storage, "queue_capacity", STORE_QUEUE);
    cJSON_AddNumberToObject(storage, "record_capacity", TB_RECORD_MAX - 1);
    cJSON_AddNumberToObject(storage, "buffered_during_recovery",
                           (double)counter_value(&state.buffered_during_recovery));
    if(state.ready && state.recovery_complete){
        cJSON_AddNumberToObject(storage,"pending_records",(double)state.pending_records);
        cJSON_AddNumberToObject(storage,"pending_bytes",(double)state.pending_bytes);
    }else{cJSON_AddNullToObject(storage,"pending_records");cJSON_AddNullToObject(storage,"pending_bytes");}
    cJSON_AddBoolToObject(storage,"pending_known",state.ready && state.recovery_complete);
    cJSON_AddBoolToObject(storage,"rollback_paused",state.rollback_paused);
    cJSON_AddBoolToObject(storage,"rollback_ready",state.rollback_paused && state.rollback_drained && state.ready && state.recovery_complete && !state.pending_records && !uxQueueMessagesWaiting(state.queue));
    cJSON_AddStringToObject(storage,"phase",state.rollback_paused?"MAINTENANCE_PAUSED":state.ready && state.capacity_blocked?"FULL":state.ready?"NORMAL":state.initializing?"RECOVERING":"RECOVERY_REQUIRED");
    uint64_t used=state.archived_bytes;
    for(size_t i=0;i<state.segment_count;i++)used+=state.segments[i].bytes;
    uint64_t budget=state.maximum_segments*state.segment_bytes;
    uint64_t available=used<budget?budget-used:0;
    uint64_t appendable=state.segment_count<state.maximum_segments?(state.maximum_segments-state.segment_count)*state.segment_bytes:0;
    if(state.segment_count && state.segments[state.segment_count-1].bytes<state.segment_bytes)
        appendable+=state.segment_bytes-state.segments[state.segment_count-1].bytes;
    if(available>appendable)available=appendable;
#ifndef TB_STORE_HOST_TEST
    if(state.base_path && esp_vfs_fat_info(state.base_path,&state.total_bytes,&state.available_bytes)==ESP_OK && available>state.available_bytes)
        available=state.available_bytes;
#endif
    cJSON_AddNumberToObject(storage,"space_total_bytes",(double)(state.total_bytes?state.total_bytes:budget));
    cJSON_AddNumberToObject(storage,"outbox_budget_bytes",(double)budget);
    cJSON_AddNumberToObject(storage,"space_available_bytes",(double)available);
    uint64_t now=(uint64_t)esp_timer_get_time();
    if(now>=UINT64_C(60000000) && state.stored_bytes_this_boot){
        cJSON_AddNumberToObject(storage,"offline_estimate_seconds",(double)available*((double)now/1000000.0)/(double)state.stored_bytes_this_boot);
        cJSON_AddStringToObject(storage,"offline_estimate_quality","WRITE_RATE_THIS_BOOT");
    }else{cJSON_AddNullToObject(storage,"offline_estimate_seconds");cJSON_AddStringToObject(storage,"offline_estimate_quality","LEARNING");}
    cJSON_AddNumberToObject(storage,"isolated_files",(double)state.isolated_files);
    cJSON_AddNumberToObject(storage,"isolated_bytes",(double)state.isolated_bytes);
    cJSON_AddNumberToObject(storage,"recovered_records",(double)state.recovered_records);
    cJSON_AddNullToObject(storage,"unrecoverable_records");
    cJSON_AddStringToObject(storage,"isolated_evidence_status","PRESERVED_NOT_ACKED_CORRUPT_RECORD_COUNT_UNKNOWN");
    cJSON_AddNumberToObject(storage,"dropped_before_durability",(double)(counter_value(&state.dropped_queue)+counter_value(&state.dropped_full)+counter_value(&state.dropped_unavailable)));
    cJSON_AddBoolToObject(storage,"oldest_pending_known",state.oldest_pending_known);
    if(state.pending_records && state.oldest_pending_known){
        cJSON_AddNumberToObject(storage,"oldest_pending_seq",(double)state.oldest_pending_sequence);
        if(state.oldest_pending_ts[0])cJSON_AddStringToObject(storage,"oldest_pending_ts",state.oldest_pending_ts);
        cJSON_AddNumberToObject(storage,"oldest_pending_boot",state.oldest_pending_boot);
        cJSON_AddNumberToObject(storage,"oldest_pending_uptime_ms",(double)state.oldest_pending_uptime_us/1000.0);
    }else cJSON_AddNullToObject(storage,"oldest_pending_uptime_ms");
    if(state.last_ack_us)cJSON_AddNumberToObject(storage,"last_ack_age_ms",(double)(now-state.last_ack_us)/1000.0);
    else cJSON_AddNullToObject(storage,"last_ack_age_ms");
    cJSON_AddNumberToObject(storage, "last_ack_seq", (double)state.ack_sequence);
    cJSON_AddNumberToObject(storage, "max_seq", (double)state.highest_committed);
    cJSON_AddNumberToObject(storage, "committed_this_boot", (double)state.committed_this_boot);
    cJSON_AddNumberToObject(storage, "segments", (double)state.segment_count);
    cJSON_AddNumberToObject(storage, "max_segments", (double)state.maximum_segments);
    cJSON_AddNumberToObject(storage, "segment_bytes", (double)state.segment_bytes);
    cJSON_AddNumberToObject(storage, "recovered_tail_bytes", (double)state.recovered_tail_bytes);
    cJSON_AddBoolToObject(storage, "discard_completed_this_boot", state.purged_this_boot);
    cJSON_AddNumberToObject(storage, "discarded_files", state.purged_files);
    cJSON_AddNumberToObject(storage, "corrupt_records", (double)state.corrupt_records);
    cJSON_AddNumberToObject(storage, "write_errors", (double)state.write_errors);
    cJSON_AddNumberToObject(storage,"recovery_attempts",(double)state.recovery_attempts);
    cJSON_AddNumberToObject(storage,"recovery_successes",(double)state.recovery_successes);
    cJSON_AddNumberToObject(storage,"volume_rebinds",(double)state.volume_rebinds);
    cJSON_AddBoolToObject(storage,"volume_rebind_pending",state.volume_rebind_required);
    if(state.volume_rebinds || state.volume_result)cJSON_AddNumberToObject(storage,"volume_rebind_fresult",state.volume_result);
    else cJSON_AddNullToObject(storage,"volume_rebind_fresult");
    if(state.last_recovery_us)cJSON_AddNumberToObject(storage,"last_recovery_age_ms",(double)(now-state.last_recovery_us)/1000.0);
    else cJSON_AddNullToObject(storage,"last_recovery_age_ms");
    if(state.last_io_failure.stage){
        const store_io_failure_t *io=&state.last_io_failure;
        cJSON *failure=cJSON_AddObjectToObject(storage,"last_io_failure");
        cJSON_AddStringToObject(failure,"stage",io->stage);
        cJSON_AddNumberToObject(failure,"errno",io->error);
        cJSON_AddBoolToObject(failure,"errno_known",io->error!=0);
        cJSON_AddNumberToObject(failure,"close_errno",io->close_error);
        cJSON_AddNumberToObject(failure,"segment",io->segment);
        cJSON_AddNumberToObject(failure,"sequence",(double)io->sequence);
        cJSON_AddNumberToObject(failure,"requested_bytes",(double)io->requested);
        cJSON_AddNumberToObject(failure,"written_bytes",(double)io->written);
        cJSON_AddNumberToObject(failure,"boot_id",tb_boot_id());
        cJSON_AddNumberToObject(failure,"uptime_ms",(double)io->at_us/1000.0);
        cJSON_AddBoolToObject(failure,"historical",state.ready);
    }
    cJSON_AddNumberToObject(storage, "queue_depth",
                           state.queue ? uxQueueMessagesWaiting(state.queue) : 0);
    cJSON_AddNumberToObject(storage, "dropped_queue", (double)counter_value(&state.dropped_queue));
    cJSON_AddNumberToObject(storage, "dropped_unavailable",
                           (double)counter_value(&state.dropped_unavailable));
    cJSON_AddNumberToObject(storage, "dropped_full", (double)counter_value(&state.dropped_full));
    cJSON_AddNumberToObject(storage, "invalid_input", (double)counter_value(&state.invalid_input));
    cJSON_AddStringToObject(storage, "fram_status",
                           state.fram_gate_status
                               ? state.fram_gate_status : tb_fram_status());
    xSemaphoreGive(state.mutex);
}
