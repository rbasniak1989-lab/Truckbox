#pragma once
#include "esp_http_server.h"
esp_err_t tb_crash_info_handler(httpd_req_t *request);
esp_err_t tb_crash_download_handler(httpd_req_t *request);
