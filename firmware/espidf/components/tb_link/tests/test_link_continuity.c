#include "tb_link_journal.h"
#include "tb_link_telemetry_codec.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OWNER 123u
#define FIRST UINT64_C(1000000)
#define RECORDS_PER_MINUTE 7u
#define TEST_LIMIT 10000u

static const uint64_t epoch = TB_LITE_EPOCH_MS + UINT64_C(90900000000);

static void sample(unsigned index, bool dense, char json[TB_LITE_RECORD_MAX], uint64_t *captured, bool *health)
{
    unsigned slot = index % RECORDS_PER_MINUTE;
    *health = slot == 6;
    *captured = epoch + (uint64_t)(index / RECORDS_PER_MINUTE) * 60000 + slot * 10000;
    double hours = (double)(*captured - epoch) / 3600000;
    tb_lite_snapshot_t core = {.uid = "tb-c6-test123456", .boot_id=4093039421, .local_seq = 8254500 + index,
        .valid = TB_LITE_ALL, .uptime_ms=205999456 + index * UINT64_C(10000), .age_ms=531,
        .frames = 40651432 + index, .sent = 1229921, .answered = 1229893, .timeouts = 28,
        .heap_minimum = 138108, .guard = 3, .reset_reason=1, .reset_reason_valid=true};
    for (unsigned i=0; i<TB_LITE_FIELDS; ++i)
        core.values[i] = dense ? 1234.1234567 + i : 12.34 + i;
    core.values[TB_LITE_SPEED] = 76.7890123;
    core.values[TB_LITE_GEAR] = 12;
    core.values[TB_LITE_SELECTED_GEAR] = 12;
    core.values[TB_LITE_ODOMETER] = 1204000.1987 + hours * 76.781;
    core.values[TB_LITE_PID250] = 988632.80834 + hours * 36.634;
    tb_lite_reference_t ref = {.started_ms = epoch, .odometer_ms = epoch, .pid250_ms = epoch,
        .odometer = 1204000.1987, .pid250 = 988632.80834};
    tb_lite_link_health_t link = {.link_boot_id = 1118, .link_reset_reason = 1, .heap_free = 176400,
        .heap_minimum = 120015, .largest_block = 132000, .link_uptime_ms = core.uptime_ms,
        .pending = index, .capacity = TB_LITE_JOURNAL_LIMIT, .used = index * 320,
        .oldest_age_valid = true, .oldest_age = index * 10, .wifi_connected = true, .wifi_rssi = -78,
        .internet_reachable = -1, .cloud_last_success_ms = epoch - 10, .consecutive_failures = index,
        .disconnect_reason = "core_http_timeout", .cloud_error_stage = "http_timeout", .poll_failures = index / 11,
        .link_disconnects = index / 41};
    size_t length = *health ? tb_lite_health(json, TB_LITE_RECORD_MAX, FIRST + index,
        *captured, &core, true, &link, TB_LITE_NO_EVENT) :
        tb_lite_operational(json, TB_LITE_RECORD_MAX, FIRST + index, *captured, &core, &ref);
    assert(length);
}

static void cleanup(const char *dir)
{
    char path[112];
    for (unsigned i = 0; i < TB_LITE_JOURNAL_SLOTS; ++i) {
        snprintf(path, sizeof(path), "%s/d%02u.tbl", dir, i);
        if (unlink(path) != 0) assert(errno == ENOENT);
    }
    for (unsigned i = 0; i < 2; ++i) {
        snprintf(path, sizeof(path), "%s/ack%u.tbl", dir, i);
        if (unlink(path) != 0) assert(errno == ENOENT);
    }
    assert(rmdir(dir) == 0);
}

static int append(tb_lite_journal_t *journal, unsigned index, bool dense)
{
    char json[TB_LITE_RECORD_MAX];
    uint64_t captured;
    bool health;
    sample(index, dense, json, &captured, &health);
    return tb_lite_journal_append(journal, FIRST + index, captured, health, json);
}

