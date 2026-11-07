#define _POSIX_C_SOURCE 200809L
#include "tb_gps_outbox.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAGIC "TBGP1\n"
#define HEADER_SIZE (sizeof(MAGIC) - 1)
#define LINE_MAXIMUM (TB_GPS_OUTBOX_JSON_MAX + 32)

static uint32_t crc32(const unsigned char *data, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    while (length--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1)));
    }
    return ~crc;
}

static int synchronize(FILE *file)
{
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) return errno ? errno : EIO;
    return 0;
}

static int create_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) return errno;
    int result = fwrite(MAGIC, 1, HEADER_SIZE, file) == HEADER_SIZE ? synchronize(file) : EIO;
    if (fclose(file) != 0 && !result) result = errno;
    return result;
}

static int parse_line(char *line, char *kind, char **payload)
{
    size_t length = strlen(line);
    if (length < 13 || line[length - 1] != '\n' || line[1] != ' ' || line[10] != ' ' ||
        (line[0] != 'P' && line[0] != 'A')) return EINVAL;
    line[length - 1] = 0;
    char saved = line[10];
    line[10] = 0;
    char *end;
    errno = 0;
    unsigned long expected = strtoul(line + 2, &end, 16);
    bool malformed = errno || end != line + 10 || expected > UINT32_MAX;
    line[10] = saved;
    if (malformed || crc32((unsigned char *)line + 11, length - 12) != (uint32_t)expected) return EINVAL;
    *kind = line[0];
    *payload = line + 11;
    return 0;
}

static int truncate_tail(const char *path, off_t end)
{
    int descriptor = open(path, O_RDWR);
    if (descriptor < 0) return errno;
    int result = ftruncate(descriptor, end) == 0 && fsync(descriptor) == 0 ? 0 : (errno ? errno : EIO);
    close(descriptor);
    return result;
}

uint64_t tb_gps_outbox_pending(const tb_gps_outbox_t *box)
{
    return box->total_records >= box->delivered_records ? box->total_records - box->delivered_records : 0;
}

int tb_gps_outbox_init(tb_gps_outbox_t *box, const char *path, size_t maximum_bytes)
{
    if (!box || !path || strlen(path) + 5 >= sizeof(box->path) || maximum_bytes < 4096) return EINVAL;
    memset(box, 0, sizeof(*box));
    strcpy(box->path, path);
    box->maximum_bytes = maximum_bytes;
    char backup[sizeof(box->path) + 5], temporary[sizeof(box->path) + 5];
    snprintf(backup, sizeof(backup), "%s.bak", path);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    struct stat information;
    if (stat(path, &information) != 0) {
        if (errno != ENOENT) return errno;
        if (stat(backup, &information) == 0) {
            if (rename(backup, path) != 0) return errno;
        } else {
            if (errno != ENOENT) return errno;
            int result = create_file(path);
            if (result) return result;
        }
    }
    FILE *file = fopen(path, "rb");
    if (!file) return errno;
    char header[HEADER_SIZE];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) || memcmp(header, MAGIC, sizeof(header)) != 0) {
        fclose(file);
        return EINVAL;
    }
    box->read_offset = HEADER_SIZE;
    char line[LINE_MAXIMUM];
    int result = 0;
    off_t pending_truncation = -1;
    for (;;) {
        off_t start = ftell(file);
        if (!fgets(line, sizeof(line), file)) {
            if (ferror(file)) result = EIO;
            box->file_bytes = (uint64_t)start;
            break;
        }
        size_t length = strlen(line);
        if (!length || line[length - 1] != '\n') {
            if (!feof(file)) { result = EINVAL; break; }
            box->recovered_tail_bytes = length;
            pending_truncation = start;
            box->file_bytes = (uint64_t)start;
            break;
        }
        char kind, *payload;
        if (parse_line(line, &kind, &payload) != 0) { result = EINVAL; break; }
        if (kind == 'P') {
            if (!payload[0] || strlen(payload) >= TB_GPS_OUTBOX_JSON_MAX) { result = EINVAL; break; }
            box->total_records++;
        } else {
            uint64_t offset, count;
            char trailing;
            if (sscanf(payload, "%" SCNu64 ":%" SCNu64 "%c", &offset, &count, &trailing) != 2 ||
                offset < box->read_offset || offset > (uint64_t)start || count < box->delivered_records ||
                count > box->total_records) { result = EINVAL; break; }
            box->read_offset = offset;
            box->delivered_records = count;
        }
    }
    fclose(file);
    if (!result && pending_truncation >= 0) result = truncate_tail(path, pending_truncation);
    if (result) return result;
    /* Only unpublished temporary files and obsolete backup data are removed. */
    unlink(temporary);
    unlink(backup);
    box->ready = true;
    return 0;
}

