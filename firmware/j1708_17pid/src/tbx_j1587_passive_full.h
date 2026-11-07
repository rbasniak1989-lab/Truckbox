#ifndef TBX_J1587_PASSIVE_FULL_H
#define TBX_J1587_PASSIVE_FULL_H

#include "tbx_j1587_passive.h"
#include <stdbool.h>
#include <stdint.h>

/* Full parser of the 31 initially-raw J1708/J1587 passive PIDs.
 * All results remain PROVISIONAL for this Volvo; raw data is retained.
 * This is a decoder, not a J1708 frame parser or a vehicle control API. */
#define TBX_J1587_MAX_FIELDS 8u
#define TBX_J1587_MAX_DTCS 16u

typedef enum {
    TBX_J1587_KIND_SCALAR=1,
    TBX_J1587_KIND_STATES,
    TBX_J1587_KIND_TEXT,
    TBX_J1587_KIND_REQUEST,
    TBX_J1587_KIND_DTCS,
    TBX_J1587_KIND_CLOCK,
    TBX_J1587_KIND_DATE
} tbx_j1587_kind_t;

typedef struct {
    const char *name;
    /* Individual flag: 0/1. Two-bit SAE state: 0=off, 1=on,
       2=error, 3=unavailable. Manufacturer meanings may differ. */
    uint8_t state;
    uint8_t width;
} tbx_j1587_state_field_t;

typedef struct {
    uint16_t identifier; /* PID, SID, or page-2 PID */
    uint8_t fmi;
    bool sid;
    bool page2;
    bool active;
    bool has_occurrences;
    uint8_t occurrences;
} tbx_j1587_dtc_t;

typedef struct {
    tbx_j1587_passive_reading_t base;
    tbx_j1587_kind_t kind;
    bool interpreted;
    bool manufacturer_detail_required;
    double numeric_value;
    const char *unit;
    tbx_j1587_state_field_t fields[TBX_J1587_MAX_FIELDS];
    uint8_t field_count;
    /* 162/163 can carry a manufacturer control code in the first byte. */
    char text[3];
    uint16_t requested_pid;
    uint8_t target_mid;
    tbx_j1587_dtc_t dtcs[TBX_J1587_MAX_DTCS];
    uint8_t dtc_count;
    /* 251 and 252 use components, never treat them as a single integer. */
    uint8_t hour, minute;
    double second;
    uint16_t year;
    uint8_t month, day;
} tbx_j1587_full_reading_t;

/* data = *value* bytes only (no MID, PID, length prefix, checksum,
 * page-2 envelope). Caller validates checksum, length and source.
 * A PID 194 with zero data bytes means no DTCs.
 * Returns false on unsupported, invalid length, or malformed DTC list. */
bool tbx_j1587_decode_passive_full(uint8_t mid, uint16_t pid,
                                  const uint8_t *data, uint8_t length,
                                  tbx_j1587_full_reading_t *out);
#endif
