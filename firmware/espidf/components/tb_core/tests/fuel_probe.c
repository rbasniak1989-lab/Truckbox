#include "tbx_fuel.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
int main(void) {
    tbx_fuel_t m; tbx_fuel_init(&m);
    unsigned sa, rpm, torque; uint64_t at;
    while (scanf("%u %u %u %" SCNu64, &sa, &rpm, &torque, &at) == 4) {
        double rate = 0, delta = 0;
        bool valid = tbx_fuel_ingest(&m, sa, rpm, torque, at, &rate, &delta);
        printf("%d %.17g %.17g %.17g\n", valid, rate, delta, m.liters);
    }
    assert(!ferror(stdin));
    return 0;
}
