#define _POSIX_C_SOURCE 200809L
#include "tb_gps_outbox.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void point(char *buffer, size_t capacity, unsigned id)
{
    snprintf(buffer, capacity, "{\"client_uid\":\"hardware:%u\",\"lat\":-23.55,\"lon\":-46.63}", id);
}

int main(void)
{
    char directory[] = "/tmp/tb-gps-outbox-XXXXXX";
    assert(mkdtemp(directory));
    char path[128], backup[140];
    snprintf(path, sizeof(path), "%s/points", directory);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    tb_gps_outbox_t box;
    assert(tb_gps_outbox_init(&box, path, 4096) == 0);
    char json[TB_GPS_OUTBOX_JSON_MAX], expected[TB_GPS_OUTBOX_JSON_MAX];
    for (unsigned i = 0; i < 20; ++i) {
        point(json, sizeof(json), i);
        assert(tb_gps_outbox_append(&box, json) == 0);
    }
    assert(tb_gps_outbox_pending(&box) == 20);
    for (unsigned i = 0; i < 10; ++i) {
        assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
        point(expected, sizeof(expected), i);
        assert(strcmp(json, expected) == 0);
        assert(tb_gps_outbox_ack(&box) == 0);
    }
    assert(tb_gps_outbox_init(&box, path, 4096) == 0);
    /* ACKs 0..7 were checkpointed. 8 and 9 are conservatively replayed. */
    assert(tb_gps_outbox_pending(&box) == 12);
    assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
    point(expected, sizeof(expected), 8);
    assert(strcmp(json, expected) == 0);
    tb_gps_outbox_retry(&box);
    assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0 && strcmp(json, expected) == 0);
    assert(tb_gps_outbox_ack(&box) == 0);
    tb_gps_outbox_retry(&box);
    FILE *file = fopen(path, "ab");
    assert(file);
    fputs("P 12345678 {\"incomplete", file);
    fclose(file);
    assert(tb_gps_outbox_init(&box, path, 4096) == 0);
    assert(box.recovered_tail_bytes > 0);
    assert(tb_gps_outbox_pending(&box) == 12);
    /* Compaction retains pending data; an in-flight point prevents moving offsets. */
    for (unsigned i = 8; i < 20; ++i) {
        assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
        assert(tb_gps_outbox_ack(&box) == 0);
    }
    for (unsigned i = 20; i < 120; ++i) {
        point(json, sizeof(json), i);
        assert(tb_gps_outbox_append(&box, json) == 0);
        assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
        assert(tb_gps_outbox_ack(&box) == 0);
        assert(box.file_bytes <= box.maximum_bytes);
    }
    assert(rename(path, backup) == 0);
    assert(tb_gps_outbox_init(&box, path, 4096) == 0);
    assert(box.ready);
    while (tb_gps_outbox_pending(&box)) {
        assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
        assert(tb_gps_outbox_ack(&box) == 0);
    }
    /* Once the unacknowledged queue is full, new points are rejected, not old points discarded. */
    unsigned appended = 0;
    for (;;) {
        point(json, sizeof(json), appended + 200);
        int result = tb_gps_outbox_append(&box, json);
        if (result == ENOSPC) break;
        assert(result == 0);
        appended++;
    }
    assert(appended > 0 && box.rejected_records == 1);
    assert(tb_gps_outbox_pending(&box) == appended);
    assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
    point(expected, sizeof(expected), 200);
    assert(strcmp(json, expected) == 0);
    tb_gps_outbox_retry(&box);
    /* A complete line with a bad CRC is a fault, not permission to reformat/drop the queue. */
    file = fopen(path, "r+b");
    assert(file && fseek(file, 12, SEEK_SET) == 0);
    fputc('X', file);
    fclose(file);
    assert(tb_gps_outbox_init(&box, path, 4096) == EINVAL);
    assert(!box.ready);
    unlink(path);
    /* Power loss during an ACK checkpoint safely replays accepted points. */
    assert(tb_gps_outbox_init(&box, path, 4096) == 0);
    for (unsigned i = 0; i < 8; ++i) {
        point(json, sizeof(json), i);
        assert(tb_gps_outbox_append(&box, json) == 0);
    }
    for (unsigned i = 0; i < 8; ++i) {
        assert(tb_gps_outbox_peek(&box, json, sizeof(json)) == 0);
        assert(tb_gps_outbox_ack(&box) == 0);
    }
    struct stat information;
    assert(stat(path, &information) == 0 && information.st_size > 10);
    assert(truncate(path, information.st_size - 5) == 0);
    assert(tb_gps_outbox_init(&box, path, 4096) == 0);
    assert(box.recovered_tail_bytes > 0 && tb_gps_outbox_pending(&box) == 8);
    unlink(path);
    unlink(backup);
    rmdir(directory);
    puts("GPS outbox tests passed");
    return 0;
}
