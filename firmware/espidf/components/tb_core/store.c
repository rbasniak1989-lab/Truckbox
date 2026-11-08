#ifndef TB_STORE_HOST_TEST
#include "tb.h"
#endif
#include "store_internal.h"
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
} segment_t;

typedef struct {
    uint64_t uptime_us;
    char json[TB_RECORD_MAX];
} queued_record_t;

typedef struct {
    SemaphoreHandle_t mutex;
    QueueHandle_t queue;
    nvs_handle_t nvs;
    bool nvs_open;
    bool medium_clean;
    volatile bool ready;
    volatile bool initializing;
    bool spi_ready;
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
    uint64_t write_errors;
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
    const char *data = NULL;
    size_t payload_length = 0;
    if (length >= STORE_LINE_MAX ||
        !tb_store_payload_span(line, length, &data, &payload_length)) {
        return NULL;
    }
    if (!payload_length || payload_length >= TB_RECORD_MAX) {
        return NULL;
    }
    cJSON *object = cJSON_ParseWithOpts(line, NULL, true);
    uint64_t crc = 0;
    uint64_t uptime = 0;
    bool valid = cJSON_IsObject(object) &&
                 get_u64_json(object, "seq", sequence) && *sequence > 0 &&
                 get_u64_json(object, "uptime_us", &uptime) &&
                 get_u64_json(object, "crc32", &crc) && crc <= UINT32_MAX &&
                 cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(object, "data")) &&
                 tb_store_record_crc(*sequence, uptime, data, payload_length) ==
                     (uint32_t)crc;
    if (!valid) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
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
    /* A full-size 8 MB layout may allocate a larger FAT storage partition.
     * Preserve the 24-segment limit on the shipped 1.875 MiB partition,
     * and expand only on a USB-migrated partition, keeping 512 KiB spare
     * for FAT, wear levelling metadata and temporary file operations. */
    size_t flash_max = 24;
    if (partition->size > 0x1e0000u &&
        partition->size > (512u * 1024u)) {
        flash_max = (partition->size - (512u * 1024u)) / STORE_FLASH_SEGMENT_BYTES;
        if (flash_max > STORE_SEGMENTS) flash_max = STORE_SEGMENTS;
        if (flash_max < 24) flash_max = 24;
    }
    state.maximum_segments = flash_max;
    state.segment_bytes = STORE_FLASH_SEGMENT_BYTES;
    return true;
}

#endif

/* Delete ONLY our named telemetry .LOG segments, never other files or FAT/NVS.
 * Performed at boot, before recovery and before storage starts accepting data.
 * The NVS marker survives a power failure; a subsequent boot resumes deletion.
 * Sequence reservations remain unchanged so old cloud IDs are never reused. */
