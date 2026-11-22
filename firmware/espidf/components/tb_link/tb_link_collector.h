#pragma once

#include "tb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

esp_err_t tb_link_collector_start(SemaphoreHandle_t filesystem_lock);
void tb_link_collector_storage(esp_err_t result);
void tb_link_collector_json(cJSON *root);
esp_err_t tb_link_collector_reset_reference(httpd_req_t *request);
bool tb_link_collector_has_route(void);
esp_err_t tb_link_collector_post(const char *body, size_t length, uint64_t sequence,
                                unsigned operational, unsigned health, bool alternate,
                                int *status, const char **stage);
