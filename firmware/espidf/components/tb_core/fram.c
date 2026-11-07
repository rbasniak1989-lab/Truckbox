#include "fram.h"

#include <stddef.h>
#include <string.h>

#define FRAM_BYTES 8192U
#define FRAM_SLOT_BYTES 64U
#define FRAM_MAX_SEQUENCE UINT64_C(9007199254740991)

/*
 * Version 1 journal, two independently validated 64-byte slots at 0 and 64.
 * All integers are explicitly little-endian, independent of compiler padding.
 *
 *  0..3  magic "TBFR"          4 version=1        5 header length=32
 *  6..7  reserved=0           8..11 generation 12..15 reserved=0
 * 16..23 reserved sequence end                24..31 acknowledged sequence
 * 32..59 reserved=FF                         60..63 IEEE CRC32 of bytes 0..59
 *
 * NVS is authoritative. Call checkpoint only after the corresponding NVS
 * transaction has committed; this mirror must never acknowledge data first.
 */
typedef struct {
    uint32_t generation;
    uint64_t reserved_end;
    uint64_t ack;
} fram_metadata_t;

static uint32_t fram_crc32(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return ~crc;
}

static uint32_t read_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t read_le64(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)bytes[i] << (8 * i);
    return value;
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
}

static void write_le64(uint8_t *bytes, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
}

static bool metadata_values_valid(uint64_t reserved_end, uint64_t ack)
{
    return reserved_end > 0 && reserved_end <= FRAM_MAX_SEQUENCE && ack <= reserved_end;
}

static void metadata_encode(uint8_t slot[FRAM_SLOT_BYTES], const fram_metadata_t *metadata)
{
    memset(slot, 0xff, FRAM_SLOT_BYTES);
    memcpy(slot, "TBFR", 4);
    slot[4] = 1;
    slot[5] = 32;
    slot[6] = slot[7] = 0;
    write_le32(slot + 8, metadata->generation);
    memset(slot + 12, 0, 4);
    write_le64(slot + 16, metadata->reserved_end);
    write_le64(slot + 24, metadata->ack);
    write_le32(slot + 60, fram_crc32(slot, 60));
}

static bool metadata_decode(const uint8_t slot[FRAM_SLOT_BYTES], fram_metadata_t *metadata)
{
    if (memcmp(slot, "TBFR", 4) || slot[4] != 1 || slot[5] != 32 ||
        read_le32(slot + 60) != fram_crc32(slot, 60)) return false;
    fram_metadata_t decoded = {
        .generation = read_le32(slot + 8),
        .reserved_end = read_le64(slot + 16),
        .ack = read_le64(slot + 24),
    };
    if (!metadata_values_valid(decoded.reserved_end, decoded.ack)) return false;
    if (metadata) *metadata = decoded;
    return true;
}

static bool generation_newer(uint32_t candidate, uint32_t reference)
{
    return (int32_t)(candidate - reference) > 0;
}

static bool checkpoint_needed(const fram_metadata_t *current, uint64_t reserved_end, uint64_t ack)
{
    /*
     * Reservations never regress. An advancing reservation may mirror a lower
     * ACK after NVS recovery/reset: the FRAM ACK must not override authoritative
     * NVS and must not prevent subsequent reservation checkpoints.
     */
    return reserved_end >= current->reserved_end &&
           (reserved_end > current->reserved_end || ack >= current->ack) &&
           (reserved_end != current->reserved_end || ack != current->ack);
}

#ifndef TB_FRAM_HOST_TEST

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static struct {
    spi_device_handle_t device;
    SemaphoreHandle_t mutex;
    bool attempted;
    bool ready;
    int active_slot;
    fram_metadata_t metadata;
    const char *status;
} s = { .active_slot = -1, .status = "not_started" };

/* Full-duplex transfer: command/address bytes precede the returned data. */
static esp_err_t transfer(uint8_t command, bool addressed, uint16_t address,
                          const uint8_t *input, uint8_t *output, size_t length)
{
    if (!s.device || length > 256 ||
        (addressed && ((size_t)address + length > FRAM_BYTES))) return ESP_ERR_INVALID_ARG;
    const size_t header = addressed ? 3 : 1;
    uint8_t tx[259] = {0}, rx[259] = {0};
    tx[0] = command;
    if (addressed) {
        tx[1] = (uint8_t)(address >> 8);
        tx[2] = (uint8_t)address;
    }
    if (input) memcpy(tx + header, input, length);
    spi_transaction_t transaction = {
        .length = (header + length) * 8,
        .tx_buffer = tx,
        .rx_buffer = output ? rx : NULL,
    };
    esp_err_t result = spi_device_transmit(s.device, &transaction);
    if (result == ESP_OK && output) memcpy(output, rx + header, length);
    return result;
}

static bool read_status(uint8_t *status)
{
    return transfer(0x05, false, 0, NULL, status, 1) == ESP_OK;
}

static void fail(const char *status)
{
    /* Best effort: leave write enable clear even after a failed readback. */
    if (s.device) transfer(0x04, false, 0, NULL, NULL, 0);
    s.ready = false;
    s.status = status;
}