static int compact(tb_gps_outbox_t *box)
{
    if (box->peek_end) return EAGAIN;
    char temporary[sizeof(box->path) + 5], backup[sizeof(box->path) + 5];
    snprintf(temporary, sizeof(temporary), "%s.tmp", box->path);
    snprintf(backup, sizeof(backup), "%s.bak", box->path);
    int result = create_file(temporary);
    if (result) return result;
    FILE *source = fopen(box->path, "rb"), *destination = fopen(temporary, "ab");
    if (!source || !destination) {
        if (source) fclose(source);
        if (destination) fclose(destination);
        return errno ? errno : EIO;
    }
    if (fseek(source, (long)box->read_offset, SEEK_SET) != 0) result = EIO;
    char line[LINE_MAXIMUM], validation[LINE_MAXIMUM];
    uint64_t copied = 0;
    while (!result && fgets(line, sizeof(line), source)) {
        memcpy(validation, line, strlen(line) + 1);
        char kind, *payload;
        result = parse_line(validation, &kind, &payload);
        if (!result && kind == 'P') {
            size_t length = strlen(line);
            if (fwrite(line, 1, length, destination) != length) result = EIO;
            else copied++;
        }
    }
    if (ferror(source)) result = EIO;
    if (!result && copied != tb_gps_outbox_pending(box)) result = EINVAL;
    if (!result) result = synchronize(destination);
    fclose(source);
    fclose(destination);
    if (result) { unlink(temporary); return result; }
    if (rename(box->path, backup) != 0) return errno;
    if (rename(temporary, box->path) != 0) {
        result = errno;
        rename(backup, box->path);
        return result;
    }
    unlink(backup);
    struct stat information;
    if (stat(box->path, &information) != 0) { box->ready = false; return errno; }
    box->file_bytes = (uint64_t)information.st_size;
    box->read_offset = HEADER_SIZE;
    box->total_records = copied;
    box->delivered_records = 0;
    box->acknowledgements_since_checkpoint = 0;
    return 0;
}

static int append_line(tb_gps_outbox_t *box, char kind, const char *payload)
{
    char line[LINE_MAXIMUM];
    int length = snprintf(line, sizeof(line), "%c %08" PRIX32 " %s\n", kind,
                          crc32((const unsigned char *)payload, strlen(payload)), payload);
    if (length < 0 || (size_t)length >= sizeof(line)) return EINVAL;
    FILE *file = fopen(box->path, "ab");
    if (!file) return errno;
    int result = fwrite(line, 1, (size_t)length, file) == (size_t)length ? synchronize(file) : EIO;
    if (fclose(file) != 0 && !result) result = errno;
    if (!result) box->file_bytes += (uint64_t)length;
    else box->ready = false; /* Reopen/recover on boot; do not append after an uncertain tail. */
    return result;
}

int tb_gps_outbox_append(tb_gps_outbox_t *box, const char *json)
{
    if (!box || !box->ready) return EIO;
    if (!json || !*json || strlen(json) >= TB_GPS_OUTBOX_JSON_MAX || strchr(json, '\n') || strchr(json, '\r')) return EINVAL;
    if (box->file_bytes + strlen(json) + 12 > box->maximum_bytes) {
        if (box->delivered_records && !box->peek_end) {
            int result = compact(box);
            if (result) return result;
        }
        if (box->file_bytes + strlen(json) + 12 > box->maximum_bytes) {
            box->rejected_records++;
            return ENOSPC;
        }
    }
    int result = append_line(box, 'P', json);
    if (!result) box->total_records++;
    return result;
}

int tb_gps_outbox_peek(tb_gps_outbox_t *box, char *json, size_t capacity)
{
    if (!box || !box->ready || !json || capacity < 2) return EINVAL;
    if (!tb_gps_outbox_pending(box)) return ENOENT;
    FILE *file = fopen(box->path, "rb");
    if (!file) return errno;
    int result = fseek(file, (long)box->read_offset, SEEK_SET) == 0 ? ENOENT : EIO;
    char line[LINE_MAXIMUM];
    while (result == ENOENT && fgets(line, sizeof(line), file)) {
        char kind, *payload;
        result = parse_line(line, &kind, &payload);
        if (result) break;
        if (kind != 'P') { result = ENOENT; continue; }
        if (strlen(payload) + 1 > capacity) { result = ENOBUFS; break; }
        strcpy(json, payload);
        box->peek_end = (uint64_t)ftell(file);
        result = 0;
        break;
    }
    fclose(file);
    return result;
}

int tb_gps_outbox_ack(tb_gps_outbox_t *box)
{
    if (!box || !box->ready || !box->peek_end) return EINVAL;
    box->read_offset = box->peek_end;
    box->peek_end = 0;
    box->delivered_records++;
    box->acknowledgements_since_checkpoint++;
    /* A torn checkpoint may replay eight accepted points; client_uid deduplicates them. */
    if (box->acknowledgements_since_checkpoint >= 8) {
        if (box->file_bytes + 64 > box->maximum_bytes) return compact(box);
        char marker[64];
        snprintf(marker, sizeof(marker), "%" PRIu64 ":%" PRIu64, box->read_offset, box->delivered_records);
        int result = append_line(box, 'A', marker);
        if (result) return result;
        box->acknowledgements_since_checkpoint = 0;
    }
    return 0;
}

void tb_gps_outbox_retry(tb_gps_outbox_t *box)
{
    if (box) box->peek_end = 0;
}
