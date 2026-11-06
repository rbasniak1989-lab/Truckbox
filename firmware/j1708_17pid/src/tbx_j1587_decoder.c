#include "tbx_j1587_decoder.h"
#include <string.h>

typedef struct {
    uint16_t pid;
    uint8_t size;
    double factor;
    const char *unit;
    bool bitmapped;
} pid_spec_t;
/* SAE J1587 Revised FEB2002, Appendix A. 1/8 US gallon is exact in SI:
 * 3.785411784 / 8 = 0.473176473 litres, rather than rounded 0.473. */
static const pid_spec_t specs[] = {
    {26, 1, 0.4, "%", false},
    {31, 1, 0.4, "%", false},
    {32, 1, 0.4, "%", false},
    {33, 1, 0.4, "%", false},
    {34, 1, 1.0, "estados", true},
    {37, 1, 0.0689, "bar", false},     /* 6.89 kPa/count */
    {62, 1, 1.0, "estado", true},
    {63, 1, 1.0, "estados", true},
    {68, 1, 0.5, "%", false},
    {108, 1, 0.431, "kPa", false},
    {117, 1, 0.0414, "bar", false},   /* 4.14 kPa/count */
    {118, 1, 0.0414, "bar", false},
    {158, 2, 0.05, "V", false},
    {160, 2, 0.25, "rpm", false},
    {235, 4, 0.05, "h", false},
    {236, 4, 0.473176473, "L", false},
    {250, 4, 0.473176473, "L", false}
};

bool tbx_j1587_decode(uint8_t mid, uint16_t pid, const uint8_t *data,
                      uint8_t len, tbx_j1587_decoded_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->pid = pid;
    out->mid = mid;
    out->status = TBX_J1587_VALUE_PID_NAO_SUPORTADO;
    const pid_spec_t *spec = NULL;
    for (unsigned i=0; i<sizeof(specs)/sizeof(specs[0]); ++i) {
        if (specs[i].pid == pid) { spec = &specs[i]; break; }
    }
    if (!spec) return false;
    out->scale = spec->factor;
    out->unit = spec->unit;
    out->numeric = !spec->bitmapped;
    out->bitmapped = spec->bitmapped;
    out->status = TBX_J1587_VALUE_TAMANHO_INVALIDO;
    if (!data || len != spec->size) return false;
    out->raw_length = len;
    memcpy(out->raw, data, len);
    uint32_t raw = 0;
    for (uint8_t i=0; i<len; ++i) raw |= (uint32_t)data[i] << (8u*i);
    out->raw_unsigned = raw;
    if (spec->bitmapped) {
        for (uint8_t i=0; i<4; ++i) out->status_fields[i] = (uint8_t)((raw >> (i*2u)) & 3u);
        out->status = (pid == 62 && out->status_fields[0] == 3u) ||
                      ((pid == 34 || pid == 63) && raw == 0xffu)
                    ? TBX_J1587_VALUE_INDISPONIVEL : TBX_J1587_VALUE_EM_VALIDACAO;
        return true;
    }
    if ((len == 1 && raw == 0xffu) ||
        (len == 2 && raw == 0xffffu) ||
        (len == 4 && raw == UINT32_MAX)) {
        out->status = TBX_J1587_VALUE_INDISPONIVEL;
        return true;
    }
    out->value = (double)raw * spec->factor;
    out->status = TBX_J1587_VALUE_EM_VALIDACAO;
    return true;
}
