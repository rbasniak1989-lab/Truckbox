#include "tbx_j1587_bridge.h"
#include <string.h>

typedef struct { uint16_t pid; const uint8_t *data; uint8_t len; } parameter_t;

static bool parse_parameters(const uint8_t *frame, size_t length,
                             parameter_t out[10], unsigned *count)
{
    size_t pos = 1, end = length - 1;
    uint16_t page = 0;
    *count = 0;
    if (pos == end) return false;
    if (frame[pos] == 255u) {
        page = 256;
        if (++pos == end) return false;
    }
    while (pos < end) {
        uint8_t pid = frame[pos++];
        /* PID254 is a data-link escape; its payload must remain raw. */
        if (pid >= 254u) return false;
        unsigned len;
        if (pid < 128u) len = 1;
        else if (pid < 192u) len = 2;
        else {
            if (pos == end) return false;
            len = frame[pos++];
        }
        if (len > end - pos || *count >= 10u) return false;
        out[(*count)++] = (parameter_t){ (uint16_t)(page + pid), frame + pos, (uint8_t)len };
        pos += len;
    }
    return true;
}

static tbx_j1587_observation_quality_t passive_quality(tbx_passive_quality_t q)
{
    switch (q) {
    case TBX_PASSIVE_EM_VALIDACAO: return TBX_J1587_OBS_EM_VALIDACAO;
    case TBX_PASSIVE_INDISPONIVEL: return TBX_J1587_OBS_INDISPONIVEL;
    case TBX_PASSIVE_COMPRIMENTO_INVALIDO: return TBX_J1587_OBS_COMPRIMENTO_INVALIDO;
    default: return TBX_J1587_OBS_DESCONHECIDO;
    }
}

static const char *requested_name(uint16_t pid)
{
    switch (pid) {
    case 26: return "Percentual estimado da ventoinha";
    case 31: return "Posicao range";
    case 32: return "Posicao splitter";
    case 33: return "Posicao cilindro da embreagem";
    case 34: return "Estados de atuadores";
    case 37: return "Pressao do ar da transmissao";
    case 62: return "Estado de inibicao do retarder";
    case 63: return "Estados da transmissao";
    case 68: return "Limite de torque";
    case 108: return "Pressao atmosferica";
    case 117: return "Pressao do ar primario";
    case 118: return "Pressao do ar secundario";
    case 158: return "Tensao com chave";
    case 160: return "Rotacao do eixo principal";
    case 235: return "Horas acumuladas em marcha lenta";
    case 236: return "Litros acumulados em marcha lenta";
    case 250: return "Litros totais acumulados na ECU";
    default: return "PID de teste por requisicao";
    }
}

tbx_j1587_frame_result_t tbx_j1587_bridge_decode(
    const uint8_t *frame, size_t length, uint64_t uptime_ms,
    tbx_j1587_observation_fn callback, void *context)
{
    if (!frame || length < 2u || length > 21u) return TBX_J1587_FRAME_LENGTH_INVALID;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum = (uint8_t)(sum + frame[i]);
    if (sum) return TBX_J1587_FRAME_CHECKSUM_INVALID;
    if (frame[0] < 128u) return TBX_J1587_FRAME_NON_J1587_MID;
    parameter_t parameters[10];
    unsigned count;
    if (!parse_parameters(frame, length, parameters, &count)) return TBX_J1587_FRAME_LAYOUT_INVALID;
    for (unsigned i = 0; i < count; ++i) {
        const parameter_t *p = &parameters[i];
        tbx_j1587_observation_t o;
        memset(&o, 0, sizeof(o));
        o.mid = frame[0]; o.pid = p->pid; o.uptime_ms = uptime_ms;
        o.raw = p->data; o.raw_len = p->len;
        o.name = "PID sem catalogo"; o.unit = "raw";
        (void)tbx_j1587_decode(o.mid, o.pid, o.raw, o.raw_len, &o.requested);
        if (o.requested.status != TBX_J1587_VALUE_PID_NAO_SUPORTADO) {
            o.request_catalog = true;
            o.name = requested_name(o.pid);
            o.unit = o.requested.unit;
            switch (o.requested.status) {
            case TBX_J1587_VALUE_EM_VALIDACAO:
                o.quality = TBX_J1587_OBS_EM_VALIDACAO;
                o.has_numeric = o.requested.numeric;
                o.value = o.requested.value;
                break;
            case TBX_J1587_VALUE_INDISPONIVEL: o.quality = TBX_J1587_OBS_INDISPONIVEL; break;
            default: o.quality = TBX_J1587_OBS_COMPRIMENTO_INVALIDO; break;
            }
        } else {
            (void)tbx_j1587_decode_passive_full(o.mid, o.pid, o.raw, o.raw_len, &o.passive);
            o.quality = passive_quality(o.passive.base.quality);
            if (o.passive.base.spec) {
                o.name = o.passive.base.spec->name;
                o.unit = o.passive.unit ? o.passive.unit : o.passive.base.spec->unit;
                o.has_numeric = o.quality == TBX_J1587_OBS_EM_VALIDACAO && o.passive.base.has_numeric_value;
                o.value = o.passive.base.converted_value;
            }
        }
        if (callback) callback(&o, context);
    }
    return TBX_J1587_FRAME_OK;
}

