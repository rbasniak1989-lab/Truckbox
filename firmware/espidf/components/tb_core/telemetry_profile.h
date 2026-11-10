#pragma once
/* Rev B1 light capture policy. Kept portable for host-only cadence tests. */
#include <stdbool.h>
#include <stdint.h>
enum { TB_LEGACY_OPERATIONAL_SECONDS = 10U };
static inline bool tb_operational_due(bool full_decode_enabled, uint32_t seconds_elapsed)
{
    return full_decode_enabled ||
           ((seconds_elapsed + 1U) % TB_LEGACY_OPERATIONAL_SECONDS) == 0U;
}
