#include "tb_link_telemetry_codec.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <string.h>

static cJSON *parse_object(const char *json, size_t size)
{
    if (!json || size < 2 || json[size] || memchr(json, 0, size)) return NULL;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; ++i) {
        char c = json[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 6) return NULL; }
        else if (c == '}' || c == ']') { if (!depth) return NULL; --depth; }
    }
    if (depth || quoted) return NULL;
    cJSON *object = cJSON_ParseWithLengthOpts(json, size + 1, NULL, true);
    bool valid = cJSON_IsObject(object);
    for (const cJSON *field = object ? object->child : NULL; valid && field; field = field->next) {
        for (const cJSON *other = field->next; other; other = other->next) {
            if (!strcmp(field->string,other->string)) { valid = false; break; }
        }
    }
    if (valid) return object;
    cJSON_Delete(object);
    return NULL;
}

static bool exact_number(const cJSON *object, const char *name, uint64_t maximum, uint64_t *result)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > (double)maximum) return false;
    uint64_t integer = (uint64_t)item->valuedouble;
    if ((double)integer != item->valuedouble) return false;
    *result = integer;
    return true;
}

bool tb_lite_snapshot_decode(const char *json, size_t size, tb_lite_snapshot_t *s)
{
    if (!s || size >= TB_LITE_SNAPSHOT_MAX) return false;
    cJSON *o = parse_object(json, size);
    if (!o) return false;
    memset(s, 0, sizeof(*s));
    const cJSON *uid = cJSON_GetObjectItemCaseSensitive(o,"device_uid");
    const cJSON *values = cJSON_GetObjectItemCaseSensitive(o,"values");
    uint64_t number;
    bool ok = cJSON_IsString(uid) && tb_lite_uid_valid(uid->valuestring) &&
              strlen(uid->valuestring) < sizeof(s->uid) && cJSON_IsObject(values) &&
              exact_number(o,"v",1,&number) && number == 1;
    if (ok) strcpy(s->uid, uid->valuestring);
    const struct {const char *key; uint32_t *target;} fields[] = {
        {"boot_id",&s->boot_id}, {"age_ms",&s->age_ms}, {"valid",&s->valid},
        {"stale",&s->stale}, {"estimated",&s->estimated}, {"frames",&s->frames},
        {"sent",&s->sent}, {"answered",&s->answered}, {"timeouts",&s->timeouts},
        {"guard",&s->guard}, {"heap_minimum",&s->heap_minimum},
    };
    for (size_t i = 0; ok && i < sizeof(fields)/sizeof(fields[0]); ++i) {
        ok = exact_number(o, fields[i].key, UINT32_MAX, &number);
        if (ok) *fields[i].target = (uint32_t)number;
    }
    ok = ok && s->boot_id && !(s->valid & s->stale) && !(s->estimated & ~s->valid) &&
        !((s->valid | s->stale) & ~TB_LITE_ALL) &&
        exact_number(o,"local_seq",TB_LITE_SAFE_SEQ,&s->local_seq) &&
        exact_number(o,"uptime_ms",TB_LITE_SAFE_SEQ,&s->uptime_ms) &&
        exact_number(o,"unix_ms",TB_LITE_SAFE_SEQ,&s->unix_ms) &&
        (s->valid ? s->local_seq && s->age_ms <= TB_LITE_MAX_AGE_MS && s->age_ms <= s->uptime_ms
                  : s->age_ms == UINT32_MAX);
    for (unsigned i = 0; ok && i < TB_LITE_FIELDS; ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(values,tb_lite_signals[i].key);
        if (!(s->valid & TB_LITE_BIT(i))) { ok = !item; continue; }
        ok = cJSON_IsNumber(item) && isfinite(item->valuedouble) && fabs(item->valuedouble) < 1e12;
        if (ok) s->values[i] = item->valuedouble;
    }
    cJSON_Delete(o);
    return ok;
}

bool tb_lite_candidate(const tb_lite_cursor_t *cursor, const tb_lite_snapshot_t *s,
                       uint64_t link_now_ms, uint64_t *captured_ms)
{
    if (!s->valid || !s->local_seq || s->age_ms > TB_LITE_MAX_AGE_MS ||
        link_now_ms < TB_LITE_EPOCH_MS + s->age_ms ||
        (cursor->boot_id == s->boot_id && s->local_seq <= cursor->local_seq)) return false;
    uint64_t now = link_now_ms;
    if (s->unix_ms >= TB_LITE_EPOCH_MS) {
        uint64_t skew = s->unix_ms > now ? s->unix_ms - now : now - s->unix_ms;
        if (skew > 10000u) return false;
        if (s->unix_ms < now) now = s->unix_ms;
    }
    if (now < TB_LITE_EPOCH_MS + s->age_ms || now - s->age_ms <= cursor->captured_ms) return false;
    *captured_ms = now - s->age_ms;
    return true;
}

