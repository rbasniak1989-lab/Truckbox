#include "store_codec.h"
#include <stdint.h>
#include <string.h>

/* Fixed bounded workspace, no dictionary persisted across records or boots.
 * 12-bit distance, 4-bit length minus3, groups of8 with literal flag bits.
 * Header: original length little endian. Each record is independently decoded. */
#define CODEC_MAX 768u
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
/* Immutable dictionary for local format2. These are syntax/catalog strings,
 * never readings. A change to this dictionary requires a NEW format version. */
static const char seed[]=
    "{\"sample_kind\":\"OPERATIONAL\",\"uptime_ms\":,\"boot_id\":,\"schema_version\":2,"
    "\"time_valid\":false,\"time_source\":\"boot_monotonic\",\"ts\":\"2026-\","
    "\"gps\":null,\"gps_status\":\"NO_FIX\",\"has_fix\":false,\"source\":\"j1708\",\"origin\":\"PASSIVE_RX\","
    "\"speed_kmh\":,\"rpm\":,\"fuel_rate_lph\":,\"odometer_km\":,\"trip_distance_km\":,\"trip_fuel_l\":,"
    "\"speed_source\":\"j1587\",\"rpm_source\":\"j1587\",\"fuel_source\":\"j1587\","
    "\"j1587_sensors\":{},\"j1587_raw\":{},\"j1587_units\":{},\"quality\":\"EM_VALIDACAO\","
    "\"sample_kind\":\"J1708_FAST\",\"sample_kind\":\"HEALTH\",\"metrics\":{},\"context\":{},"
    "\"sample_kind\":\"J1708_REQUEST_EVENT\",\"event\":\"REQUEST\",\"event\":\"RESPONSE_VALUE\","
    "\"event\":\"TIMEOUT\",\"detail\":\"PID128_TARGETED_REQUEST_QUEUED\","
    "\"local_mid\":,\"target_mid\":,\"tx_frame\":\"\",\"parameter_hex\":\"\",\"pid\":,\"mid\":,\"value\":,\"unit\":,"
    "\"classification\":\"RESPONSE_CANDIDATE\",\"validation\":\"EM_VALIDACAO\",\"time_valid\":true,\"time_source\":\"system_clock\","
    "\"degC\",\"kPa\",\"bar\",\"rpm\",\"km/h\",\"V\",\"L/h\",\"km\",\"L\",\"h\",\"raw\","
    "\"window_seconds\":60,\"window_start_uptime_ms\":,\"window_end_uptime_ms\":,\"j1708_frames\":,"
    "\"j1708_checksum_errors\":0,\"uart_overruns\":0,\"can_frames\":0,\"telemetry_profile\":\"LEGACY\","
    "\"j1708_framing\":\"uart_idle_10bits\",\"firmware\":\"0.2.12\",\"role\":\"core\"";
