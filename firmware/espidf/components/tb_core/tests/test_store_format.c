#include "store_internal.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void test_record_crc(void)
{
    static const char canonical[] = "{\"rpm\":1000.125,\"text\":\"x,\\\"data\\\":y\"}";
    assert(tb_store_crc32("123456789", 9) == UINT32_C(0xcbf43926));
    uint32_t crc = tb_store_record_crc(256, 1234567, canonical, strlen(canonical));
    assert(crc != tb_store_record_crc(257, 1234567, canonical, strlen(canonical)));
    assert(crc != tb_store_record_crc(256, 1234566, canonical, strlen(canonical)));
    assert(crc != tb_store_record_crc(256, 1234567, canonical, strlen(canonical) - 1));
    char line[1024];
    int length = snprintf(line, sizeof(line),
        "{\"seq\":256,\"uptime_us\":1234567,\"crc32\":%" PRIu32 ",\"data\":%s}\n",
        crc, canonical);
    assert(length > 0);
    const char *payload = NULL;
    size_t payload_length = 0;
    assert(tb_store_payload_span(line, (size_t)length, &payload, &payload_length));
    assert(payload_length == strlen(canonical));
    assert(memcmp(payload, canonical, payload_length) == 0);
    assert(tb_store_record_crc(256, 1234567, payload, payload_length) == crc);
    /* Every interrupted prefix is rejected until the complete newline exists. */
    for (size_t end = 0; end < (size_t)length; ++end) {
        assert(!tb_store_payload_span(line, end, &payload, &payload_length));
    }
    char damaged[sizeof(line)];
    memcpy(damaged, line, (size_t)length + 1);
    damaged[length - 2] = 'x';
    assert(!tb_store_payload_span(damaged, (size_t)length, &payload, &payload_length));
    assert(!tb_store_payload_span(NULL, 100, &payload, &payload_length));
}

int main(void)
{
    test_record_crc();
    puts("Storage record CRC and interrupted-tail tests passed");
    return 0;
}