static unsigned offer(tb_lite_journal_t *journal, tb_lite_ticket_t *tickets, bool dense, unsigned *health)
{
    *health = 0;
    unsigned count = 0;
    for (; count < TB_LITE_BATCH_MAX; ++count) {
        char json[TB_LITE_RECORD_MAX], expected[TB_LITE_RECORD_MAX];
        int error = tb_lite_journal_read(journal, count ? &tickets[count - 1] : NULL,
                                       &tickets[count], json, sizeof(json));
        if (error == ENOENT) break;
        assert(error == 0);
        uint64_t captured;
        bool kind;
        sample((unsigned)(tickets[count].sequence - FIRST), dense, expected, &captured, &kind);
        assert(tickets[count].captured_ms == captured && tickets[count].health == kind);
        assert(!strcmp(json, expected));
        *health += kind;
    }
    return count;
}

static void acknowledge(tb_lite_journal_t *journal, const tb_lite_ticket_t *tickets,
                        unsigned count, unsigned health)
{
    char ack[160];
    uint64_t sequence = tickets[count - 1].sequence;
    snprintf(ack, sizeof(ack), "{\"ok\":true,\"ack_seq\":%" PRIu64 ",\"accepted\":%u,\"health\":%u,\"events\":0}",
             sequence, count - health, health);
    assert(!tb_lite_ack_valid(ack, strlen(ack), 503, sequence, count - health, health));
    assert(!tb_lite_ack_valid(ack, strlen(ack), 200, sequence, count - health, health + 1));
    assert(tb_lite_ack_valid(ack, strlen(ack), 200, sequence, count - health, health));
    uint32_t pending = journal->pending;
    assert(tb_lite_journal_commit(journal, tickets, count) == 0);
    assert(journal->pending == pending - count && journal->acknowledged == sequence);
    assert(tb_lite_journal_commit(journal, tickets, count) == EINVAL);
}

static void shutdown_mid_write(tb_lite_journal_t *journal, const char *dir)
{
    for (unsigned i = 0; i < TB_LITE_JOURNAL_SLOTS; ++i) {
        if (journal->segment[i].last != journal->sequence) continue;
        char path[112];
        snprintf(path, sizeof(path), "%s/d%02u.tbl", dir, i);
        FILE *f = fopen(path, "ab");
        assert(f && fwrite("torn header", 1, 11, f) == 11 && fclose(f) == 0);
        return;
    }
    assert(false);
}

static void outage(unsigned minutes, bool dense, bool cold_reboot)
{
    char dir[] = "/tmp/tblite-continuity.XXXXXX";
    assert(mkdtemp(dir));
    tb_lite_journal_t journal;
    assert(tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT) == 0);
    unsigned expected = minutes * RECORDS_PER_MINUTE;
    for (unsigned i=0; i<expected; ++i) {
        int error = append(&journal, i, dense);
        if (error) fprintf(stderr, "outage %u min: index %u, bytes %" PRIu32 ", pending %" PRIu32 ", errno %d\n",
                           minutes, i, journal.bytes, journal.pending, error);
        assert(!error);
        if (cold_reboot && i == expected / 2) {
            shutdown_mid_write(&journal, dir);
            assert(tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT) == 0);
            assert(journal.recovered_tail_bytes == 11 && journal.pending == i + 1);
        }
    }
    assert(journal.pending == expected && tb_lite_journal_oldest(&journal) == epoch);
    unsigned initial_bytes = journal.bytes, batches = 0, accepted = 0;
    while (journal.pending) {
        tb_lite_ticket_t tickets[TB_LITE_BATCH_MAX];
        unsigned health;
        unsigned count = offer(&journal, tickets, dense, &health);
        assert(count && tickets[0].sequence == FIRST + accepted);
        acknowledge(&journal, tickets, count, health);
        accepted += count;
        ++batches;
    }
    assert(accepted == expected && journal.acknowledged == FIRST + expected - 1);
    assert(tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT) == 0 && !journal.pending);
    printf("Offline %u min%s: %" PRIu32 " B, %u registros íntegros, %u POSTs no replay, 0 perdas/duplicatas\n",
           minutes, cold_reboot ? " com reboot e corte da cauda" : "", initial_bytes, accepted, batches);
    cleanup(dir);
}

