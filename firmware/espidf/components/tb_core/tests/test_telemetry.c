#define TB_TELEMETRY_HOST_TEST
#include "../telemetry.c"
#include <assert.h>
#include <stdio.h>

static cJSON *record(unsigned i)
{
    assert(i < host_record_count);
    cJSON *value = cJSON_Parse(host_records[i]);
    assert(value);
    assert(strlen(host_records[i]) < TB_RECORD_MAX);
    assert(cJSON_GetObjectItemCaseSensitive(value, "boot_id")->valueint == 42);
    return value;
}

static unsigned signals(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < host_record_count; ++i) {
        cJSON *value = record(i);
        count += (unsigned)cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(value, "signals"));
        cJSON_Delete(value);
    }
    return count;
}

static unsigned parent_signals(void)
{
    unsigned count = 0;
    for (unsigned i = 0; i < host_record_count; ++i) {
        cJSON *value = record(i);
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(value, "signals"))
            if (!cJSON_GetObjectItemCaseSensitive(item, "component_index")) ++count;
        cJSON_Delete(value);
    }
    return count;
}

static void reset_records(void)
{
    host_record_count = 0;
    s.serialization_errors = 0;
}

static void checksum(packet_t *packet)
{
    unsigned char sum = 0;
    for (unsigned i = 0; i < packet->length - 1u; ++i) sum += packet->bytes[i];
    packet->bytes[packet->length - 1u] = (unsigned char)(0u - sum);
    packet->supplied_length = packet->length;
}

static void test_capture_no_gps_and_later_fix(void)
{
    packet_t packet = { .can = true, .id = 0x18F00400, .extended = true,
        .length = 8, .supplied_length = 8, .captured_us = 1000000,
        .capture_id = 7, .bytes = {0,125,175,0x40,0x1F,0,0,0} };
    host_have_gps = false;
    reset_records(); process_packet(&packet);
    assert(host_record_count > 0 && parent_signals() == 5);
    bool provisional_fuel = false;
    for (unsigned i = 0; i < host_record_count; ++i) {
        cJSON *value = record(i);
        assert(cJSON_GetObjectItemCaseSensitive(value, "capture_id")->valueint == 7);
        assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(value, "time_valid")));
        assert(!cJSON_GetObjectItemCaseSensitive(value, "lat"));
        if (!i) assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(value, "gps")));
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(value, "signals")) {
            const cJSON *component = cJSON_GetObjectItemCaseSensitive(item, "component");
            if (cJSON_IsString(component) && !strcmp(component->valuestring, "fuel_rate_estimated_lph")) {
                assert(!strcmp(cJSON_GetObjectItemCaseSensitive(item, "validation")->valuestring, "EM_VALIDACAO"));
                assert(!strcmp(cJSON_GetObjectItemCaseSensitive(item, "quality")->valuestring, "EM_VALIDACAO"));
                assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "derived")));
                provisional_fuel = true;
            }
        }
        cJSON_Delete(value);
    }
    assert(provisional_fuel && !s.serialization_errors);
    /* A fix acquired while the packet waited cannot relabel that packet. */
    host_have_gps = true; host_gps_received_us = packet.captured_us + 1;
    reset_records(); process_packet(&packet);
    cJSON *first = record(0);
    assert(!cJSON_GetObjectItemCaseSensitive(first, "lat")); cJSON_Delete(first);
    packet.captured_us += 1000; packet.capture_id++;
    reset_records(); process_packet(&packet);
    first = record(0);
    assert(cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(first, "lat"))); cJSON_Delete(first);
    assert(!s.serialization_errors && parent_signals() == 5);
}

static void test_full_gps_complex_j1587_and_j1939_split(void)
{
    host_have_gps = host_full_gps = true; host_gps_received_us = 1000;
    packet_t packet = { .captured_us = 1000000, .wall_ms = UINT64_C(1794000123456),
        .capture_id = 22, .length = 5, .bytes = {128,155,0xFD,0xCB,0} };
    checksum(&packet);
    reset_records(); process_packet(&packet);
    assert(signals() == 1 && !s.serialization_errors);
    bool found_states = false;
    for (unsigned i = 0; i < host_record_count; ++i) {
        cJSON *value = record(i);
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(value, "signals");
        const cJSON *item = cJSON_GetArrayItem(items, 0);
        if (item) {
            assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(item, "states")) == 8);
            found_states = true;
        }
        cJSON_Delete(value);
    }
    assert(found_states);
    /* Six DTCs fit in a maximum21-byte J1708 packet. */
    packet.length = 21; packet.bytes[0] = 128; packet.bytes[1] = 194;
    packet.bytes[2] = 17;
    for (unsigned i = 0; i < 5; ++i) {
        packet.bytes[3 + i * 3] = (unsigned char)(18 + i);
        packet.bytes[4 + i * 3] = 0xB5; packet.bytes[5 + i * 3] = 1;
    }
    packet.bytes[18] = 22; packet.bytes[19] = 0x35;
    checksum(&packet);
    reset_records(); process_packet(&packet);
    assert(signals() == 1 && !s.serialization_errors);
    unsigned dtcs = 0;
    for (unsigned i = 0; i < host_record_count; ++i) {
        cJSON *value = record(i);
        cJSON *items = cJSON_GetObjectItemCaseSensitive(value, "signals");
        cJSON *item = cJSON_GetArrayItem(items, 0);
        if (item) dtcs += cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(item, "dtcs"));
        cJSON_Delete(value);
    }
    assert(dtcs == 6);
    packet = (packet_t){ .can = true, .id = 0x18FEEF00, .extended = true,
        .length = 8, .supplied_length = 8, .captured_us = 1000000,
        .wall_ms = UINT64_C(1794000123456), .capture_id = 30,
        .bytes = {100,100,100,100,100,100,100,100} };
    reset_records(); process_packet(&packet);
    assert(signals() == 7 && !s.serialization_errors);
    assert(host_record_count > 1);
}

