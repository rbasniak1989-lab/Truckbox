#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TBX_DTC_HISTORY_CAPACITY 32u
#define TBX_DTC_SECTION_SESSIONS 4u
#define TBX_DTC_MAX_PAYLOAD 239u
#define TBX_DTC_SECTION_TIMEOUT_MS UINT64_C(5000)

typedef struct {
    uint16_t identifier;
    uint8_t fmi, occurrences, raw[3], raw_length;
    bool sid, page2, active, has_occurrences;
} tbx_dtc_t;
typedef void (*tbx_dtc_fn)(const tbx_dtc_t *, void *);
/* VALUE bytes only: PID194's byte count is removed by the frame parser.
 * A page-2 code is not automatically a proprietary PPID/PSID. */
bool tbx_j1587_dtc_parse(const uint8_t *, size_t, tbx_dtc_fn, void *, size_t *count);

typedef struct { uint32_t boot; uint64_t uptime_ms, unix_ms; } tbx_dtc_time_t;
typedef struct {
    tbx_dtc_t code;
    tbx_dtc_time_t first, last;
    uint32_t observations, transitions;
    uint8_t mid;
    bool used, dirty, notify;
} tbx_dtc_history_entry_t;
typedef struct {
    tbx_dtc_history_entry_t entries[TBX_DTC_HISTORY_CAPACITY];
    uint32_t invalid_messages, empty_messages, capacity_rejected;
    bool changed;
} tbx_dtc_history_t;
/* Updates every complete code independently; malformed lists never clear or
 * mutate known states. An empty/absent broadcast never resolves a fault. */
bool tbx_dtc_history_observe(tbx_dtc_history_t *,uint8_t mid,const uint8_t *,size_t,tbx_dtc_time_t);

typedef struct {
    uint64_t started_ms, last_ms;
    uint16_t length;
    uint8_t mid, expected, last_section, total;
    bool used;
    uint8_t bytes[TBX_DTC_MAX_PAYLOAD];
} tbx_dtc_section_t;
typedef struct {
    tbx_dtc_section_t sessions[TBX_DTC_SECTION_SESSIONS];
    uint32_t completed, invalid, timeouts, capacity_rejected;
} tbx_dtc_sections_t;
typedef void (*tbx_dtc_payload_fn)(uint8_t,const uint8_t *,size_t,uint64_t,uint64_t,void *);
/* PID192 value starts with original PID, section nibbles, and (section0
 * only) original value length. Only PID194 is assembled; never transmits. */
bool tbx_dtc_section_feed(tbx_dtc_sections_t *,uint8_t,const uint8_t *,size_t,uint64_t,tbx_dtc_payload_fn,void *);
void tbx_dtc_section_expire(tbx_dtc_sections_t *,uint64_t);
