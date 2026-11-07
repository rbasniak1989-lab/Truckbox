#include "tb_image_policy.h"

#include <stdio.h>
#include <string.h>

static bool reject(char *error, size_t error_size, const char *reason)
{
    if (error && error_size) {
        snprintf(error, error_size, "%s", reason);
    }
    return false;
}

bool tb_image_prefix_valid(const uint8_t *prefix, size_t length, bool target_link,
                           esp_app_desc_t *description, char *error, size_t error_size)
{
    if (!prefix || length < TB_IMAGE_PREFIX_SIZE) {
        return reject(error, error_size, "Arquivo de firmware incompleto.");
    }
    esp_image_header_t image;
    esp_image_segment_header_t segment;
    esp_app_desc_t app;
    memcpy(&image, prefix, sizeof(image));
    memcpy(&segment, prefix + sizeof(image), sizeof(segment));
    memcpy(&app, prefix + sizeof(image) + sizeof(segment), sizeof(app));

    if (image.magic != ESP_IMAGE_HEADER_MAGIC || image.segment_count == 0 ||
        image.segment_count > ESP_IMAGE_MAX_SEGMENTS || segment.data_len < sizeof(app) ||
        app.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        return reject(error, error_size, "Selecione o .bin de aplicação; imagens mescladas e outros arquivos não são aceitos.");
    }
    if (image.chip_id != ESP_CHIP_ID_ESP32C6) {
        return reject(error, error_size, "O firmware precisa ser compilado para ESP32-C6.");
    }
    if (image.spi_size != ESP_IMAGE_FLASH_SIZE_8MB) {
        return reject(error, error_size, "Use a imagem desta placa com flash de 8 MB e o particionamento OTA compatível.");
    }
    if (image.hash_appended != 1) {
        return reject(error, error_size, "A imagem não contém o SHA-256 de integridade exigido.");
    }
    const char *expected = target_link ? "truckbox_link" : "truckbox_core";
    if (!memchr(app.project_name, '\0', sizeof(app.project_name)) || strcmp(app.project_name, expected)) {
        return reject(error, error_size, target_link ?
                      "Este destino aceita apenas firmware truckbox_link." :
                      "Este destino aceita apenas firmware truckbox_core.");
    }
    if (!memchr(app.version, '\0', sizeof(app.version)) || app.version[0] == '\0') {
        return reject(error, error_size, "A versão no cabeçalho do firmware é inválida.");
    }
    if (description) {
        *description = app;
    }
    if (error && error_size) {
        error[0] = '\0';
    }
    return true;
}