static void test_invalid_raw_queue_pressure_and_latest(void)
{
    packet_t packet = { .captured_us = 1000000, .capture_id = 40,
        .length = 4, .supplied_length = 4, .bytes = {128,84,10,0} };
    reset_records(); process_packet(&packet);
    assert(host_record_count == 1 && signals() == 0);
    cJSON *raw = record(0);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(raw, "checksum_valid")));
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(raw, "raw"))); cJSON_Delete(raw);
    packet.oversize = true; packet.length = sizeof(packet.bytes);
    packet.supplied_length = sizeof(packet.bytes) + 1u;
    memset(packet.bytes, 0xFF, sizeof(packet.bytes));
    reset_records(); process_packet(&packet);
    assert(host_record_count == 1 && signals() == 0 && !s.serialization_errors);
    raw = record(0);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(raw, "raw_truncated_or_oversize")));
    assert(strlen(cJSON_GetObjectItemCaseSensitive(raw, "raw")->valuestring) == 128);
    cJSON_Delete(raw);
    packet = (packet_t){ .captured_us = 1000000, .capture_id = 42,
        .tx_echo = true, .length = 4, .bytes = {128,84,10,0} };
    checksum(&packet);
    reset_records(); process_packet(&packet);
    assert(host_record_count == 1 && signals() == 0 && !s.serialization_errors);
    raw = record(0);
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(raw, "origin")->valuestring, "TX_ECHO"));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(raw, "j1587_result")->valuestring, "TX_ECHO"));
    cJSON_Delete(raw);
    packet = (packet_t){ .can = true, .id = 0x18F00400, .extended = true,
        .rtr = true, .length = 8, .supplied_length = 8, .captured_us = 1000000,
        .capture_id = 41, .bytes = {1,2,3,4,5,6,7,8} };
    reset_records(); process_packet(&packet);
    assert(host_record_count == 1 && signals() == 0 && !s.serialization_errors);
    raw = record(0);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(raw, "rtr")));
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(raw, "data")->valuestring, ""));
    cJSON_Delete(raw);
    host_fail_store = true;
    uint64_t rejected = s.store_rejected;
    process_packet(&packet); assert(s.store_rejected == rejected + 1);
    host_fail_store = false;
    cJSON *latest = tb_telemetry_sensors_json(0, 32);
    assert(latest && cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(latest, "readings")) > 0);
    cJSON_Delete(latest);
    for (unsigned i = 0; i < TELEMETRY_QUEUE; ++i)
        assert(tb_telemetry_j1708(packet.bytes, packet.length, packet.captured_us, false));
    assert(!tb_telemetry_j1708(packet.bytes, packet.length, packet.captured_us, false));
    assert(s.queue_dropped == 1);
    assert(s.next_capture == TELEMETRY_QUEUE + 1);
}

static size_t unhex(const char *input, uint8_t *bytes)
{
    size_t length = strlen(input) / 2;
    for (size_t i = 0; i < length; ++i) {
        unsigned value = 0;
        assert(sscanf(input + i * 2, "%2x", &value) == 1);
        bytes[i] = (uint8_t)value;
    }
    return length;
}

