#pragma once
#include <stddef.h>
/* Local format2 only. Lossless LZSS + base64; Cloud always receives the original
 * JSON. Zero means use legacy JSON / invalid compressed input respectively. */
size_t tb_store_pack(const char *json, size_t length, char *out, size_t capacity);
size_t tb_store_unpack(const char *packed, char *out, size_t capacity);
