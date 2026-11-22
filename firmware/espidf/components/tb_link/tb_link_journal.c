#define _POSIX_C_SOURCE 200809L
#include "tb_link_journal.h"
#include "tb_gps_outbox.h"
#include "tb_lite_wire.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SEGMENT_MAGIC UINT32_C(0x324a4c54)
#define CHECKPOINT_MAGIC UINT32_C(0x32414c54)
#define HEALTH_HEADROOM (64u * 1024u)
#define NO_SLOT TB_LITE_JOURNAL_SLOTS

/* Native little-endian, fixed-size layouts shared by ESP32-C6 and host tests. */
typedef struct {
    uint32_t magic, owner, crc;
} segment_header_t;

typedef struct {
    uint64_t revision, acknowledged;
    uint32_t magic, owner, dropped_operational, dropped_health, reserved, crc;
} checkpoint_t;

typedef struct {
    uint64_t sequence, captured_ms;
    uint32_t length, health;
    unsigned char data[TB_LITE_RECORD_MAX + sizeof(uint32_t)];
} frame_t;

#define FRAME_HEADER offsetof(frame_t, data)
_Static_assert(sizeof(segment_header_t) == 12, "segment header format");
_Static_assert(sizeof(checkpoint_t) == 40, "checkpoint format");
_Static_assert(FRAME_HEADER == 24, "record header format");

/* Tokens are part of the on-flash format. Append new tokens at the end.
 * 0x7f packs runs of numeric characters, 0x80 escapes reserved bytes. */
static const char *const tokens[] = {
    "\"seq\":", "\"sample_kind\":", "\"source\":", "\"OPERATIONAL\"", "\"REVB1_LINK_LITE\"",
    "\"boot_id\":", "\"core_seq\":", "\"uptime_ms\":", "\"time_valid\":", "\"unix_ms\":",
    "\"sensor_age_ms\":", "\"valid_mask\":", "\"stale_mask\":", "\"estimated_mask\":",
    "\"speed_kmh\":", "\"rpm\":", "\"pedal_pct\":", "\"gear\":", "\"selected_gear\":",
    "\"odometer_km\":", "\"odometer_provisional_km\":", "\"fuel_counter_l\":", "\"fuel_rate_lph\":",
    "\"torque_pct\":", "\"oil_pressure_bar\":", "\"fuel_pressure_bar\":", "\"boost_bar\":",
    "\"brake_air_primary_bar\":", "\"brake_air_secondary_bar\":", "\"coolant_temp_c\":",
    "\"oil_temp_c\":", "\"intake_temp_c\":", "\"gearbox_temp_c\":", "\"load_pct\":",
    "\"lite_reference_ms\":", "\"fuel_quality\":", "\"CONFIRMED_ECU_PID250\"",
    "\"ESTIMATE_FROM_RATE_PID183\"", "\"UNAVAILABLE\"", "\"trip_distance_km\":",
    "\"lite_start_odometer_km\":", "\"trip_fuel_l\":", "\"lite_pid250_reference_ms\":",
    "\"trip_average_kml\":", "\"HEALTH\"", "\"window_seconds\":", "\"metrics\":", "\"context\":",
    "\"link_heap_minimum\":", "\"pending\":", "\"dropped\":", "\"core_heap_minimum\":",
    "\"j1708_frames\":", "\"j1708_sent\":", "\"j1708_answered\":", "\"j1708_timeouts\":",
    "\"j1708_guard\":", "\"core_online\":", "\"core_boot_id\":", "\"event\":",
    "\"CORE_LOST\"", "\"CORE_RESTORED\"", "\"CORE_BOOT\"", "\"MINUTE\"",
    "\"link_heap_free\":", "\"link_heap_largest_block\":", "\"link_boot_id\":",
    "\"link_reset_reason\":", "\"core_reset_reason\":", "\"wifi_rssi\":", "\"wifi_connected\":",
    "\"internet_reachable\":", "\"cloud_connected\":", "\"cloud_last_success_ms\":",
    "\"cloud_last_http_status\":", "\"cloud_consecutive_failures\":", "\"offline_queue_capacity_bytes\":",
    "\"offline_queue_used_bytes\":", "\"offline_queue_pending_records\":",
    "\"offline_queue_oldest_age_seconds\":", "\"offline_queue_dropped_records\":",
    "\"offline_queue_replayed_records\":", "\"core_link_disconnect_count\":",
    "\"core_link_last_disconnect_reason\":", "\"cloud_last_error\":", "\"core_uid\":",
    "\"offline_time_quality\":", "\"BOOT_UPTIME_ONLY\"", "\"DECODED\"", "\"device_uid\":",
    "\"queue_dropped_operational\":", "\"queue_dropped_health\":", "\"core_poll_failures\":",
    "\"queue_recovered_tail_bytes\":", "\"link_uptime_ms\":", "\"cloud_error_stage\":"
};