static void interrupted_delivery(void)
{
    char dir[] = "/tmp/tblite-retry.XXXXXX";
    assert(mkdtemp(dir));
    tb_lite_journal_t journal;
    assert(!tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT));
    unsigned made = 0, next = 0, duplicate = 0, unique = 0;
    bool received[TEST_LIMIT] = {0};
    for (unsigned round = 0; round < 40; ++round) {
        for (unsigned i = 0; i < 15; ++i) assert(!append(&journal, made++, false));
        while (journal.pending) {
            tb_lite_ticket_t tickets[TB_LITE_BATCH_MAX];
            unsigned health;
            unsigned count = offer(&journal, tickets, false, &health);
            assert(tickets[0].sequence == FIRST + next);
            if (journal.pending > 4) assert(!append(&journal, made++, false)); /* read/HTTPS in flight */
            for (unsigned i=0; i<count; ++i) {
                unsigned index = (unsigned)(tickets[i].sequence - FIRST);
                assert(index < TEST_LIMIT);
                if (received[index]) ++duplicate;
                else { ++unique; received[index] = true; }
            }
            if (round % 3 == 0 && count > 1 && next % 7 == 0 && duplicate == 0) {
                /* Cloud persisted the batch, but power failed before local ACK. */
                assert(!tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT));
                tb_lite_ticket_t retry[TB_LITE_BATCH_MAX];
                unsigned retry_health;
                assert(offer(&journal, retry, false, &retry_health) == count);
                assert(retry[0].sequence == tickets[0].sequence && retry_health == health);
                continue;
            }
            acknowledge(&journal, tickets, count, health);
            next += count;
        }
    }
    assert(unique == made && next == made && duplicate > 0);
    printf("40 oscilações: %u registros únicos, %u reofertados após corte antes do ACK, fila vazia\n", unique, duplicate);
    cleanup(dir);
}

static void capacity(uint32_t budget)
{
    char dir[] = "/tmp/tblite-full.XXXXXX";
    assert(mkdtemp(dir));
    tb_lite_journal_t journal;
    assert(!tb_lite_journal_open(&journal, dir, OWNER, budget));
    unsigned index = 0, first_drop = 0, health_lost = 0, op_lost = 0, accepted = 0;
    unsigned max_op = 0, max_health = 0, max_op_binary = 0, max_health_binary = 0;
    while (index < TEST_LIMIT && !op_lost) {
        char json[TB_LITE_RECORD_MAX];
        uint64_t captured;
        bool health;
        sample(index, true, json, &captured, &health);
        unsigned before = journal.bytes;
        int result = tb_lite_journal_append(&journal, FIRST + index, captured, health, json);
        if (result == ENOSPC) {
            if (!first_drop) first_drop = index;
            assert(!tb_lite_journal_drop(&journal, !health, health));
            health_lost += health;
            op_lost += !health;
        } else {
            assert(!result && journal.bytes <= budget);
            unsigned binary = journal.bytes - before;
            if (health) {
                if (strlen(json) > max_health) max_health = (unsigned)strlen(json);
                if (binary > max_health_binary) max_health_binary = binary;
            } else {
                if (strlen(json) > max_op) max_op = (unsigned)strlen(json);
                if (binary > max_op_binary) max_op_binary = binary;
            }
            ++accepted;
        }
        ++index;
    }
    assert(first_drop > 0 && health_lost > 0 && op_lost == 1 && journal.pending == accepted);
    assert(!tb_lite_journal_open(&journal, dir, OWNER, budget) && journal.pending == accepted);
    assert(journal.dropped_health == health_lost && journal.dropped_operational == op_lost);
    tb_lite_ticket_t tickets[TB_LITE_BATCH_MAX];
    unsigned health;
    unsigned count = offer(&journal, tickets, true, &health);
    assert(count && tickets[0].sequence == FIRST); /* Oldest operational sample never evicted. */
    printf("Cota %" PRIu32 " KiB: primeira perda de saúde aos %.1f min, limite operacional %.1f min, "
           "%u pendentes, descartes persistidos %u saúde / %u operacional; JSON/flash máximos %u/%u B op e %u/%u B saúde\n",
           budget / 1024, (double)first_drop / 7, (double)(index - 1) / 7, accepted,
           health_lost, op_lost, max_op, max_op_binary, max_health, max_health_binary);
    cleanup(dir);
}

