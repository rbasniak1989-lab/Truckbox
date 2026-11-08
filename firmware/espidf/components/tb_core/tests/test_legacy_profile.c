#include "../telemetry_profile.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    unsigned full = 0, legacy = 0;
    for (uint32_t second = 0; second < 60; ++second) {
        if (tb_operational_due(true, second)) ++full;
        if (tb_operational_due(false, second)) {
            ++legacy;
            assert((second + 1U) % 15U == 0U);
        }
    }
    assert(full == 60U && legacy == 4U);
    assert(!tb_operational_due(false, 0U));
    assert(tb_operational_due(false, 14U));
    assert(tb_operational_due(true, 0U));
    puts("OK: LEGACY=4/60 state snapshots; FULL=60/60");
    return 0;
}