static bool discard_log_files(void)
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
    DIR *directory = opendir(state.base_path);
    if (!directory) {
        fail_store("cannot enumerate storage");
        return false;
    }
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        uint32_t id;
        if (!file_id(entry->d_name, &id)) {
            continue;
        }
        if (state.segment_count >= state.maximum_segments) {
            closedir(directory);
            fail_store("too many record segments; data preserved");
            return false;
        }
        state.segments[state.segment_count++].id = id;
    }
    closedir(directory);
    qsort(state.segments, state.segment_count, sizeof(state.segments[0]),
          compare_segments);

    uint64_t previous = 0;
    for (size_t index = 0; index < state.segment_count; ++index) {
        segment_t *segment = &state.segments[index];
        char path[64];
        segment_path(path, sizeof(path), segment->id);
        FILE *file = fopen(path, "r+b");
        if (!file) {
            fail_store("cannot recover record segment");
            return false;
        }
        char line[STORE_LINE_MAX];
        long good_end = 0;
        while (fgets(line, sizeof(line), file)) {
            size_t length = strlen(line);
            if (!length || line[length - 1] != '\n') {
                /* Only an interrupted final record of the last segment may be
                 * removed. A long/full line or older corruption is preserved. */
                if (index + 1 == state.segment_count && feof(file) &&
                    length < sizeof(line) - 1) {
                    long end = ftell(file);
                    if (end < good_end || ftruncate(fileno(file), good_end) != 0 ||
                        fsync(fileno(file)) != 0) {
                        fclose(file);
                        fail_store("cannot trim interrupted final record");
                        return false;
                    }
                    state.recovered_tail_bytes += (uint64_t)(end - good_end);
                    break;
                }
                fclose(file);
                ++state.corrupt_records;
                fail_store("non-final or oversized incomplete record; data preserved");
                return false;
            }
            uint64_t sequence = 0;
            cJSON *record = read_record(line, length, &sequence);
            if (!record || sequence <= previous) {
                cJSON_Delete(record);
                fclose(file);
                ++state.corrupt_records;
                fail_store("record CRC or sequence invalid; data preserved");
                return false;
            }
            cJSON_Delete(record);
            previous = sequence;
            if (!segment->records) {
                segment->minimum = sequence;
            }
            if (sequence > state.ack_sequence &&
                segment->maximum <= state.ack_sequence) {
                segment->pending_offset = segment->bytes;
            }
            segment->maximum = sequence;
            segment->bytes += length;
            ++segment->records;
            if (sequence > state.ack_sequence) {
                state.pending_bytes += length;
                ++state.pending_records;
            }
            good_end = ftell(file);
            if ((segment->records & 63U) == 0) {
                vTaskDelay(1);
            }
        }
        bool read_error = ferror(file) != 0;
        fclose(file);
        if (read_error) {
            fail_store("error reading record segment");
            return false;
        }
        if (segment->maximum <= state.ack_sequence) {
            segment->pending_offset = segment->bytes;
        }
        state.recovery_segments = (uint32_t)(index + 1);
        vTaskDelay(1);
    }
    state.highest_committed = previous > state.ack_sequence ? previous : state.ack_sequence;
    state.next_segment_id = state.segment_count
        ? state.segments[state.segment_count - 1].id + 1 : 1;
    if (!state.next_segment_id) {
        fail_store("segment identifier exhausted");
        return false;
    }
    if (state.highest_committed > state.reserved_end) {
        state.reserved_end = state.highest_committed;
        if (nvs_set_u64(state.nvs, "reserved", state.reserved_end) != ESP_OK ||
            nvs_commit(state.nvs) != ESP_OK) {
            fail_store("cannot reserve recovered sequence range");
            return false;
        }
    }
    state.next_sequence = state.reserved_end + 1;
    if (tb_config()->sequence_floor > state.next_sequence) {
        state.next_sequence = tb_config()->sequence_floor;
    }
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
        if (unlink(path) != 0 && errno != ENOENT) {
            snprintf(state.error, sizeof(state.error), "cannot prune acknowledged segment");
            break;
        }
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

