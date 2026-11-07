#include "protocols.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    char *out;
    size_t capacity;
    size_t used;
    bool ok;
} json_writer_t;

static void append(json_writer_t *writer, const char *format, ...)
{
    if (!writer->ok) {
        return;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(writer->out + writer->used,
                            writer->capacity - writer->used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= writer->capacity - writer->used) {
        writer->ok = false;
        return;
    }
    writer->used += (size_t)written;
}

static void append_hex(json_writer_t *writer, const uint8_t *data, size_t length)
{
    static const char hex[] = "0123456789abcdef";
    if (!writer->ok || length > (writer->capacity - writer->used - 1) / 2) {
        writer->ok = false;
        return;
    }
    for (size_t i = 0; i < length; ++i) {
        writer->out[writer->used++] = hex[data[i] >> 4];
        writer->out[writer->used++] = hex[data[i] & 15];
    }
    writer->out[writer->used] = '\0';
}

static bool finish(json_writer_t *writer)
{
    if (!writer->ok && writer->out && writer->capacity) {
        writer->out[0] = '\0';
    }
    return writer->ok;
}

static uint16_t le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

bool tb_protocol_can_json(char *out, size_t capacity, uint64_t uptime_us,
                          uint32_t identifier, bool extended, bool rtr,
                          uint8_t dlc, const uint8_t *data)
{
    if (out && capacity) {
        out[0] = '\0';
    }
    if (!out || !capacity || dlc > 8 ||
        identifier > (extended ? UINT32_C(0x1fffffff) : UINT32_C(0x7ff)) ||
        (!rtr && dlc && !data)) {
        return false;
    }
    json_writer_t writer = {out, capacity, 0, true};
    append(&writer, "{\"source\":\"can\",\"uptime_us\":%" PRIu64
           ",\"id\":%" PRIu32 ",\"extended\":%s,\"rtr\":%s,\"dlc\":%u,\"data\":\"",
           uptime_us, identifier, extended ? "true" : "false",
           rtr ? "true" : "false", (unsigned)dlc);
    append_hex(&writer, data, rtr ? 0 : dlc);
    append(&writer, "\"");

    if (extended && !rtr) {
        uint32_t pgn = (identifier >> 8) & UINT32_C(0x3ffff);
        uint8_t pdu_format = (uint8_t)(identifier >> 16);
        if (pdu_format < 240) {
            pgn &= UINT32_C(0x3ff00);
        }
        append(&writer, ",\"j1939\":{\"priority\":%u,\"pgn\":%" PRIu32
               ",\"source_address\":%u", (unsigned)((identifier >> 26) & 7),
               pgn, (unsigned)(identifier & 255));
        if (pdu_format < 240) {
            append(&writer, ",\"destination_address\":%u",
                   (unsigned)((identifier >> 8) & 255));
        }
        /* SAE J1939 two-byte numeric signals have the operational range
         * 0x0000..0xFAFF. 0xFB00..0xFFFF includes reserved/error/NA codes. */
        if (pgn == UINT32_C(61444) && dlc >= 5) {
            uint16_t raw = le16(data + 3);
            if (raw <= UINT16_C(0xfaff)) {
                append(&writer, ",\"engine_rpm\":%u.%03u",
                       (unsigned)(raw / 8), (unsigned)((raw % 8) * 125));
            }
        }
        if (pgn == UINT32_C(65265) && dlc >= 3) {
            uint16_t raw = le16(data + 1);
            if (raw <= UINT16_C(0xfaff)) {
                /* denominator 256 divides 10^8, so eight digits are exact */
                append(&writer, ",\"vehicle_speed_kmh\":%u.%08u",
                       (unsigned)(raw / 256),
                       (unsigned)((uint32_t)(raw % 256) * UINT32_C(390625)));
            }
        }
        append(&writer, "}");
    }
    append(&writer, "}");
    return finish(&writer);
}

bool tb_protocol_j1708_checksum_valid(const uint8_t *frame, size_t length)
{
    if (!frame || length < 2 || length > 21) {
        return false;
    }
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + frame[i]);
    }
    return sum == 0;
}

typedef struct {
    uint16_t pid;
    const uint8_t *data;
    size_t length;
} j1587_parameter_t;