static bool detect_write_enable(void)
{
    uint8_t before = 0xff, enabled = 0xff, after = 0xff;
    if (transfer(0x04, false, 0, NULL, NULL, 0) != ESP_OK ||
        !read_status(&before) || before == 0xff || (before & 2)) return false;
    if (transfer(0x06, false, 0, NULL, NULL, 0) != ESP_OK ||
        !read_status(&enabled) || enabled != (uint8_t)(before | 2)) return false;
    if (transfer(0x04, false, 0, NULL, NULL, 0) != ESP_OK ||
        !read_status(&after) || after != before) return false;
    return true;
}

static bool journal_write(uint64_t reserved_end, uint64_t ack)
{
    if (!metadata_values_valid(reserved_end, ack)) {
        fail("invalid_metadata");
        return false;
    }
    const int target = s.active_slot == 0 ? 1 : 0;
    const uint16_t address = (uint16_t)(target * FRAM_SLOT_BYTES);
    fram_metadata_t next = {
        .generation = s.active_slot < 0 ? 1 : s.metadata.generation + 1,
        .reserved_end = reserved_end,
        .ack = ack,
    };
    uint8_t encoded[FRAM_SLOT_BYTES], readback[FRAM_SLOT_BYTES], status = 0;
    metadata_encode(encoded, &next);
    if (transfer(0x06, false, 0, NULL, NULL, 0) != ESP_OK ||
        !read_status(&status) || !(status & 2) || status == 0xff) {
        fail("write_enable_failed");
        return false;
    }
    esp_err_t result = transfer(0x02, true, address, encoded, NULL, sizeof(encoded));
    /* MB85RS64 writes have no EEPROM-style programming delay. */
    if (result == ESP_OK) result = transfer(0x03, true, address, NULL, readback, sizeof(readback));
    transfer(0x04, false, 0, NULL, NULL, 0);
    if (result != ESP_OK || memcmp(encoded, readback, sizeof(encoded)) ||
        !metadata_decode(readback, NULL)) {
        fail("journal_readback_failed");
        return false;
    }
    s.metadata = next;
    s.active_slot = target;
    s.ready = true;
    s.status = "ready";
    return true;
}

bool tb_fram_start(void)
{
    if (s.attempted) return s.ready;
    s.attempted = true;
    s.mutex = xSemaphoreCreateMutex();
    if (!s.mutex) {
        s.status = "no_memory";
        return false;
    }
    gpio_set_level(GPIO_NUM_10, 1);
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    spi_device_interface_config_t config = {
        .clock_speed_hz = 1000000,
        .mode = 0,
        .spics_io_num = 10,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &config, &s.device) != ESP_OK) {
        s.status = "spi_device_unavailable";
        return false;
    }
    if (!detect_write_enable()) {
        fail("no_wel_response");
        return false;
    }
    uint8_t slots[2][FRAM_SLOT_BYTES];
    if (transfer(0x03, true, 0, NULL, slots[0], FRAM_SLOT_BYTES) != ESP_OK ||
        transfer(0x03, true, FRAM_SLOT_BYTES, NULL, slots[1], FRAM_SLOT_BYTES) != ESP_OK) {
        fail("journal_read_failed");
        return false;
    }
    fram_metadata_t decoded[2];
    bool valid[2] = { metadata_decode(slots[0], &decoded[0]), metadata_decode(slots[1], &decoded[1]) };
    if (valid[0] || valid[1]) {
        s.active_slot = valid[0] ? 0 : 1;
        if (valid[1] && (!valid[0] || generation_newer(decoded[1].generation, decoded[0].generation))) s.active_slot = 1;
        s.metadata = decoded[s.active_slot];
        s.ready = true;
        s.status = "ready";
        return true;
    }
    /* An unknown chip image is never overwritten merely because its header is unknown. */
    uint8_t block[128];
    for (unsigned address = 0; address < FRAM_BYTES; address += sizeof(block)) {
        if (transfer(0x03, true, address, NULL, block, sizeof(block)) != ESP_OK) {
            fail("erased_scan_failed");
            return false;
        }
        for (unsigned i = 0; i < sizeof(block); ++i) if (block[i] != 0xff) {
            fail("foreign_data_preserved");
            return false;
        }
    }
    return journal_write(1, 0);
}

bool tb_fram_get_metadata(uint64_t *reserved_end, uint64_t *ack)
{
    if (!reserved_end || !ack || !s.mutex || xSemaphoreTake(s.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool valid = s.ready;
    if (valid) {
        *reserved_end = s.metadata.reserved_end;
        *ack = s.metadata.ack;
    }
    xSemaphoreGive(s.mutex);
    return valid;
}

void tb_fram_checkpoint(uint64_t reserved_end, uint64_t ack)
{
    if (!s.mutex || xSemaphoreTake(s.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    if (s.ready && checkpoint_needed(&s.metadata, reserved_end, ack)) journal_write(reserved_end, ack);
    xSemaphoreGive(s.mutex);
}

const char *tb_fram_status(void)
{
    return s.status;
}

#endif