#define TOKEN_COUNT (sizeof(tokens) / sizeof(tokens[0]))
_Static_assert(TOKEN_COUNT <= 127, "one-byte token space");
static const char numeric[] = "0123456789.-+eE";

static size_t pack(unsigned char *out, size_t capacity, const char *json)
{
    size_t used = 0;
    while (*json && used < capacity) {
        if (*json == '"') {
            size_t i;
            for (i = 0; i < TOKEN_COUNT; ++i) {
                size_t length = strlen(tokens[i]);
                if (strncmp(json, tokens[i], length) != 0) continue;
                out[used++] = (unsigned char)(0x81 + i);
                json += length;
                break;
            }
            if (i < TOKEN_COUNT) continue;
        }
        if ((*json >= '0' && *json <= '9') || *json == '-') {
            size_t n = 0;
            while (json[n] && n < 30 && strchr(numeric, json[n])) ++n;
            if (n >= 6) {
                if (capacity - used < 2 + (n + 1) / 2) return 0;
                out[used++] = 0x7f;
                out[used++] = (unsigned char)n;
                for (size_t i = 0; i < n; i += 2) {
                    unsigned high = (unsigned)(strchr(numeric, json[i]) - numeric);
                    unsigned low = i + 1 < n ? (unsigned)(strchr(numeric, json[i + 1]) - numeric) : 15u;
                    out[used++] = (unsigned char)((high << 4) | low);
                }
                json += n;
                continue;
            }
        }
        unsigned char value = (unsigned char)*json++;
        if (value >= 0x7f) {
            if (capacity - used < 2) return 0;
            out[used++] = 0x80;
        }
        out[used++] = value;
    }
    return *json ? 0 : used;
}

static int unpack(char *out, size_t capacity, const unsigned char *data, size_t length)
{
    size_t used = 0;
    if (!capacity) return ENOBUFS;
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = data[i];
        if (value > 0x80) {
            size_t token = value - 0x81;
            if (token >= TOKEN_COUNT) return EBADMSG;
            size_t n = strlen(tokens[token]);
            if (n >= capacity - used) return ENOBUFS;
            memcpy(out + used, tokens[token], n);
            used += n;
        } else if (value == 0x7f) {
            if (++i >= length) return EBADMSG;
            size_t n = data[i], packed = (n + 1) / 2;
            if (n < 6 || n > 30 || packed >= length - i) return EBADMSG;
            if (n >= capacity - used) return ENOBUFS;
            for (size_t k = 0; k < n; ++k) {
                unsigned char digits = data[i + 1 + k / 2];
                unsigned digit = k % 2 ? digits & 15u : digits >> 4;
                if (digit >= sizeof(numeric) - 1) return EBADMSG;
                out[used++] = numeric[digit];
            }
            i += packed;
        } else {
            if (value == 0x80) {
                if (++i >= length) return EBADMSG;
                value = data[i];
            }
            if (!value) return EBADMSG;
            if (capacity - used < 2) return ENOBUFS;
            out[used++] = (char)value;
        }
    }
    out[used] = '\0';
    return 0;
}

static int finish_write(FILE *file, int error)
{
    if (!error && (fflush(file) != 0 || fsync(fileno(file)) != 0)) error = errno ? errno : EIO;
    if (fclose(file) != 0 && !error) error = errno ? errno : EIO;
    return error;
}

