#ifndef TBX_J1708_RX_GUARD_H
#define TBX_J1708_RX_GUARD_H
#include <stdbool.h>
#include <stdint.h>
/* Policy decisions, not SAE electrical limits: reuse the 10 s observation and
 * three-failure persistence. Reference age = learning + confirmation horizon.
 * Wilson 99% intervals compare observed error proportions, not TX causality. */
#define TBX_RX_WINDOW_US UINT64_C(10000000)
#define TBX_RX_REFERENCE_WINDOWS 3u
#define TBX_RX_PERSISTENCE 3u
#define TBX_RX_REFERENCE_MAX_US (TBX_RX_WINDOW_US*(TBX_RX_REFERENCE_WINDOWS+TBX_RX_PERSISTENCE))
#define TBX_RX_WILSON_Z2 6.6348966010212145

typedef enum { TBX_RX_VALID,TBX_RX_CHECKSUM,TBX_RX_FRAMING,TBX_RX_OVERRUN,TBX_RX_UART_FRAME,TBX_RX_READ_ERROR } tbx_rx_kind_t;
typedef enum {
    TBX_GUARD_NONE,TBX_GUARD_RX_QUALITY,TBX_GUARD_RX_UART,
    TBX_GUARD_RX_UNUSABLE,TBX_GUARD_RX_SILENCE,TBX_GUARD_CLOCK,
    TBX_GUARD_RX_DURING_TX,TBX_GUARD_TX_DRIVER,TBX_GUARD_ECHO_TIMEOUT,
    TBX_GUARD_ECHO_DIVERGENCE,TBX_GUARD_REPLY_TIMEOUT,
    TBX_GUARD_MID_CONFLICT,TBX_GUARD_AUTHORITY,TBX_GUARD_OPERATOR,
    TBX_GUARD_AUDIT,TBX_GUARD_OTHER
} tbx_guard_cause_t;
enum { TBX_GUARD_CRC_ELEVATED=1,TBX_GUARD_FRAME_ELEVATED=2,
    TBX_GUARD_UART_FAULT=4,TBX_GUARD_MAJORITY_INVALID=8,
    TBX_GUARD_PERSISTENT=16,TBX_GUARD_SILENT=32,TBX_GUARD_CLOCK_INVALID=64,
    TBX_GUARD_RX_BLIND_TX=128,TBX_GUARD_REQUEST_FAILURES=256,
    TBX_GUARD_REFERENCE_READY=512,TBX_GUARD_WINDOW_STALE=1024 };
typedef struct {uint32_t valid,checksum,framing,overruns,uart_frame,read_errors;}tbx_rx_counts_t;
typedef struct {uint64_t start_us,end_us;tbx_rx_counts_t count;}tbx_rx_window_t;
typedef struct {
    uint64_t at_us;
    tbx_rx_window_t window,reference;
    tbx_rx_counts_t totals;
    uint32_t sent,candidates,timeouts,echoes,collisions,echo_timeouts,tx_errors;
    uint32_t current_lower_ppm,reference_upper_ppm;
    uint16_t predicates,pid;
    uint8_t target_mid,streak,before_flags,after_flags,request_failures;
    uint32_t timeout_ms,spacing_ms;
    tbx_guard_cause_t cause,last_failure;
    bool stopped;
} tbx_rx_decision_t;
typedef struct {
    tbx_rx_counts_t total,boundary;
    tbx_rx_window_t reference[TBX_RX_REFERENCE_WINDOWS],last_window;
    uint64_t start_us,last_valid_us,last_observed_us;
    uint8_t reference_count,reference_cursor,suspect_streak;
    bool initialized,observed,seen_valid,clock_fault,warned,fault_latched,decision_pending;
    tbx_guard_cause_t last_failure;
    tbx_rx_decision_t decision;
} tbx_j1708_rx_guard_t;
struct tbx_j1708_active;
void tbx_j1708_rx_note(struct tbx_j1708_active *,tbx_rx_kind_t,uint64_t now_us);
void tbx_j1708_rx_tick(struct tbx_j1708_active *,uint64_t now_us);
void tbx_j1708_rx_prepare_stop(struct tbx_j1708_active *,const char *reason);
void tbx_j1708_rx_stopped(struct tbx_j1708_active *,const char *reason,uint8_t before_flags);
void tbx_j1708_rx_reconfigure(struct tbx_j1708_active *);
void tbx_j1708_rx_uart_fault(struct tbx_j1708_active *,tbx_rx_kind_t,uint64_t now_us);
const char *tbx_j1708_guard_cause_name(tbx_guard_cause_t);
uint8_t tbx_j1708_active_flags(const struct tbx_j1708_active *);
#endif