static bool parameters(const uint8_t *frame, size_t length,
                       j1587_parameter_t *result, size_t *count)
{
    size_t offset = 1;
    size_t end = length - 1;
    uint16_t page = 0;
    *count = 0;
    if (offset == end) {
        return false;
    }
    /* J1587 section 3.3: extension is allowed only immediately after MID,
     * and selects page 2 for every subsequent parameter in this message. */
    if (frame[offset] == 255) {
        page = 256;
        if (++offset == end) {
            return false;
        }
    }
    while (offset < end) {
        uint8_t pid = frame[offset++];
        uint16_t full_pid = (uint16_t)(page + pid);
        /* PID 254/510 is a data-link escape, not a variable-length parameter;
         * PID 255/511 is an invalid/reserved extension in this position. */
        if (pid >= 254) {
            return false;
        }
        size_t size;
        if (pid < 128) {
            size = 1;
        } else if (pid < 192) {
            size = 2;
        } else {
            if (offset == end) {
                return false;
            }
            size = frame[offset++];
        }
        if (size > end - offset || *count >= 10) {
            return false;
        }
        result[*count] = (j1587_parameter_t){full_pid, frame + offset, size};
        ++*count;
        offset += size;
    }
    return true;
}

bool tb_protocol_j1708_json(char *out, size_t capacity, uint64_t uptime_us,
                            const uint8_t *frame, size_t length)
{
    if (out && capacity) {
        out[0] = '\0';
    }
    if (!out || !capacity || !tb_protocol_j1708_checksum_valid(frame, length)) {
        return false;
    }
    j1587_parameter_t decoded[10];
    size_t count;
    bool layout_ok = parameters(frame, length, decoded, &count);
    json_writer_t writer = {out, capacity, 0, true};
    append(&writer, "{\"source\":\"j1708\",\"uptime_us\":%" PRIu64
           ",\"mid\":%u,\"raw\":\"", uptime_us, (unsigned)frame[0]);
    append_hex(&writer, frame, length);
    append(&writer, "\",\"checksum_valid\":true,\"j1587_layout_valid\":%s",
           layout_ok ? "true" : "false");
    if (layout_ok) {
        append(&writer, ",\"parameters\":[");
        for (size_t i = 0; i < count; ++i) {
            append(&writer, "%s{\"pid\":%u,\"data\":\"", i ? "," : "",
                   (unsigned)decoded[i].pid);
            append_hex(&writer, decoded[i].data, decoded[i].length);
            append(&writer, "\"}");
        }
        append(&writer, "]");
    }
    /*
     * SAE J1587 FEB2002 sections 3.4, A.84, A.183, A.190 and A.245.
     * Numeric parameters are little-endian. The imperial resolutions below
     * are converted with the exact mile and US-gallon definitions; the SI
     * factors printed in J1587 are rounded. This test release applies the
     * provisional catalog's all-FF unavailable policy consistently here and
     * in the new decoder; raw bytes remain available for later validation.
     * MIDs below 128 may carry non-J1587 data: never infer these signals there.
     */
    if (layout_ok && frame[0] >= 128) {
        bool speed = false, rpm = false, fuel = false, odometer = false;
        for (size_t i = 0; i < count; ++i) {
            const j1587_parameter_t *parameter = &decoded[i];
            if (parameter->pid == 84 && parameter->length == 1 && !speed && parameter->data[0] != UINT8_MAX) {
                append(&writer, ",\"speed_kmh\":%.6f",
                       parameter->data[0] * 0.804672);
                speed = true;
            } else if (parameter->pid == 190 && parameter->length == 2 && !rpm && le16(parameter->data) != UINT16_MAX) {
                append(&writer, ",\"engine_rpm\":%.2f",
                       le16(parameter->data) * 0.25);
                rpm = true;
            } else if (parameter->pid == 183 && parameter->length == 2 && !fuel && le16(parameter->data) != UINT16_MAX) {
                append(&writer, ",\"fuel_rate_lph\":%.9f",
                       le16(parameter->data) * 0.059147059125);
                fuel = true;
            } else if (parameter->pid == 245 && parameter->length == 4 && !odometer && le32(parameter->data) != UINT32_MAX) {
                append(&writer, ",\"odometer_km\":%.7f",
                       le32(parameter->data) * 0.1609344);
                odometer = true;
            }
        }
    }
    append(&writer, "}");
    return finish(&writer);
}