static void segment_path(const tb_lite_journal_t *j, unsigned slot, char path[112])
{
    snprintf(path, 112, "%s/d%02u.tbl", j->directory, slot);
}

static void checkpoint_path(const tb_lite_journal_t *j, unsigned copy, char path[112])
{
    snprintf(path, 112, "%s/ack%u.tbl", j->directory, copy);
}

static int load_checkpoint(const tb_lite_journal_t *j, unsigned copy, checkpoint_t *v)
{
    char path[112];
    checkpoint_path(j, copy, path);
    FILE *file = fopen(path, "rb");
    if (!file) return errno;
    bool entire = fread(v, 1, sizeof(*v), file) == sizeof(*v) && fgetc(file) == EOF;
    bool failed = ferror(file);
    fclose(file);
    if (failed) return EIO;
    if (!entire || v->magic != CHECKPOINT_MAGIC || !v->revision ||
        v->acknowledged > TB_LITE_SAFE_SEQ ||
        tb_outbox_crc32((const unsigned char *)v, offsetof(checkpoint_t, crc)) != v->crc) return EBADMSG;
    return v->owner == j->owner ? 0 : EPERM;
}

static int save_checkpoint(tb_lite_journal_t *j, uint64_t ack, uint32_t lost, uint32_t health_lost)
{
    checkpoint_t v = {.magic = CHECKPOINT_MAGIC, .owner = j->owner, .revision = j->revision + 1,
        .acknowledged = ack, .dropped_operational = lost, .dropped_health = health_lost};
    v.crc = tb_outbox_crc32((const unsigned char *)&v, offsetof(checkpoint_t, crc));
    char path[112];
    checkpoint_path(j, v.revision & 1u, path);
    FILE *file = fopen(path, "wb");
    if (!file) return errno;
    int error = finish_write(file, fwrite(&v, 1, sizeof(v), file) == sizeof(v) ? 0 : EIO);
    if (error) return error;
    j->revision = v.revision;
    j->acknowledged = ack;
    j->dropped_operational = lost;
    j->dropped_health = health_lost;
    return 0;
}

static int read_frame(FILE *file, frame_t *frame, uint32_t *crc)
{
    size_t n = fread(frame, 1, FRAME_HEADER, file);
    if (ferror(file)) return EIO;
    if (!n) return ENOENT;
    if (n != FRAME_HEADER) return ENODATA;
    if (!frame->sequence || frame->sequence > TB_LITE_SAFE_SEQ || frame->health > 1 ||
        (frame->captured_ms && (frame->captured_ms < TB_LITE_EPOCH_MS || frame->captured_ms > TB_LITE_SAFE_SEQ)) ||
        !frame->length || frame->length >= TB_LITE_RECORD_MAX) return EBADMSG;
    size_t length = frame->length + sizeof(*crc);
    if (fread(frame->data, 1, length, file) != length) return ferror(file) ? EIO : ENODATA;
    memcpy(crc, frame->data + frame->length, sizeof(*crc));
    return tb_outbox_crc32((const unsigned char *)frame, FRAME_HEADER + frame->length) == *crc ? 0 : EBADMSG;
}

static int recover_tail(tb_lite_journal_t *j, const char *path, uint32_t valid_end)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) return errno;
    struct stat st;
    int error = 0;
    if (fstat(fd, &st) != 0 || ftruncate(fd, valid_end) != 0 || fsync(fd) != 0)
        error = errno ? errno : EIO;
    else if (st.st_size > (off_t)valid_end)
        j->recovered_tail_bytes += (uint32_t)(st.st_size - valid_end);
    close(fd);
    return error;
}