static void bam(uint32_t pgn, uint8_t sa, const uint8_t *data, size_t length,
                 uint64_t capture)
{
    packet_t packet = { .can = true, .extended = true, .length = 8,
        .supplied_length = 8, .id = UINT32_C(0x18ECFF00) | sa,
        .capture_id = capture, .captured_us = capture * 1000,
        .wall_ms = UINT64_C(1794000123456),
        .bytes = {0x20, (uint8_t)length, 0, (uint8_t)((length + 6) / 7), 0xFF,
            (uint8_t)pgn, (uint8_t)(pgn >> 8), (uint8_t)(pgn >> 16)} };
    process_packet(&packet);
    packet.id = UINT32_C(0x18EBFF00) | sa;
    for (size_t offset = 0; offset < length; offset += 7) {
        memset(packet.bytes, 0xFF, sizeof(packet.bytes));
        packet.bytes[0] = (uint8_t)(offset / 7 + 1);
        size_t bytes = length - offset;
        if (bytes > 7) bytes = 7;
        memcpy(packet.bytes + 1, data + offset, bytes);
        ++packet.capture_id;
        packet.captured_us += 1000;
        process_packet(&packet);
    }
}

static void check_bam_records(uint32_t pgn, uint8_t sa, const uint8_t *payload,
                               size_t length, uint64_t first_capture,
                               unsigned curve_id, const double expected[10])
{
    unsigned physical = 0, curve_components = 0;
    bool raw_found = false;
    char expected_raw[129]; hex(expected_raw, payload, length);
    for (unsigned i = 0; i < host_record_count; ++i) {
        cJSON *value = record(i);
        cJSON *source = cJSON_GetObjectItemCaseSensitive(value, "source");
        assert(cJSON_IsString(source));
        if (!strcmp(source->valuestring, "can")) {
            ++physical;
            cJSON *dlc = cJSON_GetObjectItemCaseSensitive(value, "dlc");
            assert(cJSON_IsNumber(dlc) && dlc->valueint == 8);
        } else {
            assert(!strcmp(source->valuestring, "j1939_tp"));
            assert(!cJSON_GetObjectItemCaseSensitive(value, "id"));
            assert(cJSON_GetObjectItemCaseSensitive(value, "pgn")->valueint == (int)pgn);
            assert(cJSON_GetObjectItemCaseSensitive(value, "sa")->valueint == sa);
            assert(cJSON_GetObjectItemCaseSensitive(value, "tp_start_capture_id")->valuedouble == (double)first_capture);
            assert(cJSON_GetObjectItemCaseSensitive(value, "tp_end_capture_id")->valuedouble ==
                (double)(first_capture + (length + 6) / 7));
            cJSON *raw = cJSON_GetObjectItemCaseSensitive(value, "raw_payload");
            if (raw) { assert(!strcmp(raw->valuestring, expected_raw)); raw_found = true; }
            const cJSON *item;
            cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(value, "signals")) {
                const cJSON *sid = cJSON_GetObjectItemCaseSensitive(item, "sid");
                const cJSON *index = cJSON_GetObjectItemCaseSensitive(item, "component_index");
                if (sid->valueint == (int)curve_id && index) {
                    assert(index->valueint >= 0 && index->valueint < 10);
                    const cJSON *number = cJSON_GetObjectItemCaseSensitive(item, "value");
                    assert(cJSON_IsNumber(number));
                    assert(fabs(number->valuedouble - expected[index->valueint]) < 0.001);
                    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(item, "component")));
                    ++curve_components;
                }
            }
        }
        cJSON_Delete(value);
    }
    assert(physical == 1u + (length + 6u) / 7u);
    assert(raw_found && curve_components == 10u && !s.serialization_errors);
}

