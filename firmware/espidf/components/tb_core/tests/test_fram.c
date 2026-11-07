/*
 * Portable tests for the on-FRAM journal encoding, without an ESP-IDF runtime.
 * Test mode excludes SPI/RTOS code but retains the production codec.
 * Electrical detection and SPI readback still require the real board.
 */
#define TB_FRAM_HOST_TEST
#include "../fram.c"

#include <assert.h>
#include <stdio.h>

static void test_golden_encoding(void)
{
    uint8_t slot[FRAM_SLOT_BYTES];
    fram_metadata_t input = { .generation = 42, .reserved_end = 1000, .ack = 999 };
    fram_metadata_t output;
    metadata_encode(slot, &input);
    /* Independently calculated with Python struct and binascii in run_fram.sh. */
    assert(read_le32(slot + 60) == UINT32_C(0x0bdcf881));
    assert(memcmp(slot, "TBFR", 4) == 0);
    assert(slot[4] == 1 && slot[5] == 32 && slot[6] == 0 && slot[7] == 0);
    assert(slot[8] == 42 && slot[9] == 0);
    assert(slot[16] == 0xe8 && slot[17] == 3 && slot[23] == 0);
    assert(slot[24] == 0xe7 && slot[25] == 3 && slot[31] == 0);
    assert(metadata_decode(slot, &output));
    assert(output.generation == 42);
    assert(output.reserved_end == 1000);
    assert(output.ack == 999);
}

static void test_corruptions(void)
{
    uint8_t slot[FRAM_SLOT_BYTES];
    fram_metadata_t input = { .generation = 42, .reserved_end = 1000, .ack = 999 };
    metadata_encode(slot, &input);
    for (unsigned bit = 0; bit < FRAM_SLOT_BYTES * 8; ++bit) {
        slot[bit / 8] ^= (uint8_t)(1U << (bit % 8));
        assert(!metadata_decode(slot, NULL));
        slot[bit / 8] ^= (uint8_t)(1U << (bit % 8));
    }
    assert(metadata_decode(slot, NULL));
    memset(slot, 0xff, sizeof(slot));
    assert(!metadata_decode(slot, NULL));
    memset(slot, 0, sizeof(slot));
    assert(!metadata_decode(slot, NULL));
}

static void test_metadata_bounds(void)
{
    uint8_t slot[FRAM_SLOT_BYTES];
    fram_metadata_t value = { .generation = 0, .reserved_end = 0, .ack = 0 };
    metadata_encode(slot, &value);
    assert(!metadata_decode(slot, NULL));
    value.reserved_end = FRAM_MAX_SEQUENCE + 1;
    metadata_encode(slot, &value);
    assert(!metadata_decode(slot, NULL));
    value.reserved_end = 10;
    value.ack = 11;
    metadata_encode(slot, &value);
    assert(!metadata_decode(slot, NULL));
    value.reserved_end = FRAM_MAX_SEQUENCE;
    value.ack = FRAM_MAX_SEQUENCE;
    metadata_encode(slot, &value);
    assert(metadata_decode(slot, NULL));
    value.reserved_end = 1;
    value.ack = 0;
    metadata_encode(slot, &value);
    assert(metadata_decode(slot, NULL));
}

static void test_generation_wrap(void)
{
    assert(generation_newer(0, UINT32_MAX));
    assert(generation_newer(1, UINT32_MAX));
    assert(!generation_newer(UINT32_MAX, 0));
    assert(!generation_newer(42, 42));
    assert(generation_newer(43, 42));
    assert(!generation_newer(42, 43));
}

static void test_interrupted_slot_replacement(void)
{
    uint8_t previous[FRAM_SLOT_BYTES], replacement[FRAM_SLOT_BYTES];
    uint8_t interrupted[FRAM_SLOT_BYTES];
    fram_metadata_t older = { .generation = 0, .reserved_end = 744, .ack = 0 };
    fram_metadata_t current = { .generation = 1, .reserved_end = 1000, .ack = 12 };
    fram_metadata_t newer = { .generation = 2, .reserved_end = 1256, .ack = 20 };
    metadata_encode(previous, &older);
    metadata_encode(replacement, &newer);

    /*
     * The active slot is untouched. Power is lost at each byte count while the
     * other slot is overwritten. Only a complete, validated replacement may
     * supersede current. A still-intact older slot is harmless.
     */
    for (unsigned cut = 0; cut <= FRAM_SLOT_BYTES; ++cut) {
        memcpy(interrupted, previous, sizeof(interrupted));
        memcpy(interrupted, replacement, cut);
        fram_metadata_t candidate;
        bool valid = metadata_decode(interrupted, &candidate);
        uint32_t selected = valid && generation_newer(candidate.generation, current.generation)
                          ? candidate.generation : current.generation;
        assert(selected == (cut == FRAM_SLOT_BYTES ? 2U : 1U));
    }
}

static void test_nvs_reset_reservation_checkpoint(void)
{
    fram_metadata_t current = { .generation = 9, .reserved_end = 1000, .ack = 100 };
    assert(!checkpoint_needed(&current, 1000, 100));
    assert(!checkpoint_needed(&current, 1000, 0));
    assert(!checkpoint_needed(&current, 744, 101));
    assert(checkpoint_needed(&current, 1000, 101));

    /* The store recovers only the reservation floor from FRAM, never its ACK.
     * A new NVS-committed reservation must still mirror ACK=0 successfully. */
    assert(checkpoint_needed(&current, 1256, 0));
    fram_metadata_t recovered = { .generation = 10, .reserved_end = 1256, .ack = 0 };
    uint8_t slot[FRAM_SLOT_BYTES];
    metadata_encode(slot, &recovered);
    fram_metadata_t decoded;
    assert(metadata_decode(slot, &decoded));
    assert(generation_newer(decoded.generation, current.generation));
    assert(decoded.reserved_end == 1256 && decoded.ack == 0);
    assert(!checkpoint_needed(&decoded, 1000, 100));
    assert(checkpoint_needed(&decoded, 1256, 101));
}

int main(void)
{
    test_golden_encoding();
    test_corruptions();
    test_metadata_bounds();
    test_generation_wrap();
    test_interrupted_slot_replacement();
    test_nvs_reset_reservation_checkpoint();
    puts("FRAM codec tests passed: golden CRC, 512 corruptions, sequence bounds, generation wrap, 65 interrupted writes, NVS-reset checkpoint");
    return 0;
}
