#include "tb_image_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t prefix[TB_IMAGE_PREFIX_SIZE];

static void fixture(bool link)
{
    esp_image_header_t header = {
        .magic = ESP_IMAGE_HEADER_MAGIC,
        .segment_count = 4,
        .spi_size = ESP_IMAGE_FLASH_SIZE_8MB,
        .chip_id = ESP_CHIP_ID_ESP32C6,
        .hash_appended = 1,
    };
    esp_image_segment_header_t segment = { .data_len = sizeof(esp_app_desc_t) + 64 };
    esp_app_desc_t app = { .magic_word = ESP_APP_DESC_MAGIC_WORD };
    strcpy(app.project_name, link ? "truckbox_link" : "truckbox_core");
    strcpy(app.version, "0.1.0-test");
    memcpy(prefix, &header, sizeof(header));
    memcpy(prefix + sizeof(header), &segment, sizeof(segment));
    memcpy(prefix + sizeof(header) + sizeof(segment), &app, sizeof(app));
}

static bool accepts(bool link)
{
    char message[192] = {0};
    bool result = tb_image_prefix_valid(prefix, sizeof(prefix), link, NULL, message, sizeof(message));
    assert(result || message[0]);
    return result;
}

int main(void)
{
    assert(sizeof(esp_image_header_t) == 24);
    assert(sizeof(esp_image_segment_header_t) == 8);
    assert(sizeof(esp_app_desc_t) == 256);
    assert(TB_IMAGE_PREFIX_SIZE == 288);
    fixture(false);
    assert(accepts(false));
    assert(!accepts(true));
    fixture(true);
    assert(accepts(true));
    assert(!accepts(false));

    char message[192];
    esp_app_desc_t output = {0};
    assert(tb_image_prefix_valid(prefix, sizeof(prefix), true, &output, message, sizeof(message)));
    assert(!strcmp(output.project_name, "truckbox_link"));
    assert(!strcmp(output.version, "0.1.0-test"));
    assert(!message[0]);
    assert(!tb_image_prefix_valid(prefix, sizeof(prefix) - 1, true, NULL, message, sizeof(message)));
    assert(!tb_image_prefix_valid(NULL, sizeof(prefix), true, NULL, NULL, 0));

    fixture(false); prefix[0] = 0; assert(!accepts(false));
    fixture(false); prefix[1] = 0; assert(!accepts(false));
    fixture(false); prefix[1] = ESP_IMAGE_MAX_SEGMENTS + 1; assert(!accepts(false));
    fixture(false); prefix[12] = ESP_CHIP_ID_ESP32S3; assert(!accepts(false));
    fixture(false); prefix[3] = ESP_IMAGE_FLASH_SIZE_4MB << 4; assert(!accepts(false));
    fixture(false); prefix[23] = 0; assert(!accepts(false));

    fixture(false);
    esp_image_segment_header_t segment = { .data_len = sizeof(esp_app_desc_t) - 1 };
    memcpy(prefix + sizeof(esp_image_header_t), &segment, sizeof(segment));
    assert(!accepts(false));

    const size_t app_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    fixture(false); prefix[app_offset] ^= 1; assert(!accepts(false));
    fixture(false);
    memset(prefix + app_offset + offsetof(esp_app_desc_t, project_name), 'x', sizeof(output.project_name));
    assert(!accepts(false));
    fixture(false);
    memcpy(prefix + app_offset + offsetof(esp_app_desc_t, project_name), "truckbox_core_evil", 18);
    assert(!accepts(false));
    fixture(false);
    memset(prefix + app_offset + offsetof(esp_app_desc_t, version), 'x', sizeof(output.version));
    assert(!accepts(false));
    fixture(false); prefix[app_offset + offsetof(esp_app_desc_t, version)] = 0; assert(!accepts(false));

    fixture(false);
    char tiny[2] = {'x', 'x'};
    assert(!tb_image_prefix_valid(prefix, sizeof(prefix), true, NULL, tiny, sizeof(tiny)));
    assert(tiny[1] == '\0');
    puts("image policy: valid roles, malformed/truncated images, chip, hash and string boundaries passed");
    return 0;
}