static bool validated_counter(const tb_lite_snapshot_t *s, tb_lite_field_t field)
{
    return (s->valid & ~s->estimated & TB_LITE_BIT(field)) != 0 &&
           s->values[field] >= 0 && s->values[field] < 1e12;
}

bool tb_lite_reference_valid(const tb_lite_reference_t *ref)
{
    return (!ref->started_ms || (ref->started_ms >= TB_LITE_EPOCH_MS && ref->started_ms <= TB_LITE_SAFE_SEQ)) &&
        (!ref->odometer_ms || (ref->started_ms && ref->odometer_ms >= ref->started_ms &&
          ref->odometer_ms <= TB_LITE_SAFE_SEQ && ref->odometer >= 0 && ref->odometer < 1e12)) &&
        (!ref->pid250_ms || (ref->started_ms && ref->pid250_ms >= ref->started_ms &&
          ref->pid250_ms <= TB_LITE_SAFE_SEQ && ref->pid250 >= 0 && ref->pid250 < 1e12));
}

bool tb_lite_reference_update(tb_lite_reference_t *ref, const tb_lite_snapshot_t *s, uint64_t captured_ms)
{
    if (captured_ms < TB_LITE_EPOCH_MS || captured_ms < ref->started_ms) return false;
    bool changed = false;
    if (!ref->started_ms) { ref->started_ms = captured_ms; changed = true; }
    if (!ref->odometer_ms && validated_counter(s, TB_LITE_ODOMETER)) {
        ref->odometer_ms = captured_ms;
        ref->odometer = s->values[TB_LITE_ODOMETER];
        changed = true;
    }
    if (!ref->pid250_ms && validated_counter(s, TB_LITE_PID250)) {
        ref->pid250_ms = captured_ms;
        ref->pid250 = s->values[TB_LITE_PID250];
        changed = true;
    }
    return changed;
}

static bool plausible_delta(double current, double baseline, uint64_t time, uint64_t start, double per_hour)
{
    return start && time >= start && current >= baseline &&
        current - baseline <= 1.0 + (double)(time-start) * per_hour / 3600000.0;
}

size_t tb_lite_operational(char *json, size_t capacity, uint64_t seq, uint64_t captured_ms,
                           const tb_lite_snapshot_t *s, const tb_lite_reference_t *ref)
{
    if (!seq || seq > TB_LITE_SAFE_SEQ || !s->valid || captured_ms < TB_LITE_EPOCH_MS) return 0;
    tb_lite_buffer_t out = {.data=json, .capacity=capacity};
    tb_lite_append(&out,"{\"seq\":%" PRIu64 ",\"sample_kind\":\"OPERATIONAL\",\"source\":\"REVB1_LINK_LITE\","
        "\"boot_id\":%" PRIu32 ",\"core_seq\":%" PRIu64 ",\"uptime_ms\":%" PRIu64
        ",\"time_valid\":true,\"unix_ms\":%" PRIu64 ",\"sensor_age_ms\":%" PRIu32
        ",\"valid_mask\":%" PRIu32 ",\"stale_mask\":%" PRIu32 ",\"estimated_mask\":%" PRIu32,
        seq,s->boot_id,s->local_seq,s->uptime_ms-s->age_ms,captured_ms,s->age_ms,s->valid,s->stale,s->estimated);
    for (unsigned i = 0; i < TB_LITE_FIELDS; ++i) {
        if (!(s->valid & TB_LITE_BIT(i))) continue;
        const char *key = i == TB_LITE_ODOMETER && !validated_counter(s,TB_LITE_ODOMETER)
            ? "odometer_provisional_km" : tb_lite_signals[i].key;
        tb_lite_append(&out,",\"%s\":%.11g",key,s->values[i]);
    }
    double odo=s->values[TB_LITE_ODOMETER], fuel=s->values[TB_LITE_PID250];
    bool distance = validated_counter(s,TB_LITE_ODOMETER) && plausible_delta(odo,ref->odometer,captured_ms,ref->odometer_ms,200);
    bool direct = validated_counter(s,TB_LITE_PID250) && plausible_delta(fuel,ref->pid250,captured_ms,ref->pid250_ms,200);
    tb_lite_append(&out,",\"lite_reference_ms\":%" PRIu64 ",\"fuel_quality\":\"%s\"",ref->started_ms,
        direct ? "CONFIRMED_ECU_PID250" : (s->valid & TB_LITE_BIT(TB_LITE_FUEL_RATE)) ? "ESTIMATE_FROM_RATE_PID183" : "UNAVAILABLE");
    if (distance) tb_lite_append(&out,",\"trip_distance_km\":%.9g,\"lite_start_odometer_km\":%.11g",odo-ref->odometer,ref->odometer);
    if (direct) {
        tb_lite_append(&out,",\"trip_fuel_l\":%.9g,\"lite_pid250_reference_ms\":%" PRIu64,fuel-ref->pid250,ref->pid250_ms);
        if (distance && fuel > ref->pid250 && ref->odometer_ms == ref->pid250_ms)
            tb_lite_append(&out,",\"trip_average_kml\":%.9g",(odo-ref->odometer)/(fuel-ref->pid250));
    }
    tb_lite_append(&out,"}");
    return out.failed ? 0 : out.used;
}

