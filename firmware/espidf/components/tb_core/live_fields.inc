/* Read-only live adapter: reuse the bounded decoder cache, allocate no heap.
 * Reference2643Nm is the historical profile documented in Master v1.1;
 * only the owner's device may derive percent from J1587 PID93 absolute Nm. */
static bool live_latest(uint16_t pid,uint8_t mid,uint64_t now,latest_t *out)
{
    memset(out,0,sizeof(*out));
    portENTER_CRITICAL(&s_mux);
    for(size_t i=0;i<s.latest_count;i++){
        const latest_t *p=&s.latest[i];
        if(p->used && !p->can && p->signal==pid && p->component_index==UINT8_MAX &&
           (!mid || p->address==mid) && p->captured_us>out->captured_us)*out=*p;
    }
    portEXIT_CRITICAL(&s_mux);
    return out->used && out->captured_us && now>=out->captured_us;
}
static bool live_valid(const latest_t *p,uint64_t now,uint64_t max_age)
{return p->used && p->captured_us && now>=p->captured_us && now-p->captured_us<=max_age && p->quality && !strcmp(p->quality,"EM_VALIDACAO");}
static const char *live_quality(const latest_t *p,uint64_t now,uint64_t max_age)
{
    if(!p->used || !p->captured_us || now<p->captured_us)return "SEM_LEITURA";
    if(now-p->captured_us>max_age)return "DESATUALIZADO";
    return p->quality?p->quality:"SEM_LEITURA";
}
static bool live_gear(const latest_t *p,uint64_t now,char number[40],char label[4])
{
    snprintf(number,40,"null");label[0]=0;
    if(!live_valid(p,now,5000000))return false;
    char *end;long value=strtol(p->text,&end,10);bool valid=end!=p->text && !*end;
    if(!strcmp(p->text,"N")){value=0;valid=true;}
    else if(!strcmp(p->text,"R")){value=-1;valid=true;}
    if(valid && value>=-99 && value<=99){snprintf(number,40,"%ld",value);snprintf(label,4,"%.3s",p->text);return true;}
    if(!strcmp(p->text,"A") || !strcmp(p->text,"M"))snprintf(label,4,"%.3s",p->text);
    return false;
}
static void live_age(char out[32],const latest_t *p,uint64_t now)
{
    if(p->used && p->captured_us && now>=p->captured_us)snprintf(out,32,"%" PRIu64,(now-p->captured_us)/1000);
    else snprintf(out,32,"null");
}
size_t tb_telemetry_live_fields(char *buffer,size_t capacity)
{
    if(!buffer || !capacity)return 0;
    const uint64_t now=(uint64_t)esp_timer_get_time();latest_t p={0};
    char speed[40],rpm[40],pedal[40],odo[40],flow[40],gear[40],selected[40],nm[40],pct[40],pressure[40];
    const uint16_t pids[]={84,190,91,245,183};char *outputs[]={speed,rpm,pedal,odo,flow};
    for(unsigned i=0;i<5;i++){
        (void)live_latest(pids[i],i==1 || i==4?128:0,now,&p);
        tb_live_number(outputs[i],live_valid(&p,now,i==3?30000000:5000000) && p.has_value,p.value);
    }
    latest_t g={0},t={0},f={0};char gear_text[4],selected_text[4],graw[65],traw[65],fraw[65],ga[32],ta[32],fa[32];
    (void)live_latest(163,130,now,&g);(void)live_gear(&g,now,gear,gear_text);
    (void)live_latest(162,130,now,&p);(void)live_gear(&p,now,selected,selected_text);
    (void)live_latest(93,128,now,&t);bool torque=live_valid(&t,now,65000000) && t.has_value && isfinite(t.value);
    bool reference=!strcmp(tb_device_uid(),"tb-c6-d40592b9190c");
    tb_live_number(nm,torque,t.value);tb_live_number(pct,torque && reference,t.value*100.0/2643.0);
    (void)live_latest(94,128,now,&f);tb_live_number(pressure,live_valid(&f,now,15000000) && f.has_value,f.value);
    hex(graw,g.raw,g.raw_length);hex(traw,t.raw,t.raw_length);hex(fraw,f.raw,f.raw_length);
    live_age(ga,&g,now);live_age(ta,&t,now);live_age(fa,&f,now);
    const char *tq=torque && reference?"DERIVADO_REFERENCIA_HISTORICA_EM_VALIDACAO":live_quality(&t,now,65000000);
    int n=snprintf(buffer,capacity,",\"live_contract\":\"TBX_CORE_LIVE_V1\",\"speedKph\":%s,\"rpm\":%s,\"pedal_pct\":%s,\"distanceTotalKm\":%s,\"fuel_rate_lph\":%s,\"gear\":%s,\"gear_text\":\"%s\",\"selected_gear\":%s,\"selected_gear_text\":\"%s\",\"torqueNm\":%s,\"actualTorquePct\":%s,\"fuel_pressure_bar\":%s,\"live_readings\":{\"gear\":{\"pid\":163,\"mid\":%u,\"raw\":\"%s\",\"age_ms\":%s,\"quality\":\"%s\"},\"torque\":{\"pid\":93,\"mid\":%u,\"raw\":\"%s\",\"age_ms\":%s,\"quality\":\"%s\",\"reference_nm\":%s,\"reference_source\":\"MASTER_V1_1_PGN65251_HISTORICAL\"},\"fuel_pressure\":{\"pid\":94,\"mid\":%u,\"raw\":\"%s\",\"age_ms\":%s,\"quality\":\"%s\"}},\"diagnostics_pid\":194,\"diagnostics_endpoint\":\"/api/diagnostics?offset=0\"",
        speed,rpm,pedal,odo,flow,gear,gear_text,selected,selected_text,nm,pct,pressure,
        g.address,graw,ga,live_quality(&g,now,5000000),t.address,traw,ta,tq,reference?"2643":"null",
        f.address,fraw,fa,live_quality(&f,now,15000000));
    if(n<=0 || (size_t)n>=capacity){buffer[0]=0;return 0;}return (size_t)n;
}
static void add_live_json(cJSON *object)
{
    char encoded[1538];encoded[0]='{';size_t n=tb_telemetry_live_fields(encoded+1,sizeof(encoded)-3);
    if(!n)return;
    encoded[1]=' ';encoded[n+1]='}';encoded[n+2]=0;
    cJSON *live=cJSON_Parse(encoded);if(live)tb_json_add_object(object,"live",live);
}
size_t tb_telemetry_live_extra_fields(char *buffer,size_t capacity)
{
    if(!buffer || !capacity)return 0;
    static const struct {uint16_t pid;uint8_t mid;const char *key;} fields[]={
        {110,128,"coolant_temp_c"},{175,128,"oil_temp_c"},{90,128,"fuel_temp_c"},
        {171,0,"ambient_temp_c"},{105,128,"intake_temp_c"},{98,128,"oil_level_pct"},
        {111,128,"coolant_level_pct"},{100,128,"oil_pressure_bar"},{109,128,"coolant_pressure_bar"},
        {153,128,"crankcase_pressure_kpa"},{106,128,"air_inlet_pressure_kpa"},{107,128,"air_cleaner_diff_pressure_kpa"},
        {102,128,"boost_bar"},{177,130,"gearbox_temp_c"},{161,130,"trans_input_rpm"},
        {191,130,"trans_output_rpm"},{73,0,"brake_pedal_pct"},{92,128,"load_pct"},
        {168,0,"battery_voltage_v"},{117,140,"brake_air_primary_bar"},{118,140,"brake_air_secondary_bar"},
        {122,128,"veb_pct"}
    };
    uint64_t now=(uint64_t)esp_timer_get_time();size_t used=0;buffer[0]=0;
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++){
        latest_t p={0};char number[40];(void)live_latest(fields[i].pid,fields[i].mid,now,&p);
        tb_live_number(number,live_valid(&p,now,40000000) && p.has_value,p.value);
        int n=snprintf(buffer+used,capacity-used,",\"%s\":%s",fields[i].key,number);
        if(n<=0 || (size_t)n>=capacity-used){buffer[0]=0;return 0;}used+=(size_t)n;
    }
    return used;
}
