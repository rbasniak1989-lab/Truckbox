#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TB_GPS_OUTBOX_JSON_MAX 640

typedef struct {
    char path[128];
    size_t maximum_bytes;
    uint64_t file_bytes;
    uint64_t read_offset;
    uint64_t total_records;
    uint64_t delivered_records;
    uint64_t peek_end;
    unsigned acknowledgements_since_checkpoint;
    uint64_t recovered_tail_bytes;
    uint64_t rejected_records;
    bool ready;
} tb_gps_outbox_t;

/* Caller serializes operations. Return zero or a POSIX errno value. */
int tb_gps_outbox_init(tb_gps_outbox_t *box, const char *path, size_t maximum_bytes);
int tb_gps_outbox_append(tb_gps_outbox_t *box, const char *json);
int tb_gps_outbox_peek(tb_gps_outbox_t *box, char *json, size_t capacity);
int tb_gps_outbox_ack(tb_gps_outbox_t *box);
void tb_gps_outbox_retry(tb_gps_outbox_t *box);
uint64_t tb_gps_outbox_pending(const tb_gps_outbox_t *box);
