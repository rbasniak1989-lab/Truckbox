#include "tbx_fuel.h"
#include <math.h>
#include <string.h>
void tbx_fuel_init(tbx_fuel_t *m) { if (m) memset(m, 0, sizeof(*m)); }
bool tbx_fuel_ingest(tbx_fuel_t *m, uint8_t sa, uint16_t rpm_raw,
    uint8_t torque_raw, uint64_t at_ms, double *lph, double *delta)
{
    if (!m || !lph || !delta) return false;
    *delta = 0;
    if (rpm_raw > 0xfaffu || torque_raw > 250u) { m->baseline = false; return false; }
    double torque_pct = torque_raw - 125.0;
    double kw = TBX_FUEL_REFERENCE_NM * fmax(torque_pct, 0.0) * 0.01 * (rpm_raw * 0.125) / 9550.0;
    *lph = kw * 0.25 * TBX_FUEL_C0;
    if (m->baseline && m->sa == sa && at_ms > m->at_ms && at_ms - m->at_ms <= 1000) {
        *delta = *lph * (double)(at_ms - m->at_ms) / 3600000.0;
        m->liters += *delta;
    } else if (m->baseline) ++m->skipped_intervals;
    m->baseline = true; m->sa = sa; m->at_ms = at_ms;
    return true;
}
