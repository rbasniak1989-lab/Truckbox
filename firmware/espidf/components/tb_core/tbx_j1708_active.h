#ifndef TBX_J1708_ACTIVE_H
#define TBX_J1708_ACTIVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tbx_j1708_requests.h"
#include "tbx_j1708_field.h"
#include "tbx_j1708_rx_guard.h"

#define TBX_J1708_ACTIVE_LEASE_US UINT64_C(60000000)
#define TBX_J1708_ACTIVE_ECHO_TIMEOUT_US UINT64_C(20000)
/* Legacy manual road trial only. Production uses its persisted policy. */
#define TBX_J1708_WORK_LEASE_US UINT64_C(43200000000)
#define TBX_J1708_WORK_MIN_SPACING_MS 1200u

typedef enum {
    TBX_J1708_TX_FAULT = -1,
    TBX_J1708_TX_DEFERRED = 0,
    TBX_J1708_TX_STARTED = 1
} tbx_j1708_tx_result_t;

typedef struct {
    uint8_t local_mid;
    /* Same order as scheduler.pids: all17 must be supplied, no defaults. */
    uint8_t target_mid[TBX_J1708_REQUEST_CAPACITY];
    uint8_t priority; /* 1..8; priority8 is the conservative test policy. */
    bool inverted_de21_attested;
    /* Parked-only, one outstanding PID at a time; 0 start_pid means first PID. */
    bool sequential_scan;
    bool single_shot;
    uint16_t start_pid;
} tbx_j1708_active_config_t;

typedef struct {
    tbx_j1708_tx_result_t (*begin_tx)(const uint8_t *bytes, size_t length, void *context);
    void (*abort_tx)(void *context); /* Must release DE21 immediately. */
    void (*event)(const char *event, uint16_t pid, const char *detail, void *context);
} tbx_j1708_active_ops_t;

/* Calls are serialized by the owner. No clock, allocation, GPIO, network or
 * sleep occurs here. RX ownership stays with the original passive driver. */
typedef struct tbx_j1708_active {
    tbx_j1708_request_scheduler_t *scheduler;
    tbx_j1708_active_config_t config;
    tbx_j1708_active_ops_t ops;
    void *context;
    bool configured, tx_active, work_mode,production_mode;
    uint8_t echo_position;
    uint8_t tx_frame[6], tx_length;
    uint16_t tx_pid,last_sent_pid;
    uint64_t last_sent_us;
    uint64_t now_us, last_rx_us, tx_started_us, lease_deadline_us;
    uint32_t idle_guard_us;
    uint32_t echo_ok, collisions, echo_timeouts, lease_expirations;
    uint32_t scan_sent_baseline;
    uint8_t scan_start_index, scan_budget, scan_completed;
    uint32_t trial_echo_base, trial_answer_base;
    uint64_t retry_after_us, promotion_deadline_us;
    bool physical_validated;
    tbx_j1708_field_t field;
    tbx_j1708_rx_guard_t rx_guard;
    uint8_t consecutive_failures;
    uint16_t last_response_pid;
    uint8_t last_response_mid;
    uint64_t last_response_us;
} tbx_j1708_active_t;

void tbx_j1708_active_init(tbx_j1708_active_t *, tbx_j1708_request_scheduler_t *,
                           const tbx_j1708_active_ops_t *, void *context);
bool tbx_j1708_active_configure(tbx_j1708_active_t *, const tbx_j1708_active_config_t *);
/* Explicit authorization and parked attestation expire after60seconds.
 * Renewal does not reset scheduler spacing or its outstanding request. */
bool tbx_j1708_active_arm(tbx_j1708_active_t *, bool authorized, bool stationary, uint64_t now_us);
/* Promote only after explicit external or supervised-indirect confirmation.
 * Both paths retain parked authorization, current-boot evidence and expiry. */
bool tbx_j1708_active_enable_work(tbx_j1708_active_t *, uint64_t now_us);
bool tbx_j1708_active_work_ready(const tbx_j1708_active_t *, uint64_t now_us);
bool tbx_j1708_active_attest_physical(tbx_j1708_active_t *, bool external_tx,
    bool external_reply, uint64_t now_us);
bool tbx_j1708_active_start_production(tbx_j1708_active_t *,uint64_t now_us);
void tbx_j1708_active_stop(tbx_j1708_active_t *, const char *reason);
void tbx_j1708_active_tick(tbx_j1708_active_t *, uint64_t now_us);
/* Feed every received UART byte, before frame parsing; echo mismatch releases
 * TX immediately. Every byte also postpones the conservative idle guard. */
void tbx_j1708_active_note_rx(tbx_j1708_active_t *, const uint8_t *, size_t, uint64_t now_us);
void tbx_j1708_active_note_link_error(tbx_j1708_active_t *, const char *reason);
/* Caller supplies only checksum/layout-valid decoded parameters. This is a
 * tentative temporal correlation, never proof of causal ECU response. */
bool tbx_j1708_active_note_response(tbx_j1708_active_t *, uint8_t mid, uint16_t pid, uint64_t now_us);
bool tbx_j1708_active_is_tx_echo(const tbx_j1708_active_t *, const uint8_t *, size_t, uint64_t now_us);

#endif
