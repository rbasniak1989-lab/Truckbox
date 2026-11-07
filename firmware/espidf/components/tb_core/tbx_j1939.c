#include "tbx_j1939.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "tbx_j1939_catalog.inc"

void tbx_j1939_init(tbx_j1939_t *decoder)
{
    if (decoder) memset(decoder, 0, sizeof(*decoder));
}
uint32_t tbx_j1939_pgn(uint32_t can_id)
{
    uint32_t pgn = (can_id >> 8) & UINT32_C(0x3ffff);
    if (((can_id >> 16) & 0xffu) < 240u) pgn &= UINT32_C(0x3ff00);
    return pgn;
}
bool tbx_j1939_capture_alias(uint32_t pgn,uint8_t sa)
{
    if(pgn==61456u)return sa<=5u;
    static const struct {uint32_t pgn;uint8_t sa;} known[]={
        {65150,108},{65246,223},{65246,193},{65214,191},{65118,86},
        {65311,2},{65311,6},{65311,12},{65311,15},
        {65343,37},{65343,40},{65343,41},{65343,43},{65343,60},
        {65278,230},{65278,234},{65278,238},{65278,239},{65278,241},{65278,245},{65278,246},{65278,248}};
    for(size_t i=0;i<sizeof(known)/sizeof(known[0]);++i)
        if(known[i].pgn==pgn && known[i].sa==sa)return true;
    return false;
}
const tbx_j1939_signal_t *tbx_j1939_catalog(size_t *count)
{
    if (count) *count = TBX_J1939_SIGNAL_COUNT;
    return signals;
}
const char *tbx_j1939_quality_name(tbx_j1939_quality_t quality)
{
    static const char *const names[] = { "VALIDO", "INDISPONIVEL", "ERRO_SAE", "RESERVADO_SAE",
        "TAMANHO_INVALIDO", "FORA_DA_FAIXA", "SEM_FORMULA_DOCUMENTADA", "SEM_MARCO_INICIAL",
        "REINICIO_CONTADOR", "ORIGEM_NAO_CONFIRMADA", "ESTADO_DESCONHECIDO", "EM_VALIDACAO", "SUSPEITO_PERFIL" };
    return (unsigned)quality < sizeof(names)/sizeof(names[0]) ? names[quality] : "ESTADO_DESCONHECIDO";
}
const char *tbx_j1939_validation_name(tbx_j1939_validation_t validation)
{
    static const char *const names[] = { "VALIDADO_NOS_LOGS", "EM_VALIDACAO", "PENDENTE",
        "INDISPONIVEL", "SEM_DECODIFICACAO" };
    return (unsigned)validation < sizeof(names)/sizeof(names[0]) ? names[validation] : "PENDENTE";
}
/* SAE numeric slots use the most significant octet for NA/error/reserved codes.
 * This intentionally never treats FF/FE as an ordinary scaled measurement. */