static int scan_segment(tb_lite_journal_t *j, unsigned slot)
{
    char path[112];
    segment_path(j, slot, path);
    FILE *file = fopen(path, "rb");
    if (!file) return errno == ENOENT ? 0 : errno;
    segment_header_t header;
    size_t got_header = fread(&header, 1, sizeof(header), file);
    if (got_header != sizeof(header)) {
        bool failed = ferror(file);
        fclose(file);
        if (failed) return EIO;
        /* An incomplete segment header cannot contain any committed frame. */
        j->recovered_tail_bytes += (uint32_t)got_header;
        return unlink(path) == 0 ? 0 : errno;
    }
    if (header.magic != SEGMENT_MAGIC || header.owner != j->owner ||
        header.crc != tb_outbox_crc32((const unsigned char *)&header, offsetof(segment_header_t, crc))) {
        fclose(file);
        return EBADMSG;
    }
    tb_lite_segment_t *segment = &j->segment[slot];
    segment->bytes = sizeof(header);
    frame_t frame;
    uint32_t crc;
    int error;
    while ((error = read_frame(file, &frame, &crc)) == 0) {
        uint32_t record_bytes = (uint32_t)(FRAME_HEADER + frame.length + sizeof(crc));
        if (frame.sequence <= segment->last || segment->bytes + record_bytes > TB_LITE_JOURNAL_SEGMENT) {
            error = EBADMSG;
            break;
        }
        if (!segment->first) segment->first = frame.sequence;
        if (frame.sequence > j->acknowledged) {
            if (!segment->pending) {
                segment->head = segment->bytes;
                segment->head_ms = frame.captured_ms;
            }
            ++segment->pending;
        }
        segment->last = frame.sequence;
        segment->last_crc = crc;
        segment->bytes += record_bytes;
    }
    fclose(file);
    if (error == ENODATA) error = recover_tail(j, path, segment->bytes);
    if (error && error != ENOENT) return error;
    j->bytes += segment->bytes;
    j->pending += segment->pending;
    if (segment->last > j->sequence) j->sequence = segment->last;
    return 0;
}

static void reclaim_segment(tb_lite_journal_t *j, unsigned slot, bool keep_tail)
{
    tb_lite_segment_t *segment = &j->segment[slot];
    if (!segment->bytes || segment->pending ||
        (keep_tail && segment->last && segment->last == j->sequence)) return;
    char path[112];
    segment_path(j, slot, path);
    if (unlink(path) != 0 && errno != ENOENT) return;
    j->bytes -= segment->bytes;
    memset(segment, 0, sizeof(*segment));
}

int tb_lite_journal_open(tb_lite_journal_t *j, const char *directory, uint32_t owner, uint32_t capacity)
{
    if (!j || !directory || !*directory || strlen(directory) >= sizeof(j->directory) ||
        capacity > TB_LITE_JOURNAL_LIMIT) return EINVAL;
    memset(j, 0, sizeof(*j));
    strcpy(j->directory, directory);
    j->owner = owner;
    j->capacity = capacity;
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) return errno;
    for (unsigned copy = 0; copy < 2; ++copy) {
        checkpoint_t v;
        int error = load_checkpoint(j, copy, &v);
        /* Losing a checkpoint can replay data. It cannot advance an ACK. */
        if (error == ENOENT || error == EBADMSG) continue;
        if (error) return error;
        if (v.revision <= j->revision) continue;
        j->revision = v.revision;
        j->acknowledged = v.acknowledged;
        j->dropped_operational = v.dropped_operational;
        j->dropped_health = v.dropped_health;
    }
    j->sequence = j->acknowledged;
    for (unsigned slot = 0; slot < TB_LITE_JOURNAL_SLOTS; ++slot) {
        int error = scan_segment(j, slot);
        if (error) return error;
        const tb_lite_segment_t *segment = &j->segment[slot];
        if (!segment->first) continue;
        for (unsigned previous = 0; previous < slot; ++previous) {
            const tb_lite_segment_t *other = &j->segment[previous];
            if (other->first && segment->first <= other->last && other->first <= segment->last)
                return EBADMSG;
        }
    }
    for (unsigned slot = 0; slot < TB_LITE_JOURNAL_SLOTS; ++slot) reclaim_segment(j, slot, true);
    j->ready = true;
    return 0;
}

