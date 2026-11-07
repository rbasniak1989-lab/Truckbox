#include "tb_nmea.h"
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DAY_MS INT64_C(86400000)

static int hex_digit(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    value = (char)toupper((unsigned char)value);
    return value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
}

static bool number(const char *field, double minimum, double maximum, double *value)
{
    if (!field || !*field || isspace((unsigned char)*field)) return false;
    const char *cursor = field;
    if (*cursor == '+' || *cursor == '-') cursor++;
    unsigned digits = 0, points = 0;
    for (; *cursor; ++cursor) {
        if (*cursor == '.') points++;
        else if (isdigit((unsigned char)*cursor)) digits++;
        else return false;
    }
    if (!digits || points > 1) return false;
    char *end = NULL;
    double parsed = strtod(field, &end);
    if (!end || *end || !isfinite(parsed) || parsed < minimum || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static bool coordinate(const char *field, const char *hemisphere, bool latitude, double *value)
{
    double raw;
    if (!number(field, 0, latitude ? 9000 : 18000, &raw) || !hemisphere ||
        strlen(hemisphere) != 1) return false;
    const char direction = hemisphere[0];
    if (latitude ? (direction != 'N' && direction != 'S') : (direction != 'E' && direction != 'W')) return false;
    double degrees = floor(raw / 100.0);
    double minutes = raw - degrees * 100.0;
    if (minutes >= 60.0) return false;
    double decimal = degrees + minutes / 60.0;
    if (decimal > (latitude ? 90.0 : 180.0)) return false;
    *value = (direction == 'S' || direction == 'W') ? -decimal : decimal;
    return true;
}

static bool time_of_day(const char *field, uint32_t *milliseconds)
{
    if (!field || strlen(field) < 6) return false;
    for (size_t i = 0; i < 6; ++i) if (!isdigit((unsigned char)field[i])) return false;
    int hours = (field[0] - '0') * 10 + field[1] - '0';
    int minutes = (field[2] - '0') * 10 + field[3] - '0';
    int seconds = (field[4] - '0') * 10 + field[5] - '0';
    if (hours > 23 || minutes > 59 || seconds > 59) return false;
    int fractional = 0;
    if (field[6]) {
        if (field[6] != '.' || !field[7]) return false;
        int weight = 100;
        for (size_t i = 7; field[i]; ++i) {
            if (!isdigit((unsigned char)field[i])) return false;
            fractional += (field[i] - '0') * weight;
            weight /= 10;
        }
    }
    *milliseconds = (uint32_t)(((hours * 60 + minutes) * 60 + seconds) * 1000 + fractional);
    return true;
}

/* Gregorian calendar conversion, independent of the host timezone/timegm(). */
static int64_t civil_days(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + day_of_era - 719468;
}

static bool date_epoch(const char *field, int64_t *midnight)
{
    if (!field || strlen(field) != 6) return false;
    for (size_t i = 0; i < 6; ++i) if (!isdigit((unsigned char)field[i])) return false;
    unsigned day = (field[0] - '0') * 10u + field[1] - '0';
    unsigned month = (field[2] - '0') * 10u + field[3] - '0';
    int short_year = (field[4] - '0') * 10 + field[5] - '0';
    int year = short_year + (short_year >= 80 ? 1900 : 2000);
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!month || month > 12 || !day) return false;
    unsigned maximum = days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day > maximum || year < 2024) return false;
    *midnight = civil_days(year, month, day) * DAY_MS;
    return true;
}

