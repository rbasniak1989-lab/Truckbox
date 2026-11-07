#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    bool utc_valid;
    bool has_speed;
    bool has_heading;
    bool has_altitude;
    bool has_hdop;
    double latitude;
    double longitude;
    double speed_kmh;
    double heading_deg;
    double altitude_m;
    double hdop;
    unsigned satellites;
    unsigned quality;
    int64_t unix_ms;
    int64_t rmc_unix_ms;
    int64_t gga_unix_ms;
} tb_gnss_fix_t;

typedef enum {
    TB_NMEA_IGNORED = 0,
    TB_NMEA_VALID = 1,
    TB_NMEA_NO_FIX = 2,
    TB_NMEA_INVALID = -1
} tb_nmea_result_t;

/* Invalid/unsupported lines never replace a previous fix. NO_FIX clears validity. */
tb_nmea_result_t tb_nmea_parse(const char *line, tb_gnss_fix_t *fix);