static unsigned head_slot(const tb_lite_journal_t *j, uint64_t after)
{
    unsigned found = NO_SLOT;
    for (unsigned slot = 0; slot < TB_LITE_JOURNAL_SLOTS; ++slot) {
        const tb_lite_segment_t *s = &j->segment[slot];
        if (s->pending && s->first > after && (found == NO_SLOT || s->first < j->segment[found].first)) found = slot;
    }
    return found;
}

int tb_lite_journal_append(tb_lite_journal_t *j, uint64_t sequence, uint64_t captured_ms,
                           bool health, const char *json)
{
    if (!j || !j->ready) return EIO;
    if (!json || !sequence || sequence > TB_LITE_SAFE_SEQ || sequence < j->sequence ||
        (captured_ms && (captured_ms < TB_LITE_EPOCH_MS || captured_ms > TB_LITE_SAFE_SEQ))) return EINVAL;
    frame_t frame = {.sequence = sequence, .captured_ms = captured_ms, .health = health};
    frame.length = (uint32_t)pack(frame.data, TB_LITE_RECORD_MAX - 1, json);
    if (!frame.length) return EMSGSIZE;
    uint32_t crc = tb_outbox_crc32((const unsigned char *)&frame, FRAME_HEADER + frame.length);
    memcpy(frame.data + frame.length, &crc, sizeof(crc));
    uint32_t record_bytes = (uint32_t)(FRAME_HEADER + frame.length + sizeof(crc));

    unsigned tail = NO_SLOT, empty = NO_SLOT;
    for (unsigned slot = 0; slot < TB_LITE_JOURNAL_SLOTS; ++slot) {
        reclaim_segment(j, slot, true);
        const tb_lite_segment_t *s = &j->segment[slot];
        if (!s->bytes && empty == NO_SLOT) empty = slot;
        if (s->last && (tail == NO_SLOT || s->last > j->segment[tail].last)) tail = slot;
    }
    if (sequence == j->sequence) {
        /* The worker retries the same bytes after an uncertain local write. */
        return sequence <= j->acknowledged ||
            (tail != NO_SLOT && j->segment[tail].last_crc == crc) ? 0 : EINVAL;
    }
    uint32_t reserve = j->capacity / 8u < HEALTH_HEADROOM ? j->capacity / 8u : HEALTH_HEADROOM;
    uint32_t limit = health ? j->capacity - reserve : j->capacity;
    unsigned old_tail = tail;
    bool create = tail == NO_SLOT || j->segment[tail].bytes + record_bytes > TB_LITE_JOURNAL_SEGMENT;
    if (tail != NO_SLOT && !j->segment[tail].pending &&
        (create || j->bytes + record_bytes > limit)) {
        reclaim_segment(j, tail, false);
        if (!j->segment[tail].bytes) { empty = tail; create = true; }
    }
    if (create) tail = empty;
    uint32_t extra = record_bytes + (create ? sizeof(segment_header_t) : 0);
    if (tail == NO_SLOT || j->bytes + extra > limit) return ENOSPC;

    char path[112];
    segment_path(j, tail, path);
    FILE *file = fopen(path, create ? "wb" : "ab");
    if (!file) return errno;
    int error = 0;
    if (create) {
        segment_header_t header = {.magic = SEGMENT_MAGIC, .owner = j->owner};
        header.crc = tb_outbox_crc32((const unsigned char *)&header, offsetof(segment_header_t, crc));
        if (fwrite(&header, 1, sizeof(header), file) != sizeof(header)) error = errno ? errno : EIO;
    }
    if (!error && fwrite(&frame, 1, record_bytes, file) != record_bytes) error = errno ? errno : EIO;
    error = finish_write(file, error);
    if (error) { j->ready = false; return error; }

    tb_lite_segment_t *segment = &j->segment[tail];
    if (create) {
        segment->bytes = sizeof(segment_header_t);
        segment->first = sequence;
    }
    if (!segment->pending) {
        segment->head = segment->bytes;
        segment->head_ms = captured_ms;
    }
    segment->bytes += record_bytes;
    segment->last = sequence;
    segment->last_crc = crc;
    ++segment->pending;
    ++j->pending;
    j->bytes += extra;
    j->sequence = sequence;
    if (old_tail != NO_SLOT && old_tail != tail) reclaim_segment(j, old_tail, false);
    return 0;
}

