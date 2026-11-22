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

static bool exact_unsigned(const cJSON *item, uint64_t maximum, uint64_t *result)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > (double)maximum) return false;
    uint64_t integer = (uint64_t)item->valuedouble;
    if ((double)integer != item->valuedouble) return false;
    *result = integer;
    return true;
}

static bool exact_number(const cJSON *object, const char *name, uint64_t maximum, uint64_t *result)
{
    return exact_unsigned(cJSON_GetObjectItemCaseSensitive(object, name), maximum, result);
}

static bool uint_array(const cJSON *object, const char *key, unsigned size, uint64_t *out)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) != (int)size) return false;
    for (unsigned i = 0; i < size; ++i)
        if (!exact_unsigned(cJSON_GetArrayItem(array, (int)i), TB_LITE_SAFE_SEQ, &out[i])) return false;
    return true;
}

static bool fuel_windows(const cJSON *object, tb_lite_snapshot_t *snapshot)
{
    const cJSON *page = cJSON_GetObjectItemCaseSensitive(object, "w183");
    if (!page) return true;
    if (!cJSON_IsArray(page) || cJSON_GetArraySize(page) > (int)TB_FUEL_PAGE_SIZE) return false;
    snapshot->fuel_v2 = true;
    const cJSON *row;
    uint64_t previous = 0;
    cJSON_ArrayForEach(row, page) {
        const int size = cJSON_GetArraySize(row);
        uint64_t v[14];
        if (!cJSON_IsArray(row) || (size != 8 && size != 14)) return false;
        for (int i = 0; i < size; ++i)
            if (!exact_unsigned(cJSON_GetArrayItem(row, i), TB_LITE_SAFE_SEQ, &v[i])) return false;
        uint64_t end = v[0] * TB_LITE_OPERATIONAL_MS;
        if (!v[0] || v[0] > UINT32_MAX || v[0] <= previous || end > snapshot->uptime_ms ||
            (v[1] && v[1] < TB_LITE_EPOCH_MS) || v[2] > (v[3] * 1000000 + 3599) / 3600 ||
            v[3] > TB_LITE_OPERATIONAL_MS || v[4] > UINT32_MAX || v[5] > v[6] || v[6] > 1000000 ||
            (!v[4] && v[6]) || v[7] < v[2]) return false;
        tb_fuel_window_t *w = &snapshot->fuel_windows[snapshot->fuel_count++];
        *w = (tb_fuel_window_t){.slot = (uint32_t)v[0], .end_utc_ms = v[1], .ul = (uint32_t)v[2],
            .covered_ms = (uint32_t)v[3], .readings = (uint32_t)v[4], .min_mlph = (uint32_t)v[5],
            .max_mlph = (uint32_t)v[6], .total_ul = v[7]};
        if (size == 14) {
            if (!v[8] || v[8] >= end || v[9] >= UINT32_MAX || v[10] < 128 || v[10] > 255 || v[11] > 3 ||
                v[12] > v[7] || v[13] > v[8]) return false;
            w->counter = (tb_fuel_anchor_t){.at_ms = v[8], .ticks = (uint32_t)v[9], .mid = (uint8_t)v[10],
                .flags = (uint8_t)v[11], .observed_ul = v[12], .covered_ms = v[13]};
        }
        previous = v[0];
    }
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
    if (ok && cJSON_GetObjectItemCaseSensitive(o, "reset_reason")) {
        ok = exact_number(o, "reset_reason", UINT32_MAX, &number);
        if (ok) { s->reset_reason = (uint32_t)number; s->reset_reason_valid = true; }
    }
    for (unsigned i = 0; ok && i < TB_LITE_FIELDS; ++i) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(values,tb_lite_signals[i].key);
        if (!(s->valid & TB_LITE_BIT(i))) { ok = !item; continue; }
        ok = cJSON_IsNumber(item) && isfinite(item->valuedouble) && fabs(item->valuedouble) < 1e12;
        if (ok) s->values[i] = item->valuedouble;
    }
    uint64_t flow[3], anchors[4];
    if (ok && uint_array(o, "f183", 3, flow) && flow[0] && flow[0] <= s->uptime_ms && flow[1] <= flow[0] &&
        (double)flow[2] <= TB_LITE_FUEL_MAX_LPH * (double)flow[1] / 3.6 + 1)
        s->flow = (tb_lite_fuel_counter_t){.at_ms = flow[0], .covered_ms = flow[1], .microlitres = flow[2]};
    const uint32_t pairedbits = TB_LITE_BIT(TB_LITE_PID250) | TB_LITE_BIT(TB_LITE_ODOMETER);
    if (ok && ((s->valid & ~s->estimated) & pairedbits) == pairedbits && uint_array(o, "a250", 4, anchors) &&
        anchors[0] >= 128 && anchors[0] <= 255 && anchors[1] >= 128 && anchors[1] <= 255 &&
        anchors[2] && anchors[2] <= s->uptime_ms && s->uptime_ms - anchors[2] <= tb_lite_signals[TB_LITE_PID250].ttl_ms &&
        anchors[3] && anchors[3] <= s->uptime_ms && s->uptime_ms - anchors[3] <= tb_lite_signals[TB_LITE_ODOMETER].ttl_ms) {
        s->pid250_mid = (uint8_t)anchors[0]; s->odometer_mid = (uint8_t)anchors[1];
        s->pid250_at_ms = anchors[2]; s->odometer_at_ms = anchors[3];
    }
    ok = ok && fuel_windows(o, s);
    cJSON_Delete(o);
    return ok;
}

