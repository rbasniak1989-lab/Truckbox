#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t tb_store_init(void);
#endif

static inline uint32_t tb_store_crc32_update(uint32_t crc, const void *data, size_t length)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                  (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return crc;
}

/* IEEE CRC-32, for torn/corrupt records, not authentication. */
static inline uint32_t tb_store_crc32(const void *data, size_t length)
{
    return ~tb_store_crc32_update(UINT32_C(0xffffffff), data, length);
}

/* Storage record format v1: CRC protects sequence and timestamp too. The
 * integer prefix is fixed-width LE, independent of ABI and JSON formatting. */
static inline uint32_t tb_store_record_crc(uint64_t sequence, uint64_t uptime_us,
                                           const void *payload, size_t length)
{
    unsigned char metadata[16];
    for (unsigned i = 0; i < 8; ++i) {
        metadata[i] = (unsigned char)(sequence >> (8U * i));
        metadata[8U + i] = (unsigned char)(uptime_us >> (8U * i));
    }
    uint32_t crc = tb_store_crc32_update(UINT32_C(0xffffffff),
                                        metadata, sizeof(metadata));
    return ~tb_store_crc32_update(crc, payload, length);
}

/* Locate the exact payload bytes in a complete line emitted by store.c.
 * Unterminated final writes are never accepted as complete records. */
static inline bool tb_store_payload_span(const char *line, size_t length,
                                         const char **payload, size_t *payload_length)
{
    static const char prefix[] = "{\"seq\":";
    static const char marker[] = ",\"data\":";
    if (!line || !payload || !payload_length || length < 20 ||
        memcmp(line, prefix, sizeof(prefix) - 1) != 0 ||
        line[length - 1] != '\n' || line[length - 2] != '}') {
        return false;
    }
    for (size_t i = sizeof(prefix) - 1; i + sizeof(marker) - 1 < length - 2; ++i) {
        if (memcmp(line + i, marker, sizeof(marker) - 1) == 0) {
            *payload = line + i + sizeof(marker) - 1;
            *payload_length = length - i - (sizeof(marker) - 1) - 2;
            return *payload_length > 0;
        }
    }
    return false;
}