static void damaged_checkpoint_and_frame(void)
{
    char dir[] = "/tmp/tblite-integrity.XXXXXX", path[112];
    assert(mkdtemp(dir));
    tb_lite_journal_t journal;
    assert(!tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT));
    for (unsigned i=0; i<26; ++i) assert(!append(&journal, i, false));
    for (unsigned b=0; b<2; ++b) {
        tb_lite_ticket_t tickets[TB_LITE_BATCH_MAX];
        unsigned health;
        unsigned count = offer(&journal, tickets, false, &health);
        acknowledge(&journal, tickets, count, health);
    }
    assert(journal.revision == 2 && journal.pending == 10);
    snprintf(path, sizeof(path), "%s/ack0.tbl", dir);
    FILE *f = fopen(path, "wb");
    assert(f && fwrite("incomplete", 1, 10, f) == 10 && fclose(f) == 0);
    assert(!tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT));
    assert(journal.revision == 1 && journal.pending == 18); /* Replays only. */
    assert(tb_lite_journal_open(&journal, dir, OWNER + 1, TB_LITE_JOURNAL_LIMIT) == EPERM);
    snprintf(path, sizeof(path), "%s/d00.tbl", dir);
    f = fopen(path, "rb+");
    assert(f && !fseek(f, 40, SEEK_SET) && fputc(0xff, f) != EOF && fclose(f) == 0);
    struct stat before, after;
    assert(!stat(path, &before));
    assert(tb_lite_journal_open(&journal, dir, OWNER, TB_LITE_JOURNAL_LIMIT) == EBADMSG);
    assert(!stat(path, &after) && before.st_size == after.st_size); /* Preserve complete corrupt frame. */
    cleanup(dir);
}

static void contact_recovery(void)
{
    tb_lite_contact_t contact = {0};
    assert(tb_lite_contact_update(&contact, 0) == TB_LITE_NO_EVENT);
    assert(tb_lite_contact_update(&contact, 801) == TB_LITE_CORE_SEEN);
    assert(tb_lite_contact_update(&contact, 0) == TB_LITE_NO_EVENT);
    assert(tb_lite_contact_update(&contact, 801) == TB_LITE_NO_EVENT);
    assert(tb_lite_contact_update(&contact, 0) == TB_LITE_NO_EVENT);
    assert(tb_lite_contact_update(&contact, 0) == TB_LITE_NO_EVENT);
    assert(tb_lite_contact_update(&contact, 0) == TB_LITE_CORE_LOST && contact.disconnected);
    assert(tb_lite_contact_update(&contact, 801) == TB_LITE_CORE_RESTORED && !contact.disconnected);
    assert(tb_lite_contact_update(&contact, 802) == TB_LITE_CORE_BOOT);
    assert(tb_lite_retry_delay(1, 0) == 2000 && tb_lite_retry_delay(100, 0) == 30000 &&
           tb_lite_retry_delay(10, 429) == 60000 && tb_lite_retry_delay(0, 200) == 1000);
}

int main(void)
{
    outage(15, false, false);
    outage(60, true, true);
    outage(360, true, true);
    interrupted_delivery();
    capacity(512 * 1024);
    capacity(TB_LITE_JOURNAL_LIMIT);
    damaged_checkpoint_and_frame();
    contact_recovery();
    printf("Índice de segmentos em RAM: %zu B fixos; CRC, ACK alternado, lotes e discriminação de boot PASS\n",
           sizeof(tb_lite_journal_t));
}
