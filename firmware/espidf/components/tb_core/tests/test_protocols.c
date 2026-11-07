#include "protocols.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void checksum(uint8_t *frame, size_t length)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < length - 1; ++i) {
        sum = (uint8_t)(sum + frame[i]);
    }
    frame[length - 1] = (uint8_t)(0U - sum);
}

static void test_can(void)
{
    char out[768];
    uint8_t rpm[8] = {0xff, 0xff, 0xff, 0x41, 0x1f, 0xff, 0xff, 0xff};
    assert(tb_protocol_can_json(out, sizeof(out), 123, 0x0cf00401,
                                true, false, 8, rpm));
    assert(strstr(out, "\"uptime_us\":123"));
    assert(strstr(out, "\"pgn\":61444"));
    assert(strstr(out, "\"engine_rpm\":1000.125"));
    assert(strstr(out, "\"priority\":3"));
    assert(strstr(out, "\"source_address\":1"));
    assert(!strstr(out, "destination_address"));
    rpm[3] = 0;
    rpm[4] = 0xff;
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x0cf00401,
                                true, false, 8, rpm));
    assert(!strstr(out, "engine_rpm"));
    rpm[3] = 0xff;
    rpm[4] = 0xfa;
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x0cf00401,
                                true, false, 8, rpm));
    assert(strstr(out, "\"engine_rpm\":8031.875"));
    rpm[3] = 0;
    rpm[4] = 0xfb;
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x0cf00401,
                                true, false, 8, rpm));
    assert(!strstr(out, "engine_rpm"));
    uint8_t speed[8] = {0, 1, 100, 0, 0, 0, 0, 0};
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x18fef180,
                                true, false, 8, speed));
    assert(strstr(out, "\"vehicle_speed_kmh\":100.00390625"));
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x18ea2380,
                                true, false, 8, speed));
    assert(strstr(out, "\"pgn\":59904"));
    assert(strstr(out, "\"destination_address\":35"));
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x123,
                                false, false, 8, speed));
    assert(!strstr(out, "j1939"));
    assert(tb_protocol_can_json(out, sizeof(out), 0, 0x123,
                                false, true, 8, NULL));
    assert(strstr(out, "\"data\":\"\""));
    assert(!tb_protocol_can_json(out, sizeof(out), 0, 0x800,
                                 false, false, 8, speed));
    assert(!out[0]);
    assert(!tb_protocol_can_json(out, sizeof(out), 0, 1,
                                 false, false, 9, speed));
    assert(!tb_protocol_can_json(out, sizeof(out), 0, 1,
                                 false, false, 8, NULL));
    for (size_t capacity = 1; capacity < 100; ++capacity) {
        assert(!tb_protocol_can_json(out, capacity, 0, 0x0cf00401,
                                     true, false, 8, rpm));
        assert(!out[0]);
    }
}

static void test_j1708(void)
{
    char out[768];
    uint8_t frame[] = {128, 84, 42, 190, 0x40, 0x1f, 0};
    checksum(frame, sizeof(frame));
    assert(tb_protocol_j1708_checksum_valid(frame, sizeof(frame)));
    assert(tb_protocol_j1708_json(out, sizeof(out), 456, frame, sizeof(frame)));
    assert(strstr(out, "\"uptime_us\":456"));
    assert(strstr(out, "\"mid\":128"));
    assert(strstr(out, "\"j1587_layout_valid\":true"));
    assert(strstr(out, "{\"pid\":84,\"data\":\"2a\"}"));
    assert(strstr(out, "{\"pid\":190,\"data\":\"401f\"}"));
    assert(strstr(out, "\"speed_kmh\":33.796224"));
    assert(strstr(out, "\"engine_rpm\":2000.00"));
    ++frame[2];
    assert(!tb_protocol_j1708_checksum_valid(frame, sizeof(frame)));
    assert(!tb_protocol_j1708_json(out, sizeof(out), 0, frame, sizeof(frame)));
    assert(!out[0]);
    uint8_t variable[] = {128, 200, 3, 1, 2, 3, 40, 9, 0};
    checksum(variable, sizeof(variable));
    assert(tb_protocol_j1708_json(out, sizeof(out), 0,
                                  variable, sizeof(variable)));
    assert(strstr(out, "{\"pid\":200,\"data\":\"010203\"}"));
    assert(strstr(out, "{\"pid\":40,\"data\":\"09\"}"));
    uint8_t extended[] = {128, 255, 40, 9, 84, 1, 190, 0x40, 0x1f, 0};
    checksum(extended, sizeof(extended));
    assert(tb_protocol_j1708_json(out, sizeof(out), 0, extended, sizeof(extended)));
    assert(strstr(out, "{\"pid\":296,\"data\":\"09\"}"));
    assert(strstr(out, "{\"pid\":340,\"data\":\"01\"}"));
    assert(strstr(out, "{\"pid\":446,\"data\":\"401f\"}"));
    assert(!strstr(out, "speed_kmh"));
    assert(!strstr(out, "engine_rpm"));
    uint8_t misplaced_extension[] = {128, 84, 1, 255, 40, 9, 0};
    checksum(misplaced_extension, sizeof(misplaced_extension));
    assert(tb_protocol_j1708_json(out, sizeof(out), 0,
                                  misplaced_extension, sizeof(misplaced_extension)));
    assert(strstr(out, "\"j1587_layout_valid\":false"));
    assert(!strstr(out, "speed_kmh"));
    uint8_t incomplete[] = {128, 200, 8, 1, 2, 3, 0};
    checksum(incomplete, sizeof(incomplete));
    assert(tb_protocol_j1708_json(out, sizeof(out), 0,
                                  incomplete, sizeof(incomplete)));
    assert(strstr(out, "\"j1587_layout_valid\":false"));
    assert(!strstr(out, "parameters"));
    assert(strstr(out, "\"raw\":\""));
    uint8_t escaped[] = {128, 254, 1, 2, 0};
    checksum(escaped, sizeof(escaped));
    assert(tb_protocol_j1708_json(out, sizeof(out), 0,
                                  escaped, sizeof(escaped)));
    assert(strstr(out, "\"j1587_layout_valid\":false"));
    uint8_t no_data[] = {128, 128};
    assert(tb_protocol_j1708_json(out, sizeof(out), 0, no_data, sizeof(no_data)));
    assert(strstr(out, "\"j1587_layout_valid\":false"));
    uint8_t too_long[22] = {0};
    assert(!tb_protocol_j1708_checksum_valid(too_long, sizeof(too_long)));
    assert(!tb_protocol_j1708_checksum_valid(frame, 1));
    assert(!tb_protocol_j1708_checksum_valid(NULL, 5));
    assert(!tb_protocol_j1708_json(out, 8, 0, variable, sizeof(variable)));
    assert(!out[0]);
}