static int read_at(const tb_lite_journal_t *j, unsigned slot, uint32_t offset, frame_t *frame)
{
    char path[112];
    segment_path(j, slot, path);
    FILE *file = fopen(path, "rb");
    if (!file) return errno;
    uint32_t crc;
    int error = fseek(file, (long)offset, SEEK_SET) == 0 ? read_frame(file, frame, &crc) : EIO;
    fclose(file);
    return error == ENOENT || error == ENODATA ? EBADMSG : error;
}

int tb_lite_journal_read(tb_lite_journal_t *j, const tb_lite_ticket_t *previous, tb_lite_ticket_t *ticket,
                         char *json, size_t capacity)
{
    if (!j || !j->ready || !ticket || !json || (previous && previous->slot >= TB_LITE_JOURNAL_SLOTS)) return EINVAL;
    unsigned slot = head_slot(j, previous ? previous->sequence : 0);
    uint32_t begin = slot != NO_SLOT ? j->segment[slot].head : 0;
    if (previous && previous->end < j->segment[previous->slot].bytes) {
        slot = previous->slot;
        begin = previous->end;
    }
    if (slot == NO_SLOT) return ENOENT;
    frame_t frame;
    int error = read_at(j, slot, begin, &frame);
    if (!error) error = unpack(json, capacity, frame.data, frame.length);
    if (error) return error;
    *ticket = (tb_lite_ticket_t){.slot = (uint16_t)slot, .sequence = frame.sequence, .captured_ms = frame.captured_ms,
        .begin = begin, .end = begin + (uint32_t)(FRAME_HEADER + frame.length + sizeof(uint32_t)), .health = frame.health};
    return 0;
}

int tb_lite_journal_commit(tb_lite_journal_t *j, const tb_lite_ticket_t *tickets, size_t count)
{
    if (!j || !j->ready || !tickets || !count || count > TB_LITE_BATCH_MAX || count > j->pending) return EINVAL;
    unsigned slot = head_slot(j, 0);
    uint32_t head = slot != NO_SLOT ? j->segment[slot].head : 0;
    uint64_t ack = j->acknowledged;
    for (size_t i = 0; i < count; ++i) {
        const tb_lite_ticket_t *t = &tickets[i];
        if (slot == NO_SLOT || t->slot != slot || t->begin != head || t->end <= head ||
            t->end > j->segment[slot].bytes || t->sequence <= ack || t->sequence > j->segment[slot].last)
            return EINVAL;
        ack = t->sequence;
        head = t->end;
        if (head == j->segment[slot].bytes) {
            slot = head_slot(j, ack);
            head = slot != NO_SLOT ? j->segment[slot].head : 0;
        }
    }
    int error = save_checkpoint(j, ack, j->dropped_operational, j->dropped_health);
    if (error) return error;
    j->pending -= (uint32_t)count;
    for (size_t i = 0; i < count; ++i) {
        tb_lite_segment_t *s = &j->segment[tickets[i].slot];
        s->head = tickets[i].end;
        --s->pending;
        reclaim_segment(j, tickets[i].slot, true);
    }
    slot = head_slot(j, 0);
    if (slot != NO_SLOT) {
        frame_t frame;
        if (read_at(j, slot, j->segment[slot].head, &frame) != 0) j->ready = false;
        else j->segment[slot].head_ms = frame.captured_ms;
    }
    return 0;
}

int tb_lite_journal_drop(tb_lite_journal_t *j, uint32_t operational, uint32_t health)
{
    if (!j || !j->ready) return EIO;
    return save_checkpoint(j, j->acknowledged, j->dropped_operational + operational, j->dropped_health + health);
}

uint64_t tb_lite_journal_oldest(const tb_lite_journal_t *j)
{
    unsigned slot = head_slot(j, 0);
    return slot != NO_SLOT ? j->segment[slot].head_ms : 0;
}
