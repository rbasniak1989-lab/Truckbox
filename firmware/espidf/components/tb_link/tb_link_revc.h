#pragma once

#include "tb.h"
#include "tb_nmea.h"

esp_err_t tb_link_revc_start(void);
void tb_link_revc_add_json(cJSON *object);
bool tb_link_revc_fix(tb_gnss_fix_t *fix, uint64_t *received_us);
