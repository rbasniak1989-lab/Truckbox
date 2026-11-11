#ifndef TBX_J1708_FIELD_H
#define TBX_J1708_FIELD_H
#include <stdbool.h>
#include <stdint.h>
#include "tbx_j1708_requests.h"
#include "tbx_j1708_rx_guard.h"
/* Explicit parked session, RAM only. A result is never electrical proof. */
#define TBX_J1708_FIELD_WINDOW_US UINT64_C(30000000)
#define TBX_J1708_FIELD_LEASE_US UINT64_C(600000000)
#define TBX_J1708_FIELD_ROUNDS 3u
#define TBX_J1708_FIELD_MIN_CONTRAST 9u

typedef enum {
    TBX_FIELD_IDLE, TBX_FIELD_BEFORE, TBX_FIELD_CONTROL, TBX_FIELD_REQUEST,
    TBX_FIELD_GAP, TBX_FIELD_AFTER, TBX_FIELD_SATISFACTORY,
    TBX_FIELD_INCONCLUSIVE, TBX_FIELD_FAILED, TBX_FIELD_CONFIRMED
} tbx_j1708_field_phase_t;
typedef struct {
    uint16_t before, after;
    uint8_t controls, control_hits, sent, echoes, replies;
    uint32_t latency_min_us, latency_max_us, latency_sum_us;
} tbx_j1708_field_pid_t;
typedef struct {
    uint32_t before_frames,after_frames,before_errors,after_errors;
    uint32_t before_duration_us,after_duration_us;
} tbx_j1708_field_health_t;
typedef struct {
    tbx_j1708_field_phase_t phase;
    uint64_t started_us, phase_until_us, last_frame_us, watch_at_us, completed_us;
    uint32_t frames, bad_frames, before_frames, before_errors, after_frames, after_errors;
    uint32_t before_duration_us, after_duration_us, after_start_errors;
    uint32_t watch_frames, watch_errors, after_start_frames;
    uint16_t completed;
    uint8_t index, contrasting_pids, result_cursor;
    bool control_seen, echo_seen, confirmed;
    volatile bool audit_lost;
    const char *reason;
    tbx_j1708_field_pid_t pids[TBX_J1708_REQUEST_COUNT];
} tbx_j1708_field_t;
struct tbx_j1708_active;
bool tbx_j1708_field_running(const tbx_j1708_field_t *);
const char *tbx_j1708_field_phase_name(tbx_j1708_field_phase_t);
bool tbx_j1708_field_start(struct tbx_j1708_active *,bool authorized,bool stationary,uint64_t now_us);
bool tbx_j1708_field_confirm(struct tbx_j1708_active *,uint64_t session_started_us,bool acknowledged,uint64_t now_us);
/* Single existing active controller calls this gate; it is not a second sender. */
bool tbx_j1708_field_tick(struct tbx_j1708_active *,uint64_t now_us);
void tbx_j1708_field_note_frame_kind(struct tbx_j1708_active *,uint8_t mid,tbx_rx_kind_t,uint64_t now_us);
void tbx_j1708_field_note_frame(struct tbx_j1708_active *,uint8_t mid,bool checksum_valid,uint64_t now_us);
void tbx_j1708_field_note_parameter(struct tbx_j1708_active *,uint8_t mid,uint16_t pid,uint64_t now_us);
void tbx_j1708_field_sent(struct tbx_j1708_active *);
void tbx_j1708_field_echo(struct tbx_j1708_active *);
void tbx_j1708_field_reply(struct tbx_j1708_active *,uint64_t now_us);
void tbx_j1708_field_fail(struct tbx_j1708_active *,const char *reason);
#endif