static void append_record(const queued_record_t *item)
{
    if (!state.ready) {
        increment(&state.dropped_unavailable);
        return;
    }
    uint64_t sequence = state.next_sequence;
    char line[STORE_LINE_MAX];
    int written = snprintf(line, sizeof(line),
        "{\"seq\":%" PRIu64 ",\"uptime_us\":%" PRIu64 ",\"crc32\":%" PRIu32
        ",\"data\":%s}\n", sequence, item->uptime_us,
        tb_store_record_crc(sequence, item->uptime_us, item->json, strlen(item->json)),
        item->json);
    if (written <= 0 || (size_t)written >= sizeof(line)) {
        increment(&state.invalid_input);
        return;
    }
    size_t length = (size_t)written;
    segment_t *segment = state.segment_count
        ? &state.segments[state.segment_count - 1] : NULL;
    if (!segment || segment->bytes + length > state.segment_bytes) {
        prune_segments();
        if (state.segment_count >= state.maximum_segments || !state.next_segment_id) {
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
    FILE *file = fopen(path, "ab");
    bool ok = file && fwrite(line, 1, length, file) == length &&
              fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (file && fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        ++state.write_errors;
        fail_store("record write failed; stopped to preserve data");
        return;
    }
    if (!segment->records) {
        segment->minimum = sequence;
    }
    segment->maximum = sequence;
    segment->bytes += length;
    ++segment->records;
    ++state.pending_records;
    state.pending_bytes += length;
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
    if (!initialize_fram() || !mount_storage()) {
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
    if (discard_requested) {
        if (discard_requested != 1 || strcmp(state.medium, "flash") != 0) {
            fail_store("discard: invalid marker or non-flash backend");
            return false;
        }
        if (!discard_log_files()) return false;
        if (nvs_set_u8(state.nvs, "discard_logs", 0) != ESP_OK ||
            nvs_commit(state.nvs) != ESP_OK) {
            fail_store("discard: cannot commit completion marker");
            return false;
        }
        state.purged_this_boot = true;
        ESP_LOGW(TAG, "Operator-authorized discard completed: %lu log files, NVS/OTA intact",
                 (unsigned long)state.purged_files);
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
    if (!state.pending_records &&
        nvs_set_u8(state.nvs, "clean", 1) == ESP_OK &&
        nvs_commit(state.nvs) == ESP_OK) {
        state.medium_clean = true;
    }
    state.ready = true;
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
    tb_task_budget_t budget;
    tb_task_budget_reset(&budget, esp_timer_get_time());
    for (;;) {
        /* Preserve the bounded volatile backlog after an I/O failure. Reading
         * and discarding it would erase the only remaining copy. Reboot is
         * required to recover the failed medium; no unsafe automatic remount. */
        if (!state.ready) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (xQueueReceive(state.queue, &item, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(state.mutex, portMAX_DELAY);
            append_record(&item);
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
    /* Called only by an authenticated admin endpoint after OTA confirmation.
     * The active writer is never interrupted: deletion starts after reboot. */
    if (!state.mutex || xSemaphoreTake(state.mutex, pdMS_TO_TICKS(2000)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (state.ready && !state.initializing && state.nvs_open &&
        state.medium && strcmp(state.medium, "flash") == 0) {
        result = nvs_set_u8(state.nvs, "discard_logs", 1);
        if (result == ESP_OK) result = nvs_commit(state.nvs);
    }
    xSemaphoreGive(state.mutex);
    return result;
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
    if (xTaskCreate(storage_task, "tb_store", 7168, NULL, 4, NULL) != pdPASS) {
        state.initializing = false;
        fail_store("storage worker allocation failed");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif

esp_err_t tb_store_submit(const char *record_json)
{
    /* Capture starts while media recovery is asynchronous. Buffer that window
     * in the same bounded queue; ESP_OK still means enqueued, not durable. */
    if (!record_json || !state.queue || (!state.ready && !state.initializing)) {
        increment(&state.dropped_unavailable);
        return ESP_ERR_INVALID_STATE;
    }
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
    if (xQueueSend(state.queue, &item, 0) != pdTRUE) {
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
    cJSON_AddItemToObject(result, "samples", samples);
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
    uint64_t maximum = 0;
    bool failed = false;
    for (size_t index = 0; index < state.segment_count && count < limit && !failed; ++index) {
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
        char line[STORE_LINE_MAX];
        while (count < limit && fgets(line, sizeof(line), file)) {
            size_t length = strlen(line);
            uint64_t sequence = 0;
            cJSON *record = read_record(line, length, &sequence);
            if (!record) {
                failed = true;
                break;
            }
            if (sequence <= state.ack_sequence) {
                cJSON_Delete(record);
                continue;
            }
            if (wire_bytes + length + 32 > max_bytes) {
                cJSON_Delete(record);
                index = state.segment_count;
                break;
            }
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
            wire_bytes += length + 32;
            maximum = sequence;
            ++count;
        }
        if (ferror(file)) {
            failed = true;
        }
        fclose(file);
    }
    if (failed) {
        ++state.corrupt_records;
        fail_store("cannot verify stored batch; data preserved");
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
        char line[STORE_LINE_MAX];
        while (fgets(line, sizeof(line), file)) {
            size_t length = strlen(line);
            uint64_t sequence;
            cJSON *record = read_record(line, length, &sequence);
            if (!record) {
                fclose(file);
                return false;
            }
            cJSON_Delete(record);
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
        bool error = ferror(file) != 0;
        fclose(file);
        if (error) {
            return false;
        }
    }
    return true;
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
                state.ack_sequence = sequence;
                state.pending_records -= records;
                state.pending_bytes -= bytes;
                for (size_t i = 0; i < state.segment_count; ++i) {
                    state.segments[i].pending_offset = offsets[i];
                }
                if (state.fram_attempted) {
                    tb_fram_checkpoint(state.reserved_end, state.ack_sequence);
                }
                prune_segments();
                /* ACK must already be durable before clean becomes durable.
                 * A crash between these commits leaves a conservative dirty
                 * flag, never a clean flag with a stale acknowledgement. */
                state.medium_clean = false;
                if (!state.pending_records &&
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
        cJSON_AddBoolToObject(storage, "ready", false);
        cJSON_AddStringToObject(storage, "error", "storage_busy_or_uninitialized");
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
    cJSON_AddNumberToObject(storage, "pending_records", (double)state.pending_records);
    cJSON_AddNumberToObject(storage, "pending_bytes", (double)state.pending_bytes);
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