static void test_bam_grouped_components_and_latest_history(void)
{
    /* These payloads are constructed from the supplied Documento Mestre
     * offsets/examples. They are not a replay of unavailable mother logs. */
    uint8_t engine[64], retarder[64];
    size_t engine_length = unhex("C012B1F041B38025E0E02EDB4038CEF041FFFF530AE047FF32FA7DE1C05D", engine);
    size_t retarder_length = unhex("0304201C2CE04721802522003C1D8034A20319", retarder);
    const double engine_curve[10] = {600,52,2110,54,1200,99,1500,94,1800,81};
    const double retarder_curve[10] = {900,-81,2300,-92,1200,-91,1920,-96,1680,-100};
    assert(engine_length == 30 && retarder_length == 19);
    reset_records(); bam(65251, 0, engine, engine_length, 1000);
    check_bam_records(65251, 0, engine, engine_length, 1000, 10, engine_curve);
    assert(signals() == 22);
    reset_records(); bam(65249, 1, retarder, retarder_length, 1100);
    check_bam_records(65249, 1, retarder, retarder_length, 1100, 55, retarder_curve);
    assert(signals() == 13);
    /* The transport bound is64; the latest API intentionally stores at most32
     * raw bytes, explicitly marked, while the SD record preserves all64. */
    memset(engine + engine_length, 0xFF, sizeof(engine) - engine_length);
    reset_records(); bam(65251, 5, engine, sizeof(engine), 1200);
    check_bam_records(65251, 5, engine, sizeof(engine), 1200, 10, engine_curve);
    bool truncated = false;
    for (size_t offset = 0; offset < s.latest_count; offset += 32) {
        cJSON *latest = tb_telemetry_sensors_json(offset, 32);
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(latest, "readings")) {
            const cJSON *sa = cJSON_GetObjectItemCaseSensitive(item, "sa");
            if (sa && sa->valueint == 5) {
                assert(cJSON_GetObjectItemCaseSensitive(item, "raw_length")->valueint == 64);
                assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(item, "raw_truncated_in_latest")));
                assert(strlen(cJSON_GetObjectItemCaseSensitive(item, "raw")->valuestring) == 64);
                truncated = true;
            }
        }
        cJSON_Delete(latest);
    }
    assert(truncated);
    /* A plausible but different calibration is a profile mismatch; it must not
     * replace the previously accepted2643Nm operational reference. */
    engine[19] = 0x54;
    reset_records(); bam(65251, 5, engine, sizeof(engine), 1300);
    bool profile_retained = false;
    for (size_t offset = 0; offset < s.latest_count; offset += 32) {
        cJSON *latest = tb_telemetry_sensors_json(offset, 32);
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(latest, "readings")) {
            const cJSON *sa = cJSON_GetObjectItemCaseSensitive(item, "sa");
            const cJSON *sid = cJSON_GetObjectItemCaseSensitive(item, "sid");
            if (sa && sa->valueint == 5 && sid && sid->valueint == 9) {
                assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(item, "value")));
                assert(!strcmp(cJSON_GetObjectItemCaseSensitive(item, "quality")->valuestring, "SUSPEITO_PERFIL"));
                assert(cJSON_GetObjectItemCaseSensitive(item, "last_valid_value")->valuedouble == 2643);
                profile_retained = true;
            }
        }
        cJSON_Delete(latest);
    }
    assert(profile_retained && !s.serialization_errors);
    packet_t packet = { .can = true, .id = 0x18F0040A, .extended = true,
        .length = 8, .supplied_length = 8, .captured_us = 2000000,
        .capture_id = 2000, .bytes = {0,125,175,0x40,0x1F,0,0,0} };
    reset_records(); process_packet(&packet);
    packet.bytes[3] = packet.bytes[4] = 0xFF;
    packet.capture_id++; packet.captured_us += 1000;
    process_packet(&packet);
    bool retained = false;
    for (size_t offset = 0; offset < s.latest_count; offset += 32) {
        cJSON *latest = tb_telemetry_sensors_json(offset, 32);
        const cJSON *item;
        cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(latest, "readings")) {
            const cJSON *sa = cJSON_GetObjectItemCaseSensitive(item, "sa");
            const cJSON *sid = cJSON_GetObjectItemCaseSensitive(item, "sid");
            if (sa && sa->valueint == 10 && sid && sid->valueint == 1) {
                assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(item, "value")));
                assert(cJSON_GetObjectItemCaseSensitive(item, "last_valid_value")->valuedouble == 1000);
                assert(cJSON_GetObjectItemCaseSensitive(item, "last_valid_age_ms")->valuedouble == 8000);
                retained = true;
            }
        }
        cJSON_Delete(latest);
    }
    assert(retained && !s.serialization_errors);
}

static void test_legacy_decodes_without_persistent_raw(void)
{
    host_telemetry_config.full_decode_enabled = false;
    packet_t can = { .can = true, .id = 0x18F00400, .extended = true,
        .length = 8, .supplied_length = 8, .captured_us = 4000000,
        .capture_id = 4000, .bytes = {0,125,175,0x40,0x1F,0,0,0} };
    uint64_t processed_before = s.processed;
    uint64_t stored_before = s.store_accepted;
    reset_records();
    process_packet(&can);
    assert(host_record_count == 0);
    assert(s.processed == processed_before + 1);
    cJSON *live = tb_telemetry_sensors_json(0, 32);
    assert(live);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(live, "readings")) > 0);
    cJSON_Delete(live);

    packet_t j1708 = { .capture_id = 4001, .captured_us = 4001000,
        .length = 5, .bytes = {128,155,0xFD,0xCB,0} };
    checksum(&j1708);
    process_packet(&j1708);
    assert(host_record_count == 0);
    assert(s.store_accepted == stored_before);
    assert(s.processed == processed_before + 2);
    assert(!s.serialization_errors);
    host_telemetry_config.full_decode_enabled = true;
}

int main(void)
{
    assert(tb_telemetry_start() == ESP_OK);
    test_capture_no_gps_and_later_fix();
    test_full_gps_complex_j1587_and_j1939_split();
    test_invalid_raw_queue_pressure_and_latest();
    test_bam_grouped_components_and_latest_history();
    test_legacy_decodes_without_persistent_raw();
    vQueueDelete(s.queue);
    puts("OK: RAW mode serialization and LEGACY J1939/J1587 decode with zero raw persistent writes");
    return 0;
}
