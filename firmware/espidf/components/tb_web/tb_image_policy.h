#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_app_desc.h"
#include "esp_app_format.h"

#define TB_IMAGE_PREFIX_SIZE (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t))

/* Checks the untrusted upload prefix before any flash erase or network forwarding.
 * esp_ota_end still validates the complete image, revision and appended SHA-256. */
bool tb_image_prefix_valid(const uint8_t *prefix, size_t length, bool target_link,
                           esp_app_desc_t *description, char *error, size_t error_size);
