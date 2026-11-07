#ifndef TBX_J1587_DECODER_H
#define TBX_J1587_DECODER_H
#include <stdbool.h>
#include <stdint.h>
/* These 17 signals are ALL experimental on the tested Volvo; do not use for
 * brake/engine safety interlocks or automatic fault diagnosis. */
typedef enum {
    TBX_J1587_VALUE_EM_VALIDACAO = 0,
    TBX_J1587_VALUE_INDISPONIVEL,
    TBX_J1587_VALUE_TAMANHO_INVALIDO,
    TBX_J1587_VALUE_PID_NAO_SUPORTADO
} tbx_j1587_value_status_t;
typedef struct {
    uint16_t pid;
    uint8_t mid;
    uint8_t raw[4];
    uint8_t raw_length;
    uint32_t raw_unsigned;
    /* For binary statuses, each field is: 0=off, 1=on, 2=error,
     * 3=not available; index 0 = bits 1-2, index 3 = bits 7-8. */
    uint8_t status_fields[4];
    bool numeric;
    bool bitmapped;
    double value;
    double scale;
    const char *unit;
    tbx_j1587_value_status_t status;
} tbx_j1587_decoded_t;
/* Pass only the PID data bytes (exclude MID, PID, J1587 length prefix and
 * checksum). The caller must verify frame checksum, sender MID and freshness. */
bool tbx_j1587_decode(uint8_t mid, uint16_t pid, const uint8_t *data,
                      uint8_t len, tbx_j1587_decoded_t *out);
#endif