tb_nmea_result_t tb_nmea_parse(const char *line, tb_gnss_fix_t *fix)
{
    if (!line || !fix || line[0] != '$') return TB_NMEA_INVALID;
    size_t length = strlen(line);
    if (length < 10 || length >= 160) return TB_NMEA_INVALID;
    const char *star = strchr(line, '*');
    if (!star || star - line < 6 || !star[1] || !star[2]) return TB_NMEA_INVALID;
    int high = hex_digit(star[1]), low = hex_digit(star[2]);
    if (high < 0 || low < 0) return TB_NMEA_INVALID;
    for (const char *tail = star + 3; *tail; ++tail) if (*tail != '\r' && *tail != '\n') return TB_NMEA_INVALID;
    unsigned checksum = 0;
    for (const char *cursor = line + 1; cursor < star; ++cursor) checksum ^= (unsigned char)*cursor;
    if (checksum != (unsigned)((high << 4) | low)) return TB_NMEA_INVALID;
    char buffer[160];
    size_t payload_size = (size_t)(star - line - 1);
    memcpy(buffer, line + 1, payload_size);
    buffer[payload_size] = 0;
    char *fields[24] = {buffer};
    size_t count = 1;
    for (char *cursor = buffer; *cursor; ++cursor) {
        if (*cursor == ',') {
            if (count >= sizeof(fields) / sizeof(fields[0])) return TB_NMEA_INVALID;
            *cursor = 0;
            fields[count++] = cursor + 1;
        }
    }
    if (strlen(fields[0]) != 5) return TB_NMEA_IGNORED;
    bool rmc = strcmp(fields[0] + 2, "RMC") == 0;
    bool gga = strcmp(fields[0] + 2, "GGA") == 0;
    if (!rmc && !gga) return TB_NMEA_IGNORED;
    if ((rmc && count < 10) || (gga && count < 11)) return TB_NMEA_INVALID;
    uint32_t tod;
    if (!time_of_day(fields[1], &tod)) return TB_NMEA_INVALID;
    tb_gnss_fix_t next = *fix;
    double latitude, longitude, numeric;
    if (rmc) {
        if (strcmp(fields[2], "V") == 0 || (count > 12 && strcmp(fields[12], "N") == 0)) {
            fix->valid = false;
            return TB_NMEA_NO_FIX;
        }
        if (strcmp(fields[2], "A") != 0 ||
            !coordinate(fields[3], fields[4], true, &latitude) ||
            !coordinate(fields[5], fields[6], false, &longitude)) return TB_NMEA_INVALID;
        int64_t midnight;
        next.utc_valid = date_epoch(fields[9], &midnight);
        next.unix_ms = next.utc_valid ? midnight + tod : 0;
        next.rmc_unix_ms = next.unix_ms;
        if (!next.utc_valid || !next.gga_unix_ms || llabs(next.unix_ms - next.gga_unix_ms) > 2000) {
            next.has_altitude = false;
            next.has_hdop = false;
            next.satellites = 0;
            next.quality = 0;
        }
        next.has_speed = number(fields[7], 0, 2000, &numeric);
        if (next.has_speed) next.speed_kmh = numeric * 1.852;
        next.has_heading = number(fields[8], 0, 360, &numeric);
        if (next.has_heading) next.heading_deg = numeric == 360 ? 0 : numeric;
    } else {
        if (!number(fields[6], 0, 8, &numeric) || floor(numeric) != numeric) return TB_NMEA_INVALID;
        next.quality = (unsigned)numeric;
        if (!next.quality) {
            fix->valid = false;
            fix->quality = 0;
            return TB_NMEA_NO_FIX;
        }
        if (!coordinate(fields[2], fields[3], true, &latitude) ||
            !coordinate(fields[4], fields[5], false, &longitude)) return TB_NMEA_INVALID;
        if (next.utc_valid) {
            int64_t previous_tod = next.unix_ms % DAY_MS;
            int64_t midnight = next.unix_ms - previous_tod;
            if ((int64_t)tod - previous_tod < -DAY_MS / 2) midnight += DAY_MS;
            else if ((int64_t)tod - previous_tod > DAY_MS / 2) midnight -= DAY_MS;
            next.unix_ms = midnight + tod;
        }
        next.gga_unix_ms = next.utc_valid ? next.unix_ms : 0;
        if (!next.utc_valid || llabs(next.unix_ms - next.rmc_unix_ms) > 2000) {
            next.has_speed = false;
            next.has_heading = false;
        }
        next.has_altitude = strcmp(fields[10], "M") == 0 && number(fields[9], -1500, 50000, &numeric);
        if (next.has_altitude) next.altitude_m = numeric;
        next.has_hdop = number(fields[8], 0, 999, &numeric);
        if (next.has_hdop) next.hdop = numeric;
        if (number(fields[7], 0, 99, &numeric) && floor(numeric) == numeric) next.satellites = (unsigned)numeric;
    }
    next.valid = true;
    next.latitude = latitude;
    next.longitude = longitude;
    *fix = next;
    return TB_NMEA_VALID;
}