bool tb_lite_candidate(const tb_lite_cursor_t *cursor, const tb_lite_snapshot_t *s,
                       uint64_t link_now_ms, uint64_t *captured_ms)
{
    const bool fuel_due = s->fuel_count && s->fuel_windows[0].end_utc_ms >= TB_LITE_EPOCH_MS;
    const uint32_t age = s->valid ? s->age_ms : 0;
    if ((!fuel_due && (!s->valid || !s->local_seq ||
        (cursor->boot_id == s->boot_id && s->local_seq <= cursor->local_seq))) ||
        age > TB_LITE_MAX_AGE_MS || age > s->uptime_ms) return false;
    uint64_t now = link_now_ms >= TB_LITE_EPOCH_MS ? link_now_ms : 0;
    if (s->unix_ms >= TB_LITE_EPOCH_MS) {
        uint64_t skew = now > s->unix_ms ? now - s->unix_ms : s->unix_ms - now;
        now = now && skew > 10000u ? 0 : s->unix_ms;
    }
    /* Preserve unknown-time samples for diagnostics. Never invent a trip timestamp. */
    *captured_ms = now >= TB_LITE_EPOCH_MS + age && now - age > cursor->captured_ms ? now - age : 0;
    if (!*captured_ms && fuel_due && now >= TB_LITE_EPOCH_MS && now > cursor->captured_ms) *captured_ms = now;
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
    const uint32_t age = s->valid ? s->age_ms : 0;
    if (!seq || seq > TB_LITE_SAFE_SEQ || (!s->valid && !s->fuel_count) || age > s->uptime_ms ||
        (captured_ms && captured_ms < TB_LITE_EPOCH_MS)) return 0;
    tb_lite_buffer_t out = {.data=json, .capacity=capacity};
    tb_lite_append(&out,"{\"seq\":%" PRIu64 ",\"sample_kind\":\"%s\",\"source\":\"REVB1_LINK_LITE\","
        "\"boot_id\":%" PRIu32 ",\"core_seq\":%" PRIu64 ",\"uptime_ms\":%" PRIu64
        ",\"time_valid\":%s,\"unix_ms\":%" PRIu64 ",\"sensor_age_ms\":%" PRIu32
        ",\"valid_mask\":%" PRIu32 ",\"stale_mask\":%" PRIu32 ",\"estimated_mask\":%" PRIu32,
        seq,captured_ms ? "OPERATIONAL" : "DECODED",s->boot_id,s->local_seq,s->uptime_ms-age,
        captured_ms ? "true" : "false",captured_ms,age,s->valid,s->stale,s->estimated);
    if (tb_lite_uid_valid(s->uid)) tb_lite_append(&out, ",\"core_uid\":\"%s\"", s->uid);
    for (unsigned i = 0; i < TB_LITE_FIELDS; ++i) {
        if (!(s->valid & TB_LITE_BIT(i))) continue;
        const char *key = i == TB_LITE_ODOMETER && !validated_counter(s,TB_LITE_ODOMETER)
            ? "odometer_provisional_km" : tb_lite_signals[i].key;
        tb_lite_append(&out,",\"%s\":%.11g",key,s->values[i]);
    }
    if (!captured_ms) {
        tb_lite_append(&out, ",\"offline_time_quality\":\"BOOT_UPTIME_ONLY\"}");
        return out.failed ? 0 : out.used;
    }
    if (s->flow.at_ms) tb_lite_append(&out, ",\"f183\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "]",
        s->flow.at_ms, s->flow.covered_ms, s->flow.microlitres);
    if (s->fuel_v2) {
        const unsigned dated = s->fuel_count && s->fuel_windows[0].end_utc_ms >= TB_LITE_EPOCH_MS ? s->fuel_count : 0;
        tb_fuel_append_windows(&out, s->fuel_windows, dated);
    }
    double odo=s->values[TB_LITE_ODOMETER], fuel=s->values[TB_LITE_PID250];
    const uint64_t pair_gap = s->pid250_at_ms > s->odometer_at_ms
        ? s->pid250_at_ms - s->odometer_at_ms : s->odometer_at_ms - s->pid250_at_ms;
    if (!s->fuel_v2 && s->pid250_at_ms && s->odometer_at_ms && s->pid250_at_ms <= s->uptime_ms && s->odometer_at_ms <= s->uptime_ms &&
        pair_gap <= 5000 && validated_counter(s, TB_LITE_PID250) && validated_counter(s, TB_LITE_ODOMETER)) {
        /* Store monotonic ages. Cloud combines them with unix_ms + sensor_age_ms. */
        tb_lite_append(&out, ",\"a250\":[%u,%u,%" PRIu64 ",%" PRIu64 "]",
            s->pid250_mid, s->odometer_mid, s->uptime_ms - s->pid250_at_ms, s->uptime_ms - s->odometer_at_ms);
    }
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
                      const tb_lite_snapshot_t *s, bool online, const tb_lite_link_health_t *link,
                      tb_lite_event_t event)
{
    static const char *const events[] = {"MINUTE", "CORE_LOST", "CORE_RESTORED", "CORE_BOOT", "MID_GUARD_CHANGED", "CORE_SEEN"};
    if (!seq || seq > TB_LITE_SAFE_SEQ || epoch_ms < TB_LITE_EPOCH_MS || !link ||
        (unsigned)event >= sizeof(events)/sizeof(events[0])) return 0;
    tb_lite_buffer_t out = {.data=json, .capacity=capacity};
    tb_lite_append(&out,"{\"seq\":%" PRIu64 ",\"sample_kind\":\"HEALTH\",\"time_valid\":true,\"unix_ms\":%" PRIu64
        ",\"window_seconds\":%u,\"metrics\":{\"link_heap_minimum\":%" PRIu32 ",\"pending\":%" PRIu32 ",\"dropped\":%" PRIu32,
        seq,epoch_ms,event == TB_LITE_NO_EVENT ? 60 : 1,link->heap_minimum,link->pending,link->dropped);
    if (online) tb_lite_append(&out,",\"core_heap_minimum\":%" PRIu32 ",\"j1708_frames\":%" PRIu32
        ",\"j1708_sent\":%" PRIu32 ",\"j1708_answered\":%" PRIu32 ",\"j1708_timeouts\":%" PRIu32 ",\"j1708_guard\":%" PRIu32,
        s->heap_minimum,s->frames,s->sent,s->answered,s->timeouts,s->guard);
    tb_lite_append(&out, ",\"link_heap_free\":%" PRIu32 ",\"link_heap_largest_block\":%" PRIu32
        ",\"link_uptime_ms\":%" PRIu64 ",\"link_reset_reason\":%" PRIu32 ",\"wifi_rssi\":%d,\"wifi_connected\":%s"
        ",\"internet_reachable\":%s,\"cloud_connected\":%s,\"cloud_last_success_ms\":%" PRIu64
        ",\"cloud_last_http_status\":%d,\"cloud_consecutive_failures\":%" PRIu32
        ",\"offline_queue_capacity_bytes\":%" PRIu32 ",\"offline_queue_used_bytes\":%" PRIu32
        ",\"offline_queue_pending_records\":%" PRIu32 ",\"offline_queue_dropped_records\":%" PRIu32
        ",\"offline_queue_replayed_records\":%" PRIu32 ",\"queue_recovered_tail_bytes\":%" PRIu32
        ",\"queue_dropped_operational\":%" PRIu32 ",\"queue_dropped_health\":%" PRIu32
        ",\"core_link_disconnect_count\":%" PRIu32 ",\"core_poll_failures\":%" PRIu32,
        link->heap_free, link->largest_block, link->link_uptime_ms, link->link_reset_reason,
        link->wifi_rssi, link->wifi_connected ? "true" : "false",
        link->internet_reachable < 0 ? "null" : link->internet_reachable ? "true" : "false",
        link->cloud_connected ? "true" : "false", link->cloud_last_success_ms, link->last_http_status,
        link->consecutive_failures, link->capacity, link->used, link->pending, link->dropped, link->replayed,
        link->recovered_tail_bytes, link->dropped_operational, link->dropped_health,
        link->link_disconnects, link->poll_failures);
    if (link->oldest_age_valid)
        tb_lite_append(&out, ",\"offline_queue_oldest_age_seconds\":%" PRIu32, link->oldest_age);
    else tb_lite_append(&out, ",\"offline_queue_oldest_age_seconds\":null");
    if (s->reset_reason_valid) tb_lite_append(&out, ",\"core_reset_reason\":%" PRIu32, s->reset_reason);
    tb_lite_append(&out,"},\"context\":{\"source\":\"REVB1_LINK_LITE\",\"core_online\":%s,\"core_boot_id\":%" PRIu32
        ",\"link_boot_id\":%" PRIu32 ",\"event\":\"%s\",\"core_link_last_disconnect_reason\":\"%s\","
        "\"cloud_error_stage\":\"%s\"}}",online ? "true" : "false",s->boot_id,link->link_boot_id,events[event],
        link->disconnect_reason ? link->disconnect_reason : "", link->cloud_error_stage ? link->cloud_error_stage : "");
    return out.failed ? 0 : out.used;
}

