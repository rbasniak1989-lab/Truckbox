#ifndef TBX_J1587_PASSIVE_H
#define TBX_J1587_PASSIVE_H
#include <stdbool.h>
#include <stdint.h>

/* Passive catalogue from Volvo FH 540 J1708 field capture, 2026-10-29.
 * This component NEVER transmits onto the vehicle network. */
#define TBX_J1587_PASSIVE_PID_COUNT 48u
#define TBX_J1587_MAX_PASSIVE_DATA 32u

typedef enum {
    TBX_PASSIVE_EM_VALIDACAO=0,
    TBX_PASSIVE_RAW_AGUARDANDO_ESCALA,
    TBX_PASSIVE_INDISPONIVEL,
    TBX_PASSIVE_COMPRIMENTO_INVALIDO,
    TBX_PASSIVE_DESCONHECIDO
} tbx_passive_quality_t;

typedef struct {
    uint16_t pid;
    const char *name;
    const char *unit;
    uint8_t expected_len; /* 0 indicates variable-length payload */
    bool numeric;
    double multiplier, offset;
} tbx_j1587_passive_spec_t;

typedef struct {
    uint8_t mid;
    uint16_t pid;
    uint8_t payload[TBX_J1587_MAX_PASSIVE_DATA];
    uint8_t payload_len;
    uint32_t raw_unsigned;
    bool has_numeric_value;
    double converted_value;
    const tbx_j1587_passive_spec_t *spec;
    tbx_passive_quality_t quality;
} tbx_j1587_passive_reading_t;

const tbx_j1587_passive_spec_t *tbx_j1587_passive_specs(unsigned *count);
/* Caller validates J1708 checksum and strips MID/PID, any J1587 length
 * prefix, and page-2 envelope (e.g. PID440) before passing value bytes.
 * Caller also distinguishes passive frames from replies to active polls. */
bool tbx_j1587_decode_passive(uint8_t mid, uint16_t pid,
                             const uint8_t *payload, uint8_t payload_len,
                             tbx_j1587_passive_reading_t *out);
#endif