/* Fixed key tokens are part of format2, alongside the seed above. */
static const char *const keys[]={
    "\"sample_kind\":",
    "\"uptime_ms\":",
    "\"boot_id\":",
    "\"schema_version\":",
    "\"time_valid\":",
    "\"time_source\":",
    "\"gps_status\":",
    "\"has_fix\":",
    "\"speed_kmh\":",
    "\"rpm\":",
    "\"fuel_rate_lph\":",
    "\"odometer_km\":",
    "\"trip_distance_km\":",
    "\"trip_fuel_l\":",
    "\"speed_source\":",
    "\"rpm_source\":",
    "\"fuel_source\":",
    "\"j1587_sensors\":",
    "\"j1587_raw\":",
    "\"j1587_units\":",
    "\"quality\":",
    "\"j1587_quality\":",
    "\"metrics\":",
    "\"context\":",
    "\"event\":",
    "\"detail\":",
    "\"local_mid\":",
    "\"target_mid\":",
    "\"tx_frame\":",
    "\"parameter_hex\":",
    "\"pid\":",
    "\"mid\":",
    "\"value\":",
    "\"unit\":",
    "\"classification\":",
    "\"validation\":",
    "\"source\":",
    "\"origin\":",
    "\"interval_s\":",
    "\"part\":",
    "\"window_seconds\":",
    "\"window_start_uptime_ms\":",
    "\"window_end_uptime_ms\":",
    "\"j1708_frames\":",
    "\"j1708_checksum_errors\":",
    "\"uart_overruns\":",
    "\"can_frames\":",
    "\"telemetry_profile\":",
    "\"j1708_framing\":",
    "\"firmware\":",
    "\"role\":",
    "\"latency_ms\":",
    "\"raw\":",
    "\"unavailable\":",
    "\"trip_accounting\":",
    "\"integrated_l\":",
    "\"estimated_l\":",
    "\"direct_l\":",
    "\"gap_count\":",
    "\"unknown_gap\":",
    "\"distance_quality\":",
    "\"fuel_quality\":",
    "\"ts\":",
    "\"gps\":",
};
static char history(const char *input,size_t index)
{ return index<sizeof(seed)-1?seed[index]:input[index-(sizeof(seed)-1)]; }
static int digit(unsigned char c)
{
    const char *p=c?strchr(alphabet,c):NULL;
    return p?(int)(p-alphabet):-1;
}
size_t tb_store_pack(const char *json,size_t n,char *out,size_t cap)
{
    uint8_t bytes[CODEC_MAX];char tokenized[CODEC_MAX];
    if(!json || !out || n<48 || n>=CODEC_MAX || cap<12)return 0;
    size_t original=n,converted=0;
    for(size_t i=0;i<n;){
        size_t k=0;
        for(;k<sizeof(keys)/sizeof(keys[0]);++k){size_t z=strlen(keys[k]);if(z<=n-i && !memcmp(json+i,keys[k],z)){if(converted>=sizeof(tokenized))return 0;tokenized[converted++]=(char)(128u+k);i+=z;break;}}
        if(k==sizeof(keys)/sizeof(keys[0])){
            unsigned char c=(unsigned char)json[i++];
            if(converted+(c>=128?2u:1u)>sizeof(tokenized))return 0;
            if(c>=128)tokenized[converted++]=(char)192;
            tokenized[converted++]=(char)c;
        }
    }
    json=tokenized;n=converted;
    size_t used=2,pos=0;bytes[0]=(uint8_t)n;bytes[1]=(uint8_t)(n>>8);
    while(pos<n){
        if(used+1>=sizeof(bytes))return 0;
        size_t flag=used++;bytes[flag]=0;
        for(unsigned bit=0;bit<8 && pos<n;bit++){
            size_t best=0,distance=0;
            for(size_t d=1;d<=sizeof(seed)-1+pos && d<=4096;d++){
                size_t count=0;
                while(count<18 && pos+count<n && json[pos+count]==history(json,sizeof(seed)-1+pos+count-d))++count;
                if(count>best && count>=3){best=count;distance=d;}
            }
            if(best>=3){
                if(used+2>sizeof(bytes))return 0;
                unsigned code=(unsigned)((distance-1)<<4)|(unsigned)(best-3);
                bytes[used++]=(uint8_t)code;bytes[used++]=(uint8_t)(code>>8);pos+=best;
            }else{
                if(used>=sizeof(bytes))return 0;
                bytes[flag]|=(uint8_t)(1u<<bit);bytes[used++]=(uint8_t)json[pos++];
            }
        }
    }
    size_t needed=4*((used+2)/3);
    if(needed+1>cap || needed+24>=original)return 0;
    size_t target=0;
    for(size_t i=0;i<used;i+=3){
        uint32_t v=(uint32_t)bytes[i]<<16;
        if(i+1<used)v|=(uint32_t)bytes[i+1]<<8;
        if(i+2<used)v|=bytes[i+2];
        out[target++]=alphabet[(v>>18)&63];out[target++]=alphabet[(v>>12)&63];
        out[target++]=i+1<used?alphabet[(v>>6)&63]:'=';
        out[target++]=i+2<used?alphabet[v&63]:'=';
    }
    out[target]=0;return target;
}
size_t tb_store_unpack(const char *packed,char *out,size_t cap)
{
    uint8_t bytes[CODEC_MAX];char decoded[CODEC_MAX];
    if(!packed || !out || cap<2)return 0;
    size_t n=strlen(packed),used=0;
    if(!n || n%4 || n>4*((sizeof(bytes)+2)/3))return 0;
    for(size_t i=0;i<n;i+=4){
        int a=digit((unsigned char)packed[i]),b=digit((unsigned char)packed[i+1]);
        int c=packed[i+2]=='='?0:digit((unsigned char)packed[i+2]);
        int d=packed[i+3]=='='?0:digit((unsigned char)packed[i+3]);
        if(a<0 || b<0 || c<0 || d<0 ||
           (packed[i+2]=='=' && packed[i+3]!='=') ||
           ((packed[i+2]=='=' || packed[i+3]=='=') && i+4!=n) ||
           (packed[i+2]=='=' && (b&15)) || (packed[i+3]=='=' && (c&3)))return 0;
        uint32_t v=(uint32_t)a<<18|(uint32_t)b<<12|(uint32_t)c<<6|(uint32_t)d;
        if(used>=sizeof(bytes))return 0;
        bytes[used++]=(uint8_t)(v>>16);
        if(packed[i+2]!='='){if(used>=sizeof(bytes))return 0;bytes[used++]=(uint8_t)(v>>8);}
        if(packed[i+3]!='='){if(used>=sizeof(bytes))return 0;bytes[used++]=(uint8_t)v;}
    }
    if(used<3)return 0;
    size_t expected=(size_t)bytes[0]|(size_t)bytes[1]<<8,pos=2,target=0;
    if(!expected || expected>CODEC_MAX)return 0;
    while(target<expected){
        if(pos>=used)return 0;
        unsigned flags=bytes[pos++];
        for(unsigned bit=0;bit<8 && target<expected;bit++){
            if(flags&(1u<<bit)){
                if(pos>=used)return 0;
                decoded[target++]=(char)bytes[pos++];
            }else{
                if(pos+2>used)return 0;
                unsigned code=(unsigned)bytes[pos]|(unsigned)bytes[pos+1]<<8;pos+=2;
                size_t distance=(code>>4)+1,length=(code&15)+3;
                if(distance>sizeof(seed)-1+target || length>expected-target)return 0;
                for(size_t j=0;j<length;j++){decoded[target]=history(decoded,sizeof(seed)-1+target-distance);++target;}
            }
        }
    }
    if(pos!=used)return 0;
    size_t expanded=0;
    for(size_t i=0;i<target;++i){
        unsigned char c=(unsigned char)decoded[i];
        if(c>=128 && c<192){const char *key=keys[c-128];size_t z=strlen(key);if(z>=cap-expanded)return 0;memcpy(out+expanded,key,z);expanded+=z;}
        else {if(c==192){if(++i>=target)return 0;c=(unsigned char)decoded[i];}else if(c>192)return 0;if(expanded+1>=cap)return 0;out[expanded++]=(char)c;}
    }
    out[expanded]=0;return expanded;
}