static bool numeric(tbx_j1939_observation_t *o, size_t offset, size_t bytes,
                    double factor, double bias, double minimum, double maximum)
{
    if (offset + bytes > o->dlc) { o->quality = TBX_J1939_BAD_LENGTH; return false; }
    uint8_t high = o->raw[offset + bytes - 1u];
    if (high >= 251u) {
        o->quality = high == 255u ? TBX_J1939_NA : high == 254u ? TBX_J1939_ERROR : TBX_J1939_RESERVED;
        return false;
    }
    uint32_t raw = 0;
    for (size_t i=0; i<bytes; ++i) raw |= (uint32_t)o->raw[offset+i] << (8u*i);
    double value = raw * factor + bias;
    if (!isfinite(value) || value < minimum || value > maximum) { o->quality = TBX_J1939_RANGE; return false; }
    o->value = value; o->has_value = true; o->quality = TBX_J1939_OK;
    return true;
}
static void partial(tbx_j1939_observation_t *o)
{
    o->validation=TBX_J1939_IN_VALIDATION;o->raw_only=true;o->quality=TBX_J1939_PARTIAL;
}
static tbx_j1939_component_t *component(tbx_j1939_observation_t *o,const char *name,const char *unit)
{
    if(o->component_count>=TBX_J1939_COMPONENT_MAX)return NULL;
    tbx_j1939_component_t *c=&o->components[o->component_count++];
    c->name=name;c->unit=unit;c->quality=TBX_J1939_NA;return c;
}
static void field(tbx_j1939_observation_t *o,const char *name,const char *unit,
                  unsigned off,unsigned bytes,double factor,double bias,double lo,double hi,bool raw)
{
    tbx_j1939_component_t *c=component(o,name,unit);if(!c)return;
    tbx_j1939_observation_t temp;memset(&temp,0,sizeof(temp));
    temp.dlc=o->dlc;memcpy(temp.raw,o->raw,o->dlc);
    numeric(&temp,off,bytes,factor,bias,lo,hi);
    c->has_value=temp.has_value;c->value=temp.value;
    c->quality=raw && temp.quality==TBX_J1939_OK?TBX_J1939_PARTIAL:temp.quality;
}
static void raw_field(tbx_j1939_observation_t *o,const char *name,unsigned off,unsigned bytes)
{
    partial(o);field(o,name,"raw",off,bytes,1,0,0,65535,true);
    if(o->components[o->component_count-1].quality!=TBX_J1939_PARTIAL)
        o->quality=o->components[o->component_count-1].quality;
}
static void known_code(tbx_j1939_observation_t *o,unsigned off,uint8_t yes,uint8_t no)
{
    if(off>=o->dlc){o->quality=TBX_J1939_BAD_LENGTH;return;}
    uint8_t v=o->raw[off];
    if(v==yes || v==no){o->quality=TBX_J1939_OK;o->has_value=true;o->value=v==yes;}
    else o->quality=v==0xffu?TBX_J1939_NA:TBX_J1939_UNKNOWN_STATE;
}
static const char *const point_rpm[]={"p1_rpm","p2_rpm","p3_rpm","p4_rpm","p5_rpm"};
static const char *const point_pct[]={"p1_pct","p2_pct","p3_pct","p4_pct","p5_pct"};
static void curve(tbx_j1939_observation_t *o,bool engine)
{
    if(o->dlc<(engine?30u:19u)){o->quality=TBX_J1939_BAD_LENGTH;return;}
    static const double engine_rpm[]={600,2110,1200,1500,1800};
    static const double engine_pct[]={52,54,99,94,81};
    o->quality=TBX_J1939_OK;
    for(unsigned i=0;i<5;++i){
        unsigned off=engine?i*3u:2u+i*3u;
        unsigned tq=(!engine && i==4)?18u:off+2u;
        field(o,point_rpm[i],"rpm",off,2,0.125,0,0,8031.875,false);
        field(o,point_pct[i],"%",tq,1,1,-125,-125,125,false);
        for(unsigned k=i*2u;k<i*2u+2u;++k){
            tbx_j1939_component_t *c=&o->components[k];
            if(engine && c->has_value && c->value!=(k%2u?engine_pct[i]:engine_rpm[i])){
                c->quality=TBX_J1939_PROFILE;c->has_value=false;
            }
            if(c->quality!=TBX_J1939_OK)o->quality=c->quality;
        }
    }
}
static bool ascii_status(const tbx_j1939_observation_t *o)
{
    if(o->dlc<8u)return false;
    for(unsigned i=0;i<8u;++i)if(o->raw[i]<32u || o->raw[i]>=127u)return false;
    return o->raw[1]=='A' || o->raw[1]=='M' || o->raw[1]=='N';
}
static void calendar(tbx_j1939_observation_t *o)
{
    if(o->dlc<8u){o->quality=TBX_J1939_BAD_LENGTH;return;}
    static const char *const names[]={"seconds","minutes","hours","month","day","year","local_minute_offset_raw","local_hour_offset_raw"};
    for(unsigned i=0;i<8u;++i){
        double factor=i==0?0.25:i==4?0.25:1;
        field(o,names[i],i==0?"s":i>=6?"raw":"calendar",i,1,factor,i==5?1985:0,0,i==0?59.75:i==1?59:i==2?23:i==3?12:i==4?31:i==5?2235:250,i>=6);
    }
    unsigned year=1985u+o->raw[5],month=o->raw[3],day=(o->raw[4]+3u)/4u;
    if(o->components[4].has_value)o->components[4].value=day;
    for(unsigned i=0;i<6u;++i)if(!o->components[i].has_value){o->quality=o->components[i].quality;return;}
    static const unsigned days[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned maxday=month>=1u && month<=12u?days[month]:0;
    if(month==2u && year%4u==0u && (year%100u!=0u || year%400u==0u))++maxday;
    if(day<1u || day>maxday){o->quality=TBX_J1939_RANGE;return;}
    char stamp[40];
    int stamp_size=snprintf(stamp,sizeof(stamp),"%04u-%02u-%02uT%02u:%02u:%02u.%03u",year,month,day,o->raw[2],o->raw[1],o->raw[0]/4u,(o->raw[0]%4u)*250u);
    if(stamp_size!=23){o->quality=TBX_J1939_RANGE;return;}
    memcpy(o->text,stamp,24);
    /* Calendar is local network time. Offsets stay raw; never assert UTC/fix. */
    o->quality=TBX_J1939_OK;
}
static void bogie(tbx_j1939_t *s,tbx_j1939_observation_t *o)
{
    if(o->dlc<3u){o->quality=TBX_J1939_BAD_LENGTH;return;}
    if(o->raw[0]!=0x1fu && o->raw[0]!=0x2fu){o->quality=TBX_J1939_WRONG_SOURCE;return;}
    if(!numeric(o,1,2,0.5,0,0,32127.5))return;
    size_t slot=8,oldest=0;
    for(size_t i=0;i<8u;++i){
        if(s->weights[i].used && s->weights[i].sa==o->sa){slot=i;break;}
        if(!s->weights[i].used){slot=i;break;}
        if(s->weights[i].at_ms[0]<s->weights[oldest].at_ms[0])oldest=i;
    }
    if(slot==8)slot=oldest;
    if(!s->weights[slot].used || s->weights[slot].sa!=o->sa){memset(&s->weights[slot],0,sizeof(s->weights[slot]));s->weights[slot].used=true;s->weights[slot].sa=o->sa;}
    unsigned channel=o->raw[0]==0x1fu?0u:1u,other=1u-channel;
    s->weights[slot].kg[channel]=o->value;s->weights[slot].valid[channel]=true;s->weights[slot].at_ms[channel]=o->captured_ms;
    if(s->weights[slot].valid[other] && o->captured_ms>=s->weights[slot].at_ms[other] && o->captured_ms-s->weights[slot].at_ms[other]<=3000u)
        o->value=(o->value+s->weights[slot].kg[other])*0.5;
}
static void decode_one(tbx_j1939_t *s, tbx_j1939_observation_t *o)
{
    const unsigned id=o->signal->id;
    switch(id) {
    case 1: numeric(o,3,2,0.125,0,0,8031.875); break;
    case 2: numeric(o,2,1,1,-125,-125,125); break;
    case 3: {
        tbx_j1939_observation_t rpm=*o, torque=*o;
        if (!numeric(&rpm,3,2,0.125,0,0,8031.875)) { o->quality=rpm.quality; break; }
        if (!numeric(&torque,2,1,1,-125,-125,125)) { o->quality=torque.quality; break; }
        /* Explicit calibration from inventory, not presumed engine nameplate hp.
         * The archive does not specify the 65251 byte mapping for a live reference. */
        o->reference_torque_nm=2643.0;
        o->value=fmax(torque.value,0.0)*0.01*o->reference_torque_nm*rpm.value/9550.0;
        o->has_value=true; o->quality=TBX_J1939_OK;
        tbx_j1939_component_t *nm=component(o,"actual_torque_nm","Nm");
        nm->has_value=true;nm->value=fmax(torque.value,0.0)*0.01*o->reference_torque_nm;nm->quality=TBX_J1939_OK;
        tbx_j1939_component_t *cv=component(o,"power_cv","cv");
        cv->has_value=true;cv->value=o->value*1.35962;cv->quality=TBX_J1939_OK;
        tbx_j1939_component_t *fuel=component(o,"fuel_rate_estimated_lph","L/h");
        fuel->has_value=true;fuel->value=o->value*0.25*0.9720031611625598;fuel->quality=TBX_J1939_PARTIAL;
        break;
    }
    case 6: numeric(o,1,1,0.4,0,0,100); break;
    case 7:
        raw_field(o,"switches_low_nibble_raw",0,1);
        if(o->component_count && o->components[0].has_value)o->components[0].value=(unsigned)o->components[0].value&15u;
        break;
    case 8: numeric(o,0,1,1,-125,-125,125);break;
    case 9:
        if(numeric(o,19,2,1,0,0,64255) && o->value!=2643){o->has_value=false;o->quality=TBX_J1939_PROFILE;}
        break;
    case 10: curve(o,true);break;
    case 11:
        numeric(o,15,2,0.125,0,0,8031.875);
        field(o,"high_idle_rpm","rpm",15,2,0.125,0,0,8031.875,false);
        field(o,"extended_speed_upper_rpm","rpm",28,2,0.125,0,0,8031.875,false);break;
    case 12:
        o->quality=TBX_J1939_OK;
        field(o,"torque_control_min_pct","%",26,1,1,-125,-125,125,false);
        field(o,"torque_control_max_pct","%",27,1,1,-125,-125,125,false);
        for(unsigned i=0;i<o->component_count;++i)if(o->components[i].quality!=TBX_J1939_OK)o->quality=o->components[i].quality;
        break;
    case 13:
        raw_field(o,"engine_gain_kp_raw",21,2);raw_field(o,"requested_speed_lower_raw",24,1);raw_field(o,"requested_speed_upper_raw",25,1);break;
    case 15:raw_field(o,"parking_state_b0_raw",0,1);break;
    case 16:numeric(o,5,1,1,0,0,250);break;
    case 14: numeric(o,1,2,1.0/256.0,0,0,250.99609375); break;
    case 17: numeric(o,0,4,0.005,0,0,21000000); break;
    case 18: {
        if (!numeric(o,0,4,0.005,0,0,21000000)) break;
        uint32_t raw=0;
        for(unsigned i=0;i<4;++i) raw|=(uint32_t)o->raw[i]<<(8u*i);
        const unsigned sa=o->sa;
        const bool backwards=s->baseline[sa] && raw<s->last_counter[sa];
        if (!s->baseline[sa] || o->captured_ms < s->counter_at_ms[sa] ||
            o->captured_ms-s->counter_at_ms[sa]>30000u) {
            o->has_value=false; o->quality=TBX_J1939_BASELINE;
        } else if(backwards) {
            /* A negative delta is not a negative trip and not an assumed rollover. */
            o->has_value=false; o->quality=TBX_J1939_COUNTER_RESET;
        } else {
            double delta=(raw-s->last_counter[sa])*0.005;
            uint64_t elapsed=o->captured_ms-s->counter_at_ms[sa];
            double plausible=250.0*(double)elapsed/3600000.0+0.05;
            if(delta>plausible) { o->has_value=false; o->quality=TBX_J1939_RANGE; }
            else { s->session_distance_km[sa]+=delta; o->value=s->session_distance_km[sa]; }
        }
        s->baseline[sa]=true; s->last_counter[sa]=raw; s->counter_at_ms[sa]=o->captured_ms;
        break;
    }
    case 19:bogie(s,o);break;
    case 20:
        if(o->dlc<3u){o->quality=TBX_J1939_BAD_LENGTH;break;}
        if(o->raw[0]!=0x1fu && o->raw[0]!=0x2fu){o->quality=TBX_J1939_WRONG_SOURCE;break;}
        numeric(o,1,2,0.5,0,0,32127.5);break;
    case 21: numeric(o,5,1,1000,0,10000,100000); break;
    case 22:raw_field(o,"proprietary_speed_state_raw",0,1);break;
    case 23:known_code(o,2,0xc1,0xc0);break;
    case 24: numeric(o,0,1,1,-40,-50,220); break;
    case 25: numeric(o,2,2,0.03125,-273,-50,220); break;
    case 26:numeric(o,1,1,1,-40,-50,220);o->validation=TBX_J1939_IN_VALIDATION;break;
    case 27: numeric(o,4,2,0.03125,-273,-40,200); break;
    case 28: numeric(o,3,2,0.03125,-273,-50,220); break;
    case 29: numeric(o,1,1,0.02,0,0,5); break;
    case 30: numeric(o,2,1,1,-40,-50,220); break;
    case 31:numeric(o,3,1,2,0,0,500);break;
    case 32:numeric(o,4,1,0.05,0,0,12.5);break;
    case 33: numeric(o,0,1,0.04,0,0,10); break;
    case 34: numeric(o,3,1,0.04,0,0,10); break;
    case 35: numeric(o,2,1,0.4,0,0,100); break;
    case 36:numeric(o,4,2,1.0/128.0,-250,-250,252);break;
    case 37:numeric(o,7,1,0.4,0,0,100);o->validation=TBX_J1939_IN_VALIDATION;break;
    case 38:case 39:
        if(o->dlc<=(id==38?6u:1u))o->quality=TBX_J1939_BAD_LENGTH;
        else o->quality=o->raw[id==38?6u:1u]==0xffu?TBX_J1939_NA:TBX_J1939_NO_FORMULA;
        break;
    case 40: numeric(o,5,2,0.125,0,0,3000); break;
    case 41: numeric(o,1,2,0.125,0,0,3000); break;
    case 42: numeric(o,3,1,0.4,0,0,100); break;
    case 43:raw_field(o,"etc1_b0_raw",0,1);break;
    case 44: numeric(o,0,1,1,-125,-125,125); break;
    case 45: numeric(o,3,1,1,-125,-125,125); break;
    case 46: numeric(o,1,2,0.001,0,0,64.255); break;
    case 47:
        if(o->dlc<1u) o->quality=TBX_J1939_BAD_LENGTH;
        else if(o->raw[0]==0xf2u || o->raw[0]==0xf3u || o->raw[0]==0xf4u) {
            o->text[0]=o->raw[0]==0xf2u?'N':o->raw[0]==0xf3u?'A':'M'; o->text[1]='\0';
            o->quality=TBX_J1939_OK;
        } else o->quality=o->raw[0]==0xffu?TBX_J1939_NA:TBX_J1939_UNKNOWN_STATE;
        break;
    case 48:case 49:case 71:
        partial(o);snprintf(o->text,sizeof(o->text),"PAYLOAD_SEM_MAPA");break;
    case 50:
        if(o->dlc<8u)o->quality=TBX_J1939_BAD_LENGTH;
        else if(ascii_status(o)){o->has_value=true;o->value=memcmp(o->raw," AN E+00",8)==0;o->quality=TBX_J1939_OK;}
        else o->quality=o->raw[0]==0xffu?TBX_J1939_NA:TBX_J1939_UNKNOWN_STATE;
        break;
    case 51:
        if(o->dlc<8u)o->quality=TBX_J1939_BAD_LENGTH;
        else if(ascii_status(o)){o->has_value=true;o->value=o->raw[4]=='E';o->quality=TBX_J1939_OK;}
        else o->quality=TBX_J1939_UNKNOWN_STATE;
        break;
    case 52:
        if(o->dlc<8u)o->quality=TBX_J1939_BAD_LENGTH;
        else if(ascii_status(o)){
            o->text[0]=(char)o->raw[2];o->text[1]=(char)o->raw[3];o->text[2]=0;
            if(o->text[1]==' ')o->text[1]=0;
            if(o->raw[2]>='0' && o->raw[2]<='9' && o->raw[3]>='0' && o->raw[3]<='9'){
                o->has_value=true;o->value=(o->raw[2]-'0')*10+o->raw[3]-'0';
            }
            o->quality=TBX_J1939_OK;
        }else o->quality=TBX_J1939_UNKNOWN_STATE;
        break;
    case 53:
        if(o->dlc<8u)o->quality=TBX_J1939_BAD_LENGTH;
        else if(ascii_status(o)){o->text[0]=(char)o->raw[1];o->text[1]=0;o->quality=TBX_J1939_OK;}
        else o->quality=TBX_J1939_UNKNOWN_STATE;
        break;
    case 54:numeric(o,1,1,1,-125,-125,125);break;
    case 55:curve(o,false);break;
    case 56:raw_field(o,"reference_retarder_torque_raw",16,2);break;
    case 57:
        if(o->dlc<2u)o->quality=TBX_J1939_BAD_LENGTH;
        else {unsigned v=(o->raw[1]>>5)&7u;
            if(v>=5u)o->quality=v==7u?TBX_J1939_NA:v==6u?TBX_J1939_ERROR:TBX_J1939_RESERVED;
            else {static const char *const stages[]={"OFF","AUTO","ESTAGIO_1","ESTAGIO_2","ESTAGIO_3"};
                o->has_value=true;o->value=v;o->quality=TBX_J1939_OK;snprintf(o->text,sizeof(o->text),"%s",stages[v]);}
        }break;
    case 58:known_code(o,2,0xdf,0xcf);break;
    case 59:
        known_code(o,0,0xdf,0xc7);
        if(o->dlc && o->raw[0]==0xd7u){o->quality=TBX_J1939_PARTIAL;snprintf(o->text,sizeof(o->text),"TRANSICAO");}
        break;
    case 60:
        if(o->dlc<4u)o->quality=TBX_J1939_BAD_LENGTH;
        else if(!(o->raw[1]&1u) || o->raw[1]==0xffu)o->quality=TBX_J1939_NA;
        else numeric(o,3,1,1,0,0,250);
        break;
    case 61:known_code(o,4,0xf7,0xf3);break;
    case 62:case 63:
        if(o->dlc<7u)o->quality=TBX_J1939_BAD_LENGTH;
        else if(o->raw[6]==0xc1u || o->raw[6]==0xc5u || o->raw[6]==0xd1u){
            o->has_value=true;o->value=o->raw[6]==(id==62?0xd1u:0xc5u);o->quality=TBX_J1939_OK;
        }else o->quality=o->raw[6]==0xffu?TBX_J1939_NA:TBX_J1939_UNKNOWN_STATE;
        break;
    case 64:raw_field(o,"ebc1_b0_raw",0,1);break;
    case 65:raw_field(o,"ebc1_b5_raw",5,1);break;
    case 66:
        if(o->dlc<1u)o->quality=TBX_J1939_BAD_LENGTH;
        else {unsigned v=(o->raw[0]>>6)&3u;
            if(v<2u){o->has_value=true;o->value=v;o->quality=TBX_J1939_OK;}
            else o->quality=v==2u?TBX_J1939_ERROR:TBX_J1939_NA;
        }break;
    case 67:
        raw_field(o,"driver1_working_state_raw",0,1);
        if(o->component_count && o->components[0].has_value)o->components[0].value=(unsigned)o->components[0].value&7u;
        break;
    case 68:numeric(o,4,2,0.125,0,0,8031.875);break;
    case 69:numeric(o,6,2,1.0/256.0,0,0,250.99609375);break;
    case 70:calendar(o);break;
    case 72:case 73:
        raw_field(o,"lighting_b1_raw",1,1);
        if(o->dlc>1u){
            if(o->raw[1]==(id==72?0xd0u:0xd2u))snprintf(o->text,sizeof(o->text),"CODIGO_%s_PROVAVEL",id==72?"D0":"D2");
            else if(o->raw[1]==0xc0u || o->raw[1]==0xd0u || o->raw[1]==0xd2u || o->raw[1]==0xd1u)
                snprintf(o->text,sizeof(o->text),"OUTRO_ESTADO_PARCIAL");
        }break;
    case 74:raw_field(o,"lighting_aux_b0_raw",0,1);break;
    case 75:
        o->quality=TBX_J1939_NA;
        for(unsigned i=0;i<o->dlc;++i)if(o->raw[i]<0xfeu)o->quality=TBX_J1939_NO_FORMULA;
        break;
    default: /* Evidence is absent. Catalog provenance must not create a value. */ break;
    }
}
static size_t decode_message(tbx_j1939_t *decoder, uint32_t can_id, bool extended,
                       bool rtr, const uint8_t *data, size_t length,
                       uint64_t captured_ms, tbx_j1939_emit_fn emit, void *context)
{
    if(!decoder || !emit || !extended || rtr || can_id>UINT32_C(0x1fffffff) ||
       length>TBX_J1939_PAYLOAD_MAX || (length && !data)) return 0;
    const uint32_t pgn=tbx_j1939_pgn(can_id);
    size_t count=0;
    for(size_t i=0;i<TBX_J1939_SIGNAL_COUNT;++i) {
        if(signals[i].pgn!=pgn) continue;
        tbx_j1939_observation_t o;
        memset(&o,0,sizeof(o));
        o.signal=&signals[i]; o.can_id=can_id; o.pgn=pgn;
        o.sa=(uint8_t)can_id; o.pf=(uint8_t)(can_id>>16); o.ps=(uint8_t)(can_id>>8);
        o.dlc=(uint8_t)length; o.captured_ms=captured_ms; o.quality=TBX_J1939_NO_FORMULA;
        o.validation=signals[i].validation;
        if(length) memcpy(o.raw,data,length);
        decode_one(decoder,&o);
        emit(&o,context); ++count;
    }
    return count;
}

size_t tbx_j1939_decode(tbx_j1939_t *decoder,uint32_t can_id,bool extended,
                       bool rtr,const uint8_t *data,size_t length,uint64_t captured_ms,
                       tbx_j1939_emit_fn emit,void *context)
{
    if(length>8u)return 0;
    return decode_message(decoder,can_id,extended,rtr,data,length,captured_ms,emit,context);
}
size_t tbx_j1939_decode_payload(tbx_j1939_t *decoder,uint32_t pgn,uint8_t sa,
                       const uint8_t *data,size_t length,uint64_t captured_ms,
                       tbx_j1939_emit_fn emit,void *context)
{
    if((pgn!=65251u && pgn!=65249u) || length>TBX_J1939_PAYLOAD_MAX)return 0;
    return decode_message(decoder,UINT32_C(0x18000000)|(pgn<<8)|sa,true,false,
                          data,length,captured_ms,emit,context);
}
