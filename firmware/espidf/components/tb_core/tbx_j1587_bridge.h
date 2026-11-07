#ifndef TBX_J1587_BRIDGE_H
#define TBX_J1587_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tbx_j1587_decoder.h"
#include "tbx_j1587_passive_full.h"
#include "tbx_j1708_requests.h"

#define TBX_J1587_DECODER_VERSION "0.2.0-sae-provisional"

typedef enum {
    TBX_J1587_FRAME_OK = 0,
    TBX_J1587_FRAME_LENGTH_INVALID,
    TBX_J1587_FRAME_CHECKSUM_INVALID,
    TBX_J1587_FRAME_NON_J1587_MID,
    TBX_J1587_FRAME_LAYOUT_INVALID
} tbx_j1587_frame_result_t;

typedef enum {
    TBX_J1587_OBS_EM_VALIDACAO = 0,
    TBX_J1587_OBS_INDISPONIVEL,
    TBX_J1587_OBS_COMPRIMENTO_INVALIDO,
    TBX_J1587_OBS_DESCONHECIDO
} tbx_j1587_observation_quality_t;

/* A callback borrows this structure and raw bytes only for that call.
 * All observations are passively received. Membership in the request catalog
 * never establishes that a message was a reply to a request. */
typedef struct {
    uint8_t mid;
    uint16_t pid;
    uint64_t uptime_ms;
    const uint8_t *raw;
    uint8_t raw_len;
    const char *name;
    const char *unit;
    bool has_numeric;
    double value;
    bool request_catalog;
    tbx_j1587_observation_quality_t quality;
    tbx_j1587_full_reading_t passive;
    tbx_j1587_decoded_t requested;
} tbx_j1587_observation_t;

typedef void (*tbx_j1587_observation_fn)(const tbx_j1587_observation_t *, void *);

/* Validates checksum and complete layout before emitting any parameter.
 * Accepts complete classic J1708 messages including MID/checksum, maximum21
 * bytes. PID255 only directly after MID selects page2 for all parameters.
 * No allocation, GPIO, blocking operation, clock update or transmission. */
tbx_j1587_frame_result_t tbx_j1587_bridge_decode(
    const uint8_t *frame, size_t length, uint64_t uptime_ms,
    tbx_j1587_observation_fn callback, void *context);
const char *tbx_j1587_observation_quality_name(tbx_j1587_observation_quality_t);
const char *tbx_j1587_frame_result_name(tbx_j1587_frame_result_t);

/* Rev.B.1 source HARDWARE.md documents DI tied LOW and DE21 kept LOW.
 * It proves passive reception, but supplies no validated transmitter,
 * bus-access/arbitration driver or local MID. DE-based transmission might
 * be electrically possible; this build must not infer or activate it. */
bool tbx_j1708_revb1_tx_available(void);
const char *tbx_j1708_revb1_tx_block_reason(void);
bool tbx_j1708_revb1_arm(tbx_j1708_request_scheduler_t *,
                         bool explicitly_authorized, bool stationary);

/* Accumulated ECU counters: caller persists this checkpoint if trip deltas
 * must survive reboot. This never resets a physical ECU counter. Changed
 * MID/PID, backwards counters or missing baselines produce no delta. */
typedef struct {
    bool valid;
    uint8_t mid;
    uint16_t pid;
    uint32_t raw;
} tbx_j1587_counter_checkpoint_t;
bool tbx_j1587_counter_delta(tbx_j1587_counter_checkpoint_t *,
                             const tbx_j1587_observation_t *, double *delta);

#endif