static void test_j1587_signals(void)
{
    char out[768];
    uint8_t maximum[] = {128, 84, 255, 190, 255, 255, 0};
    checksum(maximum, sizeof(maximum));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, maximum, sizeof(maximum)));
    assert(!strstr(out, "speed_kmh"));
    assert(!strstr(out, "engine_rpm"));
    assert(strstr(out, "\"pid\":84,\"data\":\"ff\""));
    assert(strstr(out, "\"pid\":190,\"data\":\"ffff\""));

    uint8_t unavailable[] = {128,183,255,255,245,4,255,255,255,255,0};
    checksum(unavailable, sizeof(unavailable));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, unavailable, sizeof(unavailable)));
    assert(!strstr(out, "fuel_rate_lph") && !strstr(out, "odometer_km"));
    assert(strstr(out, "\"raw\":\"") && strstr(out, "\"pid\":245"));

    uint8_t fuel[] = {128, 183, 1, 0, 0};
    checksum(fuel, sizeof(fuel));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, fuel, sizeof(fuel)));
    assert(strstr(out, "\"fuel_rate_lph\":0.059147059"));
    fuel[2] = 0;
    fuel[3] = 1;
    checksum(fuel, sizeof(fuel));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, fuel, sizeof(fuel)));
    assert(strstr(out, "\"fuel_rate_lph\":15.141647136"));

    uint8_t distance[] = {128, 245, 4, 2, 1, 0, 0, 0};
    checksum(distance, sizeof(distance));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, distance, sizeof(distance)));
    assert(strstr(out, "\"odometer_km\":41.5210752"));
    uint8_t short_distance[] = {128, 245, 3, 2, 1, 0, 0};
    checksum(short_distance, sizeof(short_distance));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, short_distance, sizeof(short_distance)));
    assert(strstr(out, "\"j1587_layout_valid\":true"));
    assert(!strstr(out, "odometer_km"));

    uint8_t proprietary[] = {17, 84, 255, 190, 255, 255, 0};
    checksum(proprietary, sizeof(proprietary));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, proprietary, sizeof(proprietary)));
    assert(strstr(out, "\"raw\":\""));
    assert(!strstr(out, "speed_kmh"));
    assert(!strstr(out, "engine_rpm"));

    uint8_t incomplete[] = {128, 84, 1, 190, 1, 0};
    checksum(incomplete, sizeof(incomplete));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, incomplete, sizeof(incomplete)));
    assert(strstr(out, "\"j1587_layout_valid\":false"));
    assert(!strstr(out, "speed_kmh"));
    assert(!strstr(out, "engine_rpm"));
    uint8_t extended_speed[] = {128, 255, 84, 255, 0};
    checksum(extended_speed, sizeof(extended_speed));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, extended_speed, sizeof(extended_speed)));
    assert(!strstr(out, "speed_kmh"));
    /* Duplicate PIDs retain their raw values but yield one unambiguous key. */
    uint8_t repeated[] = {128, 84, 1, 84, 2, 0};
    checksum(repeated, sizeof(repeated));
    assert(tb_protocol_j1708_json(out, sizeof(out), 1, repeated, sizeof(repeated)));
    const char *first = strstr(out, "\"speed_kmh\":0.804672");
    assert(first && !strstr(first + 1, "\"speed_kmh\""));
}

static uint32_t random_word(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void emit_json_cases(void)
{
    uint32_t state = 0x9173ba2e;
    char out[768];
    for (size_t i = 0; i < 2000; ++i) {
        uint8_t data[21];
        for (size_t j = 0; j < sizeof(data); ++j) {
            data[j] = (uint8_t)random_word(&state);
        }
        bool extended = (i & 1) != 0;
        uint32_t identifier = random_word(&state) &
                              (extended ? 0x1fffffffU : 0x7ffU);
        assert(tb_protocol_can_json(out, sizeof(out), UINT64_MAX - i,
                                    identifier, extended, (i % 3) == 0,
                                    (uint8_t)(i % 9), data));
        puts(out);
        size_t length = 2 + (i % 20);
        checksum(data, length);
        assert(tb_protocol_j1708_json(out, sizeof(out), i, data, length));
        puts(out);
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--json") == 0) {
        emit_json_cases();
        return 0;
    }
    test_can();
    test_j1708();
    test_j1587_signals();
    puts("Protocol tests passed");
    return 0;
}
