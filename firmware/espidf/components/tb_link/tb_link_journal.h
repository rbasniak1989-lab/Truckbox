#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TB_LITE_JOURNAL_LIMIT (1024u * 1024u)
#define TB_LITE_JOURNAL_SEGMENT (16u * 1024u)
#define TB_LITE_JOURNAL_SLOTS (TB_LITE_JOURNAL_LIMIT / TB_LITE_JOURNAL_SEGMENT)
#define TB_LITE_BATCH_MAX 8u

typedef struct {
    uint64_t first, last, head_ms;
    uint32_t bytes, head, last_crc;
    uint16_t pending;
} tb_lite_segment_t;

typedef struct {
    char directory[96];
    uint32_t owner;
    uint32_t capacity, bytes, pending;
    uint32_t dropped_operational, dropped_health, recovered_tail_bytes;
    uint64_t sequence, acknowledged, revision;
    bool ready;
    tb_lite_segment_t segment[TB_LITE_JOURNAL_SLOTS];
} tb_lite_journal_t;

typedef struct {
    uint64_t sequence, captured_ms;
    uint32_t begin, end;
    uint16_t slot;
    bool health;
} tb_lite_ticket_t;

/* Serialized by the caller. The only deletion is of fully acknowledged segments.
 * Peek tickets remain valid while new records are appended. */
int tb_lite_journal_open(tb_lite_journal_t *journal, const char *directory, uint32_t owner, uint32_t capacity);
int tb_lite_journal_append(tb_lite_journal_t *journal, uint64_t sequence, uint64_t captured_ms,
                           bool health, const char *json);
int tb_lite_journal_read(tb_lite_journal_t *journal, const tb_lite_ticket_t *previous, tb_lite_ticket_t *ticket,
                         char *json, size_t capacity);
int tb_lite_journal_commit(tb_lite_journal_t *journal, const tb_lite_ticket_t *tickets, size_t count);
int tb_lite_journal_drop(tb_lite_journal_t *journal, uint32_t operational, uint32_t health);
uint64_t tb_lite_journal_oldest(const tb_lite_journal_t *journal);
