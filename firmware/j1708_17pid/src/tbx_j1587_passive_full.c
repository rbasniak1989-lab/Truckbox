#include "tbx_j1587_passive_full.h"
#include <string.h>

static void scalar(tbx_j1587_full_reading_t *o, double value,
                   const char *unit) {
    o->kind=TBX_J1587_KIND_SCALAR;
    o->numeric_value=value;
    o->unit=unit;
    o->base.has_numeric_value=true;
    o->base.converted_value=value;
}

static void flag(tbx_j1587_full_reading_t *o, const char *label,
                 uint8_t value, unsigned bit) {
    if (o->field_count>=TBX_J1587_MAX_FIELDS) return;
    tbx_j1587_state_field_t *f=&o->fields[o->field_count++];
    f->name=label; f->width=1;
    f->state=(uint8_t)((value>>bit)&1u);
    o->kind=TBX_J1587_KIND_STATES;
}
static void pair(tbx_j1587_full_reading_t *o, const char *label,
                 uint8_t value, unsigned shift) {
    if (o->field_count>=TBX_J1587_MAX_FIELDS) return;
    tbx_j1587_state_field_t *f=&o->fields[o->field_count++];
    f->name=label; f->width=2;
    f->state=(uint8_t)((value>>shift)&3u);
    o->kind=TBX_J1587_KIND_STATES;
}
static uint16_t le16(const uint8_t *d) {
    return (uint16_t)((uint16_t)d[0] | (uint16_t)d[1]<<8);
}
static uint32_t le32(const uint8_t *d) {
    return (uint32_t)d[0] | (uint32_t)d[1]<<8 |
           (uint32_t)d[2]<<16 | (uint32_t)d[3]<<24;
}
/* Fahrenheit resolution 1 deg, 1/4 deg, respectively, in SAE J1587. */
static double fahrenheit_to_celsius(double fahrenheit) {
    return (fahrenheit-32.0)*5.0/9.0;
}
static bool is_printable(uint8_t ch) {
    return ch>=32u && ch<=126u;
}
static bool parse_dtcs(const uint8_t *bytes, uint8_t count,
                       tbx_j1587_full_reading_t *o) {
    o->kind=TBX_J1587_KIND_DTCS;
    o->manufacturer_detail_required=true;
    for (uint8_t i=0;i<count;) {
        if ((uint8_t)(count-i)<2u || o->dtc_count>=TBX_J1587_MAX_DTCS)
            return false;
        uint8_t identifier=bytes[i++];
        uint8_t flags=bytes[i++];
        tbx_j1587_dtc_t *dtc=&o->dtcs[o->dtc_count++];
        dtc->has_occurrences=(flags & 0x80u)!=0;
        dtc->active=(flags & 0x40u)==0;
        dtc->page2=(flags & 0x20u)==0;
        dtc->sid=!dtc->page2 && (flags & 0x10u)!=0;
        dtc->identifier=(uint16_t)(identifier+(dtc->page2?256u:0u));
        dtc->fmi=(uint8_t)(flags&0x0fu);
        if (dtc->has_occurrences) {
            if (i>=count) return false;
            dtc->occurrences=bytes[i++];
        }
    }
    return true;
}
bool tbx_j1587_decode_passive_full(uint8_t mid, uint16_t pid,
                                  const uint8_t *data, uint8_t length,
                                  tbx_j1587_full_reading_t *out) {
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (pid==194u && length==0u) {
        /* J1587 PID194 zero-length value: no diagnostic entries. */
        unsigned n=0;
        const tbx_j1587_passive_spec_t *spec=tbx_j1587_passive_specs(&n);
        for(unsigned i=0;i<n;i++) if(spec[i].pid==194u) {
            out->base.spec=&spec[i]; break;
        }
        if (!out->base.spec) return false;
        out->base.pid=pid; out->base.mid=mid;
        out->base.quality=TBX_PASSIVE_EM_VALIDACAO;
        out->kind=TBX_J1587_KIND_DTCS;
        out->interpreted=true;
        return true;
    }
    if (!tbx_j1587_decode_passive(mid,pid,data,length,&out->base))
        return false;
    /* 17 scalar PIDs are already decoded by the established passive
       decoder; this layer completes the remaining 31 only. */
    if (out->base.has_numeric_value) {
        scalar(out,out->base.converted_value,out->base.spec->unit);
        out->interpreted=true;
        return true;
    }
    if (out->base.quality==TBX_PASSIVE_INDISPONIVEL) return true;
    uint8_t a=data[0];
    uint8_t b=length>1u?data[1]:0;
    switch(pid) {
    case 36: scalar(out,(double)a*0.4,"% wear"); break;
    case 46: scalar(out,(double)a*0.0689,"bar"); break;
    case 86: scalar(out,(double)a*0.804672,"km/h"); break;
    case 105: case 110:
        scalar(out,fahrenheit_to_celsius((double)a),"C"); break;
    case 106: scalar(out,(double)a*1.724,"kPa absolute"); break;
    case 111: case 122: scalar(out,(double)a*0.5,"%"); break;
    case 171: case 174: case 175: case 177:
        scalar(out,fahrenheit_to_celsius((double)(int16_t)le16(data)*0.25),"C");
        break;
    case 249: scalar(out,(double)le32(data)*1000.0,"revolutions"); break;
    case 43:
        pair(out,"ignition accessory",a,0);
        pair(out,"ignition run",a,2);
        pair(out,"ignition crank",a,4);
        pair(out,"ignition start-aid",a,6);
        break;
    case 44:
        pair(out,"red warning lamp",a,0);
        pair(out,"amber warning lamp",a,2);
        pair(out,"protect lamp",a,4);
        break;
    case 49:
        pair(out,"ABS warning lamp",a,0);
        pair(out,"ABS brake control",a,2);
        pair(out,"ABS retarder control",a,4);
        pair(out,"ABS off-road switch",a,6);
        break;
    case 70: flag(out,"parking brake switch active",a,7); break;
    case 71:
        flag(out,"idle timer driver alert",a,0);
        flag(out,"idle timer stopped engine",a,1);
        flag(out,"idle timer override",a,2);
        flag(out,"idle timer enabled in ECU",a,3);
        flag(out,"idle timer active",a,7);
        break;
    case 83: flag(out,"road speed limiting active",a,7); break;
    case 85:
        flag(out,"cruise switch",a,0);
        flag(out,"cruise set",a,1);
        flag(out,"cruise coast",a,2);
        flag(out,"cruise resume",a,3);
        flag(out,"cruise accel",a,4);
        flag(out,"cruise brake",a,5);
        flag(out,"cruise clutch",a,6);
        flag(out,"cruise active",a,7);
        break;
    case 89:
        flag(out,"PTO switch",a,0);
        flag(out,"PTO set",a,1);
        flag(out,"PTO coast",a,2);
        flag(out,"PTO resume",a,3);
        flag(out,"PTO accel",a,4);
        flag(out,"PTO brake",a,5);
        flag(out,"PTO clutch",a,6);
        flag(out,"PTO active",a,7);
        break;
    case 97: flag(out,"water in fuel indicated",a,7); break;
    case 128:
        out->kind=TBX_J1587_KIND_REQUEST;
        out->requested_pid=a;
        out->target_mid=b;
        break;
    case 150:
        pair(out,"PTO input switch 1",a,0);
        pair(out,"PTO input switch 2",a,2);
        pair(out,"PTO output actuator 1",b,0);
        pair(out,"PTO output actuator 2",b,2);
        break;
    case 151:
        pair(out,"ATC status lamp",a,0);
        pair(out,"ATC brake control",a,2);
        pair(out,"ATC engine control",a,4);
        pair(out,"ATC spin-out detection",a,6);
        pair(out,"ATC deep snow/mud switch",b,0);
        break;
    case 155:
        pair(out,"auxiliary input 1",a,0);
        pair(out,"auxiliary input 2",a,2);
        pair(out,"auxiliary input 3",a,4);
        pair(out,"auxiliary input 4",a,6);
        pair(out,"auxiliary output 1",b,0);
        pair(out,"auxiliary output 2",b,2);
        pair(out,"auxiliary output 3",b,4);
        pair(out,"auxiliary output 4",b,6);
        out->manufacturer_detail_required=true;
        break;
    case 162: case 163:
        out->kind=TBX_J1587_KIND_TEXT;
        if (is_printable(a) && is_printable(b)) {
            if (a==' ') {
                out->text[0]=(char)b;
            } else {
                out->text[0]=(char)a;
                out->text[1]=(char)b;
            }
        } else if(a<32u && is_printable(b)) {
            out->text[0]=(char)b;
            out->manufacturer_detail_required=true;
        } else {
            /* Do not falsely display a control code as a gear. */
            out->manufacturer_detail_required=true;
            out->interpreted=false;
            return true;
        }
        break;
    case 194:
        if (!parse_dtcs(data,length,out)) {
            out->base.quality=TBX_PASSIVE_COMPRIMENTO_INVALIDO;
            return false;
        }
        break;
    case 251:
        out->kind=TBX_J1587_KIND_CLOCK;
        out->second=(double)a*0.25;
        out->minute=b; out->hour=data[2];
        if (out->second>=60.0 || out->minute>59u || out->hour>23u) {
            out->base.quality=TBX_PASSIVE_COMPRIMENTO_INVALIDO;
            return false;
        }
        break;
    case 252:
        out->kind=TBX_J1587_KIND_DATE;
        out->day=(uint8_t)(a/4u);
        out->month=b;
        out->year=(uint16_t)(1985u+data[2]);
        if (!out->day || out->day>31u || !out->month || out->month>12u) {
            out->base.quality=TBX_PASSIVE_COMPRIMENTO_INVALIDO;
            return false;
        }
        break;
    default:
        /* The base decoder has recognized this PID, but this helper is
           intentionally only responsible for the formerly raw 31. */
        return false;
    }
    out->base.quality=TBX_PASSIVE_EM_VALIDACAO;
    out->interpreted=true;
    return true;
}
