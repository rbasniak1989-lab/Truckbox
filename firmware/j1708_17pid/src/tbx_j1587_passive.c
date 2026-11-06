#include "tbx_j1587_passive.h"
#include <string.h>

/* Field matrix 29 Oct 2026: 47 PIDs labelled PASSIVE_SEEN plus PID247,
 * seen during passive baseline but labelled LIKELY_RESPONDED. PID188 has no
 * baseline sample despite its PASSIVE_SEEN label; keep provisional.
 * This includes diagnostic/control PIDs, which must not become dashboard
 * sensor values. Standard, unambiguous scalar formulas are converted.
 * Others remain RAW_AGUARDANDO_ESCALA to avoid misleading readings.
 * All numerical results retain status EM_VALIDACAO. */
#define RAW(p,n,len) {p,n,"raw",len,false,0.0,0.0}
#define VAL(p,n,len,factor,offset,unit) {p,n,unit,len,true,factor,offset}
static const tbx_j1587_passive_spec_t catalog[] = {
    RAW(36,"Clutch plates wear condition",1),
    RAW(43,"Ignition switch",1),
    RAW(44,"Warning/indicator lamps",1),
    RAW(46,"Vehicle wet tank pressure",1),
    RAW(49,"ABS control status",1),
    RAW(70,"Parking brake switch",1),
    RAW(71,"Idle shutdown timer status",1),
    RAW(83,"Road speed limit status",1),
    VAL(84,"Road speed",1,0.804672,0.0,"km/h"),
    RAW(85,"Cruise control status",1),
    RAW(86,"Cruise control set speed",1),
    RAW(89,"Power takeoff status",1),
    VAL(91,"Accelerator pedal",1,0.4,0.0,"%"),
    VAL(92,"Engine load",1,0.5,0.0,"%"),
    VAL(96,"Fuel level",1,0.5,0.0,"%"),
    RAW(97,"Water in fuel indicator",1),
    VAL(98,"Engine oil level",1,0.5,0.0,"%"),
    VAL(100,"Engine oil pressure",1,0.0345,0.0,"bar"),
    VAL(102,"Boost pressure",1,0.00862,0.0,"bar (gauge)"),
    RAW(105,"Intake manifold temperature",1),
    RAW(106,"Air inlet pressure",1),
    RAW(110,"Engine coolant temperature",1),
    RAW(111,"Coolant level",1),
    RAW(122,"Engine retarder percentage",1),
    RAW(128,"Component-specific parameter request (control)",2),
    RAW(150,"PTO engagement control status",2),
    RAW(151,"ATC status",2),
    RAW(155,"Auxiliary input/output #1",2),
    RAW(162,"Transmission range selected",2),
    RAW(163,"Transmission range attained",2),
    VAL(168,"Battery voltage",2,0.05,0.0,"V"),
    RAW(171,"Ambient air temperature",2),
    RAW(174,"Fuel temperature",2),
    RAW(175,"Engine oil temperature",2),
    RAW(177,"Transmission oil temperature",2),
    VAL(183,"Instantaneous fuel rate",2,0.059147059125,0.0,"L/h"),
    VAL(184,"Instantaneous fuel economy",2,0.00166072,0.0,"km/L"),
    VAL(185,"Average fuel economy",2,0.00166072,0.0,"km/L"),
    VAL(187,"PTO set speed",2,0.25,0.0,"rpm"),
    VAL(188,"Idle engine speed",2,0.25,0.0,"rpm"),
    VAL(190,"Engine speed",2,0.25,0.0,"rpm"),
    RAW(194,"Diagnostic trouble codes (variable)",0),
    VAL(245,"Total vehicle distance",4,0.1609344,0.0,"km"),
    VAL(247,"Total engine hours",4,0.05,0.0,"h"),
    RAW(249,"Total engine revolutions",4),
    RAW(251,"Clock",0),
    RAW(252,"Date",0),
    VAL(440,"Extended boost pressure #2",2,0.00125,0.0,"bar (gauge)")
};
#undef VAL
#undef RAW
_Static_assert(sizeof(catalog)/sizeof(catalog[0]) == TBX_J1587_PASSIVE_PID_COUNT,
               "passive catalog must match observed field PIDs");

const tbx_j1587_passive_spec_t *tbx_j1587_passive_specs(unsigned *count) {
    if (count) *count = TBX_J1587_PASSIVE_PID_COUNT;
    return catalog;
}

bool tbx_j1587_decode_passive(uint8_t mid, uint16_t pid,
                             const uint8_t *data, uint8_t len,
                             tbx_j1587_passive_reading_t *out) {
    if (!out) return false;
    memset(out,0,sizeof(*out));
    out->mid=mid;
    out->pid=pid;
    out->quality=TBX_PASSIVE_DESCONHECIDO;
    const tbx_j1587_passive_spec_t *spec=NULL;
    for (unsigned i=0;i<TBX_J1587_PASSIVE_PID_COUNT;i++)
        if (catalog[i].pid == pid) {spec=&catalog[i];break;}
    if (!spec) return false;
    out->spec=spec;
    out->quality=TBX_PASSIVE_COMPRIMENTO_INVALIDO;
    if (!data || !len || len>TBX_J1587_MAX_PASSIVE_DATA ||
        (spec->expected_len && len!=spec->expected_len)) return false;
    out->payload_len=len;
    memcpy(out->payload,data,len);
    if (!spec->numeric) {
        out->quality=TBX_PASSIVE_RAW_AGUARDANDO_ESCALA;
        return true;
    }
    if (len>4) return false;
    uint32_t raw=0;
    for (uint8_t i=0;i<len;i++) raw|=(uint32_t)data[i] << (8u*i);
    out->raw_unsigned=raw;
    if ((len==1 && raw==UINT8_MAX) ||
        (len==2 && raw==UINT16_MAX) ||
        (len==4 && raw==UINT32_MAX)) {
        out->quality=TBX_PASSIVE_INDISPONIVEL;
        return true;
    }
    out->converted_value=raw*spec->multiplier+spec->offset;
    out->has_numeric_value=true;
    out->quality=TBX_PASSIVE_EM_VALIDACAO;
    return true;
}
