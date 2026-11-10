#include "tb_json.h"
#include <string.h>

#define JSON_DEPTH 32u

typedef struct {
    tb_json_sink_t sink;
    void *context;
    size_t used, maximum;
} writer_t;

static bool put(writer_t *writer, const char *bytes, size_t length)
{
    if (length > writer->maximum - writer->used) return false;
    if (writer->sink && length && !writer->sink(bytes, length, writer->context)) return false;
    writer->used += length;
    return true;
}

static bool quoted(writer_t *writer, const char *string)
{
    static const char digits[] = "0123456789abcdef";
    if (!put(writer, "\"", 1)) return false;
    if (!string) string = "";
    const unsigned char *position = (const unsigned char *)string, *start = position;
    for (;; ++position) {
        unsigned char ch = *position;
        if (ch && ch != '"' && ch != '\\' && ch >= 32) continue;
        if (position > start && !put(writer, (const char *)start, (size_t)(position - start))) return false;
        if (!ch) break;
        char escaped[6] = {'\\', 0, 0, 0, 0, 0};
        size_t length = 2;
        switch (ch) {
            case '"': escaped[1] = '"'; break;
            case '\\': escaped[1] = '\\'; break;
            case '\b': escaped[1] = 'b'; break;
            case '\f': escaped[1] = 'f'; break;
            case '\n': escaped[1] = 'n'; break;
            case '\r': escaped[1] = 'r'; break;
            case '\t': escaped[1] = 't'; break;
            default:
                escaped[1] = 'u';
                escaped[2] = escaped[3] = '0';
                escaped[4] = digits[ch >> 4];
                escaped[5] = digits[ch & 15];
                length = 6;
                break;
        }
        if (!put(writer, escaped, length)) return false;
        start = position + 1;
    }
    return put(writer, "\"", 1);
}

static bool encode(writer_t *writer, cJSON *node, unsigned depth, bool consume)
{
    if (!node || depth > JSON_DEPTH) return false;
    if (cJSON_IsString(node)) return quoted(writer, node->valuestring);
    if (cJSON_IsRaw(node)) return node->valuestring && put(writer, node->valuestring, strlen(node->valuestring));
    bool object = cJSON_IsObject(node), array = cJSON_IsArray(node);
    if (!object && !array) {
        /* Preserve cJSON's numeric precision/nonfinite behavior, using a fixed
         * stack buffer instead of a second allocated serialized document. */
        char primitive[64];
        cJSON value = *node;
        value.child = value.next = value.prev = NULL;
        return cJSON_PrintPreallocated(&value, primitive, sizeof(primitive), false) &&
               put(writer, primitive, strlen(primitive));
    }
    if (!put(writer, object ? "{" : "[", 1)) return false;
    /* Reference nodes borrow their children. Never detach/delete those. */
    bool owned = consume && !(node->type & cJSON_IsReference), first = true;
    cJSON *child = node->child;
    while (child) {
        cJSON *next = child->next;
        if (owned) cJSON_DetachItemViaPointer(node, child);
        bool ok = first || put(writer, ",", 1);
        first = false;
        if (ok && object) ok = quoted(writer, child->string) && put(writer, ":", 1);
        if (ok) ok = encode(writer, child, depth + 1, owned);
        if (owned) cJSON_Delete(child);
        if (!ok) return false;
        child = next;
    }
    return put(writer, object ? "}" : "]", 1);
}

size_t tb_json_measure(const cJSON *node, size_t maximum_bytes)
{
    writer_t writer = {.maximum = maximum_bytes};
    return encode(&writer, (cJSON *)node, 0, false) ? writer.used : SIZE_MAX;
}

bool tb_json_write_owned(cJSON *node, tb_json_sink_t sink, void *context)
{
    writer_t writer = {.sink = sink, .context = context, .maximum = SIZE_MAX};
    bool ok = sink && encode(&writer, node, 0, true);
    cJSON_Delete(node);
    return ok;
}

typedef struct {
    char *bytes;
    size_t capacity, used;
} buffer_t;

static bool buffer_sink(const char *bytes, size_t length, void *context)
{
    buffer_t *buffer = context;
    if (length > buffer->capacity - buffer->used) return false;
    memcpy(buffer->bytes + buffer->used, bytes, length);
    buffer->used += length;
    return true;
}

char *tb_json_print_owned(cJSON *node, size_t maximum_bytes)
{
    size_t length = tb_json_measure(node, maximum_bytes);
    if (length == SIZE_MAX) {
        cJSON_Delete(node);
        return NULL;
    }
    char *bytes = cJSON_malloc(length + 1);
    if (!bytes) {
        cJSON_Delete(node);
        return NULL;
    }
    buffer_t buffer = {.bytes = bytes, .capacity = length};
    if (!tb_json_write_owned(node, buffer_sink, &buffer)) {
        cJSON_free(bytes);
        return NULL;
    }
    bytes[buffer.used] = 0;
    return bytes;
}
