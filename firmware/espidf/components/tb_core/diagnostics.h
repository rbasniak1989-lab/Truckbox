#pragma once
#include "tbx_j1587_bridge.h"
#include "cJSON.h"
typedef struct {float speed,rpm,pedal;char gear[3],selected[3];uint8_t valid;}tb_diagnostic_context_t;
void tb_diagnostics_start(void);
void tb_diagnostics_observe(const tbx_j1587_observation_t *,uint64_t unix_ms,const tb_diagnostic_context_t *);
cJSON *tb_diagnostics_json(unsigned offset,unsigned limit);
/* Called outside acquisition locks. Bounded JSON, no per-frame RAW stream. */
void tb_diagnostics_flush(void);
void tb_diagnostics_checkpoint(void);
