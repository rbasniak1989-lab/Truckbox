#pragma once
#include "tb.h"

esp_err_t tb_store_init(void);
esp_err_t tb_capture_start(void);
void tb_capture_add_json(cJSON *object);
void tb_capture_reset_trip(void);
void tb_live_can(uint32_t id, bool extended, uint8_t dlc, const uint8_t data[8]);
uint64_t tb_clock_milliseconds(void);
bool tb_core_append_gps(cJSON *sample);


bool tb_core_append_gps_at(cJSON *sample, uint64_t captured_us);
bool tb_capture_request_control(bool arm, bool authorized, bool stationary);
bool tb_capture_tx_ready(void);
/* Enable continuous read-only requests after a verified stationary TX trial. */
bool tb_capture_request_enable_work(bool authorized, bool stationary);

bool tb_capture_request_configure(const cJSON *config, char *error, size_t error_size);

bool tb_capture_confirm_physical(const cJSON *body);
