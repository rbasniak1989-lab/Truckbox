#include "tb_nmea.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static tb_nmea_result_t sentence(const char *body, tb_gnss_fix_t *fix)
{
    char line[180];
    unsigned checksum = 0;
    for (const char *p = body; *p; ++p) checksum ^= (unsigned char)*p;
    snprintf(line, sizeof(line), "$%s*%02X\r\n", body, checksum);
    return tb_nmea_parse(line, fix);
}

int main(void)
{
    tb_gnss_fix_t fix = {0};
    assert(sentence("GNRMC,120000.250,A,2333.0000,S,04638.0000,W,10.0,181.5,051126,,,A", &fix) == TB_NMEA_VALID);
    assert(fix.valid && fix.utc_valid && fix.has_speed && fix.has_heading);
    assert(fabs(fix.latitude + 23.55) < 0.000001);
    assert(fabs(fix.longitude + 46.6333333333) < 0.000001);
    assert(fabs(fix.speed_kmh - 18.52) < 0.000001);
    assert(fix.unix_ms == INT64_C(1793880000250));
    assert(sentence("GNGGA,120001.000,2333.0000,S,04638.0000,W,1,08,0.9,760.2,M,-3.0,M,,", &fix) == TB_NMEA_VALID);
    assert(fix.has_altitude && fix.has_hdop && fix.satellites == 8);
    assert(fabs(fix.altitude_m - 760.2) < 0.0001 && fix.has_speed);
    assert(tb_nmea_parse("$GNRMC,120000.250,A,2333.0000,S,04638.0000,W,10.0,181.5,051126,,,A*00", &fix) == TB_NMEA_INVALID);
    assert(fix.valid);
    assert(sentence("GNRMC,120000.250,A,2360.0000,S,04638.0000,W,10.0,181.5,051126,,,A", &fix) == TB_NMEA_INVALID);
    assert(sentence("GNRMC,120000.250,A,2333.0000,X,04638.0000,W,10.0,181.5,051126,,,A", &fix) == TB_NMEA_INVALID);
    assert(sentence("GNRMC,120000.250,V,,,,,,,051126,,,N", &fix) == TB_NMEA_NO_FIX);
    assert(!fix.valid);
    assert(sentence("GNRMC,235959.000,A,4807.038,N,01131.000,E,0,0,281024,,,A", &fix) == TB_NMEA_VALID);
    int64_t yesterday = fix.unix_ms;
    assert(sentence("GNGGA,000001.000,4807.038,N,01131.000,E,1,12,0.8,500.0,M,0.0,M,,", &fix) == TB_NMEA_VALID);
    assert(fix.unix_ms == yesterday + 2000);
    assert(sentence("GNRMC,120000,A,0000.000,N,00000.000,E,0,0,290224,,,A", &fix) == TB_NMEA_VALID);
    assert(fix.latitude == 0 && fix.longitude == 0 && fix.utc_valid);
    assert(sentence("GNGGA,120001,0000.000,N,00000.000,E,0,00,0,0,M,0,M,,", &fix) == TB_NMEA_NO_FIX);
    assert(!fix.valid);
    assert(sentence("GNRMC,250000,A,4807.038,N,01131.000,E,0,0,051126,,,A", &fix) == TB_NMEA_INVALID);
    assert(sentence("GNGLL,4807.038,N,01131.000,E,120000,A", &fix) == TB_NMEA_IGNORED);
    puts("NMEA parser tests passed");
    return 0;
}
