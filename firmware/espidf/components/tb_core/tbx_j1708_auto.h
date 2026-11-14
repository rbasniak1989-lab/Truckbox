#pragma once
#include "tbx_j1708_active.h"
typedef enum {TBX_AUTO_WAIT_RX,TBX_AUTO_RUNNING,TBX_AUTO_BACKOFF,TBX_AUTO_BLOCKED,TBX_AUTO_DISABLED,TBX_AUTO_OTA}tbx_auto_phase_t;
typedef struct {
    tbx_auto_phase_t phase;
    uint64_t paused_us,resume_us,started_us;
    uint32_t starts,pauses;
    uint32_t stalls,last_sent,last_deferred;
    uint64_t checked_us,progress_us,due_since_us,eligible_stall_us;
    uint8_t failures;
    bool launch,last_enabled;
    const char *reason;
    const char *last_stop_reason;
    uint64_t stopped_us;
}tbx_j1708_auto_t;
typedef struct {
    bool enabled,persisted,interface_ready,local_conflict,ota_busy,operator_hold,bus_busy;
    uint8_t local_mid,targets[17];
    bool topology_attested,extended;
    uint32_t timeout_ms,periods[17];
}tbx_j1708_auto_env_t;
void tbx_j1708_auto_tick(tbx_j1708_auto_t *,tbx_j1708_active_t *,const tbx_j1708_auto_env_t *,bool launch,uint64_t now_us);
void tbx_j1708_auto_suspend(tbx_j1708_auto_t *,tbx_j1708_active_t *,const char *,uint64_t);
const char *tbx_j1708_auto_phase_name(tbx_auto_phase_t);
