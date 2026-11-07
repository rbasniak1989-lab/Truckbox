#ifndef TBX_J1939_H
#define TBX_J1939_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TBX_J1939_SIGNAL_COUNT 76u
#define TBX_J1939_PAYLOAD_MAX 64u
#define TBX_J1939_COMPONENT_MAX 16u
#define TBX_J1939_DECODER_VERSION "truckbox-j1939-0.2.0"
/* These values describe provenance, independently from a sample's quality. */
typedef enum {
    TBX_J1939_LOG_VALIDATED, TBX_J1939_IN_VALIDATION, TBX_J1939_PENDING,
    TBX_J1939_UNAVAILABLE, TBX_J1939_UNDECODED
} tbx_j1939_validation_t;
typedef enum {
    TBX_J1939_OK, TBX_J1939_NA, TBX_J1939_ERROR, TBX_J1939_RESERVED,
    TBX_J1939_BAD_LENGTH, TBX_J1939_RANGE, TBX_J1939_NO_FORMULA,
    TBX_J1939_BASELINE, TBX_J1939_COUNTER_RESET, TBX_J1939_WRONG_SOURCE,
    TBX_J1939_UNKNOWN_STATE, TBX_J1939_PARTIAL, TBX_J1939_PROFILE
} tbx_j1939_quality_t;
typedef struct {
    unsigned id;
    uint32_t pgn;
    const char *name, *unit, *evidence;
    tbx_j1939_validation_t validation;
    bool implemented, proprietary, derived;
} tbx_j1939_signal_t;
typedef struct {
    const char *name, *unit;
    tbx_j1939_quality_t quality;
    bool has_value;
    double value;
} tbx_j1939_component_t;
typedef struct {
    const tbx_j1939_signal_t *signal;
    uint32_t can_id, pgn;
    uint8_t sa, pf, ps, dlc;
    uint64_t captured_ms;
    uint8_t raw[TBX_J1939_PAYLOAD_MAX];
    tbx_j1939_validation_t validation;
    bool raw_only;
    uint8_t component_count;
    tbx_j1939_component_t components[TBX_J1939_COMPONENT_MAX];
    tbx_j1939_quality_t quality;
    bool has_value;
    double value;
    char text[24];
    /* Nonzero only for a derived power estimate using documented calibration. */
    double reference_torque_nm;
} tbx_j1939_observation_t;
typedef struct {
    bool baseline[256];
    uint32_t last_counter[256];
    uint64_t counter_at_ms[256];
    /* Deltas never cross source addresses, resets, or a long discontinuity. */
    double session_distance_km[256];
    struct { bool used; uint8_t sa; bool valid[2]; double kg[2]; uint64_t at_ms[2]; } weights[8];
} tbx_j1939_t;
typedef void (*tbx_j1939_emit_fn)(const tbx_j1939_observation_t *, void *);
void tbx_j1939_init(tbx_j1939_t *decoder);
uint32_t tbx_j1939_pgn(uint32_t can_id);
/* Diagnostic classification only; never rewrites PGN or rejects raw traffic. */
bool tbx_j1939_capture_alias(uint32_t pgn, uint8_t source_address);
const tbx_j1939_signal_t *tbx_j1939_catalog(size_t *count);
const char *tbx_j1939_quality_name(tbx_j1939_quality_t quality);
const char *tbx_j1939_validation_name(tbx_j1939_validation_t validation);
/* No allocation, transmission, alias rewrite, clock/GPS dependency, or blocking.
 * Emits every catalog row for the actual PGN, including unavailable/unknown rows.
 * Call synchronously from a single decoder task; callback owns no pointer after return.
 * Raw frames not represented in this catalog must still be logged by caller. */
size_t tbx_j1939_decode(tbx_j1939_t *decoder, uint32_t can_id, bool extended,
                       bool rtr, const uint8_t *data, size_t length,
                       uint64_t captured_ms, tbx_j1939_emit_fn emit, void *context);
/* Complete bounded J1939 message from a separately verified transport session.
 * Its length is NOT a physical CAN DLC; classic decode remains limited to8. */
size_t tbx_j1939_decode_payload(tbx_j1939_t *decoder, uint32_t pgn, uint8_t sa,
                       const uint8_t *data, size_t length, uint64_t captured_ms,
                       tbx_j1939_emit_fn emit, void *context);
#endif
