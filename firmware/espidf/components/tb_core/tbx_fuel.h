#ifndef TBX_FUEL_H
#define TBX_FUEL_H
#include <stdbool.h>
#include <stdint.h>
#define TBX_FUEL_C0 0.9720031611625598
#define TBX_FUEL_REFERENCE_NM 2643.0
typedef struct {
    bool baseline;
    uint8_t sa;
    uint64_t at_ms;
    double liters;
    uint32_t skipped_intervals;
} tbx_fuel_t;
void tbx_fuel_init(tbx_fuel_t *model);
/* Document Master1.1 model, not a physical flowmeter. Source changes, invalid
 * samples, resets and gaps >1s never integrate an unknown interval. */
bool tbx_fuel_ingest(tbx_fuel_t *model, uint8_t sa, uint16_t rpm_raw,
    uint8_t torque_raw, uint64_t at_ms, double *lph, double *delta_liters);
#endif
