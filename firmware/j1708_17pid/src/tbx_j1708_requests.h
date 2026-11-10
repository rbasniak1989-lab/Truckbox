#ifndef TBX_J1708_REQUESTS_H
#define TBX_J1708_REQUESTS_H
#include <stdbool.h>
#include <stdint.h>
#define TBX_J1708_REQUEST_COUNT 17u
#define TBX_J1708_NO_PENDING 0xffu
typedef struct {
    uint16_t pid;
    bool enabled;
    uint32_t sent, answered, timed_out, last_sent_ms, last_answered_ms;
    uint32_t period_ms, next_due_ms;
} tbx_j1708_request_stat_t;
typedef bool (*tbx_j1708_send_request_fn)(uint16_t pid, void *context);
typedef struct {
    tbx_j1708_request_stat_t pids[TBX_J1708_REQUEST_COUNT];
    uint8_t cursor, pending;
    bool test_permitted, armed, safety_stopped, next_tx_valid, send_deferred, periodic;
    uint32_t min_spacing_ms, timeout_ms, next_tx_ms, pending_since_ms;
    uint32_t sent_total, answered_total, timeout_total, tx_failure_total, deferred_total;
    uint8_t consecutive_timeouts, link_error_count;
} tbx_j1708_request_scheduler_t;
void tbx_j1708_request_init(tbx_j1708_request_scheduler_t *scheduler);
void tbx_j1708_request_permit_stationary_test(tbx_j1708_request_scheduler_t *scheduler, bool permitted);
bool tbx_j1708_request_arm(tbx_j1708_request_scheduler_t *scheduler);
void tbx_j1708_request_stop(tbx_j1708_request_scheduler_t *scheduler);
bool tbx_j1708_request_enable_pid(tbx_j1708_request_scheduler_t *scheduler, uint16_t pid, bool enabled);
void tbx_j1708_request_set_spacing(tbx_j1708_request_scheduler_t *scheduler, uint32_t min_spacing_ms);
uint32_t tbx_j1708_request_default_period(uint16_t pid);
void tbx_j1708_request_start_periodic(tbx_j1708_request_scheduler_t *, uint32_t now_ms);
void tbx_j1708_request_note_link_error(tbx_j1708_request_scheduler_t *scheduler);
/* A NULL send_request processes response deadlines without transmitting. */
void tbx_j1708_request_tick(tbx_j1708_request_scheduler_t *scheduler, uint32_t now_ms, tbx_j1708_send_request_fn send_request, void *send_context);
/* Tentative matching only. Must validate source MID and physical causality. */
bool tbx_j1708_request_note_response(tbx_j1708_request_scheduler_t *scheduler, uint16_t pid, uint32_t now_ms);
#endif