size_t tb_lite_health(char *json, size_t capacity, uint64_t seq, uint64_t epoch_ms,
                      const tb_lite_snapshot_t *s, bool online, unsigned pending,
                      unsigned dropped, unsigned link_heap, tb_lite_event_t event)
{
    static const char *const events[] = {"MINUTE", "CORE_LOST", "CORE_RESTORED", "CORE_BOOT", "MID_GUARD_CHANGED"};
    if (!seq || seq > TB_LITE_SAFE_SEQ || epoch_ms < TB_LITE_EPOCH_MS || (unsigned)event >= sizeof(events)/sizeof(events[0])) return 0;
    tb_lite_buffer_t out = {.data=json, .capacity=capacity};
    tb_lite_append(&out,"{\"seq\":%" PRIu64 ",\"sample_kind\":\"HEALTH\",\"time_valid\":true,\"unix_ms\":%" PRIu64
        ",\"window_seconds\":%u,\"metrics\":{\"link_heap_minimum\":%u,\"pending\":%u,\"dropped\":%u",
        seq,epoch_ms,event == TB_LITE_NO_EVENT ? 60 : 1,link_heap,pending,dropped);
    if (online) tb_lite_append(&out,",\"core_heap_minimum\":%" PRIu32 ",\"j1708_frames\":%" PRIu32
        ",\"j1708_sent\":%" PRIu32 ",\"j1708_answered\":%" PRIu32 ",\"j1708_timeouts\":%" PRIu32 ",\"j1708_guard\":%" PRIu32,
        s->heap_minimum,s->frames,s->sent,s->answered,s->timeouts,s->guard);
    tb_lite_append(&out,"},\"context\":{\"source\":\"REVB1_LINK_LITE\",\"core_online\":%s,\"core_boot_id\":%" PRIu32
        ",\"event\":\"%s\"}}",online ? "true" : "false",s->boot_id,events[event]);
    return out.failed ? 0 : out.used;
}

bool tb_lite_record_meta(const char *record, uint64_t *sequence, bool *health)
{
    cJSON *o = parse_object(record,strlen(record));
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(o,"sample_kind");
    bool valid = cJSON_IsString(kind) && exact_number(o,"seq",TB_LITE_SAFE_SEQ,sequence) && *sequence;
    if (valid) {
        *health = !strcmp(kind->valuestring,"HEALTH");
        valid = *health || !strcmp(kind->valuestring,"OPERATIONAL");
    }
    cJSON_Delete(o);
    return valid;
}

bool tb_lite_ack_valid(const char *response, size_t size, int status, uint64_t offered, bool health)
{
    if (status < 200 || status >= 300 || !offered || offered > TB_LITE_SAFE_SEQ) return false;
    cJSON *o = parse_object(response,size);
    uint64_t seq, count;
    bool valid = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"ok")) &&
        exact_number(o,"ack_seq",TB_LITE_SAFE_SEQ,&seq) && seq == offered &&
        exact_number(o,health?"health":"accepted",1,&count) && count == 1;
    cJSON_Delete(o);
    return valid;
}

bool tb_lite_sequence_range(uint64_t floor, uint64_t previous_end, uint64_t *first, uint64_t *end)
{
    const unsigned reservation = 128;
    if (!floor || floor > TB_LITE_SAFE_SEQ || previous_end >= TB_LITE_SAFE_SEQ) return false;
    uint64_t from = previous_end + 1;
    if (floor > from) from = floor;
    if (from > TB_LITE_SAFE_SEQ - (reservation - 1)) return false;
    *first = from;
    *end = from + reservation - 1;
    return true;
}