bool tb_lite_record_meta(const char *record, uint64_t *sequence, bool *health, uint64_t *captured)
{
    cJSON *o = parse_object(record,strlen(record));
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(o,"sample_kind");
    bool valid = cJSON_IsString(kind) && exact_number(o,"seq",TB_LITE_SAFE_SEQ,sequence) && *sequence;
    if (valid) {
        *health = !strcmp(kind->valuestring,"HEALTH");
        valid = *health || !strcmp(kind->valuestring,"OPERATIONAL") ||
            (!strcmp(kind->valuestring,"DECODED") && cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(o,"time_valid")));
        if (captured && !exact_number(o,"unix_ms",TB_LITE_SAFE_SEQ,captured)) *captured = 0;
    }
    cJSON_Delete(o);
    return valid;
}

static bool acknowledged_count(const cJSON *o, const char *key, unsigned expected)
{
    uint64_t count;
    if (!expected && !cJSON_GetObjectItemCaseSensitive(o, key)) return true;
    return exact_number(o, key, expected, &count) && count == expected;
}

bool tb_lite_ack_valid(const char *response, size_t size, int status, uint64_t offered,
                       unsigned operational, unsigned health)
{
    if (status < 200 || status >= 300 || !offered || offered > TB_LITE_SAFE_SEQ || (!operational && !health)) return false;
    cJSON *o = parse_object(response,size);
    uint64_t seq;
    bool valid = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o,"ok")) &&
        exact_number(o,"ack_seq",TB_LITE_SAFE_SEQ,&seq) && seq == offered &&
        acknowledged_count(o,"accepted",operational) && acknowledged_count(o,"health",health) &&
        acknowledged_count(o,"events",0);
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

tb_lite_event_t tb_lite_contact_update(tb_lite_contact_t *contact, uint32_t observed_boot)
{
    if (!observed_boot) {
        if (contact->misses < 3) ++contact->misses;
        if (contact->boot && contact->misses == 3 && !contact->disconnected) {
            contact->disconnected = true;
            return TB_LITE_CORE_LOST;
        }
        return TB_LITE_NO_EVENT;
    }
    tb_lite_event_t event = !contact->boot ? TB_LITE_CORE_SEEN : contact->boot != observed_boot
        ? TB_LITE_CORE_BOOT : contact->disconnected ? TB_LITE_CORE_RESTORED : TB_LITE_NO_EVENT;
    *contact = (tb_lite_contact_t){.boot = observed_boot};
    return event;
}

uint32_t tb_lite_retry_delay(unsigned failures, int status)
{
    if (!failures) return 1000;
    if (status == 401 || status == 403 || status == 429) return 60000;
    return failures >= 5 ? 30000 : 1000u << failures;
}