const char *tbx_j1587_observation_quality_name(tbx_j1587_observation_quality_t q)
{
    switch (q) {
    case TBX_J1587_OBS_EM_VALIDACAO: return "EM_VALIDACAO";
    case TBX_J1587_OBS_INDISPONIVEL: return "INDISPONIVEL";
    case TBX_J1587_OBS_COMPRIMENTO_INVALIDO: return "COMPRIMENTO_INVALIDO";
    default: return "SEM_DECODIFICACAO";
    }
}

const char *tbx_j1587_frame_result_name(tbx_j1587_frame_result_t r)
{
    switch (r) {
    case TBX_J1587_FRAME_OK: return "OK";
    case TBX_J1587_FRAME_LENGTH_INVALID: return "COMPRIMENTO_INVALIDO";
    case TBX_J1587_FRAME_CHECKSUM_INVALID: return "CHECKSUM_INVALIDO";
    case TBX_J1587_FRAME_NON_J1587_MID: return "MID_NAO_J1587";
    default: return "LAYOUT_INVALIDO";
    }
}

bool tbx_j1708_revb1_tx_available(void) { return false; }
const char *tbx_j1708_revb1_tx_block_reason(void)
{
    return "TX_INIBIDO: Rev.B.1 DI=0/DE21; transmissor, arbitragem e MID local nao validados";
}

bool tbx_j1708_revb1_arm(tbx_j1708_request_scheduler_t *s,
                         bool explicitly_authorized, bool stationary)
{
    (void)explicitly_authorized; (void)stationary;
    tbx_j1708_request_permit_stationary_test(s, false);
    tbx_j1708_request_stop(s);
    return false;
}

bool tbx_j1587_counter_delta(tbx_j1587_counter_checkpoint_t *checkpoint,
                             const tbx_j1587_observation_t *o, double *delta)
{
    if (!checkpoint || !o || !delta || !o->request_catalog || !o->has_numeric ||
        o->quality != TBX_J1587_OBS_EM_VALIDACAO ||
        (o->pid != 235u && o->pid != 236u && o->pid != 250u)) return false;
    bool valid_delta = checkpoint->valid && checkpoint->mid == o->mid &&
        checkpoint->pid == o->pid && o->requested.raw_unsigned >= checkpoint->raw;
    if (valid_delta)
        *delta = (double)(o->requested.raw_unsigned - checkpoint->raw) * o->requested.scale;
    /* Backwards counter can be reset, replacement or rollover: preserve its
     * new baseline, but never manufacture a huge unsigned wrapped trip. */
    *checkpoint = (tbx_j1587_counter_checkpoint_t){ true, o->mid, o->pid, o->requested.raw_unsigned };
    return valid_delta;
}
