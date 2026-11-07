#ifndef TBX_J1939_TP_H
#define TBX_J1939_TP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TBX_J1939_TP_MAX_PAYLOAD 64u
#define TBX_J1939_TP_SESSIONS 4u
#define TBX_J1939_TP_TIMEOUT_MS UINT64_C(1250)
/* Passive TP.BAM only. No CTS, request, acknowledgement or other transmission. */
typedef struct {
    uint32_t pgn;
    uint8_t sa, da, packets, length;
    uint64_t start_ms, completed_ms;
    uint64_t start_capture_id, completed_capture_id;
    uint8_t data[TBX_J1939_TP_MAX_PAYLOAD];
} tbx_j1939_tp_message_t;
typedef struct {
    bool active;
    uint8_t expected_sequence;
    uint64_t last_ms;
    tbx_j1939_tp_message_t message;
} tbx_j1939_tp_session_t;
typedef struct {
    uint32_t announcements, completed, timeouts, invalid_length,
        invalid_sequence, orphan_packets, unsupported_control,
        unsupported_pgn, invalid_destination, capacity_rejected, restarted;
    tbx_j1939_tp_session_t sessions[TBX_J1939_TP_SESSIONS];
} tbx_j1939_tp_t;
typedef enum {
    TBX_J1939_TP_IGNORED, TBX_J1939_TP_STARTED, TBX_J1939_TP_PROGRESS,
    TBX_J1939_TP_COMPLETE, TBX_J1939_TP_REJECTED
} tbx_j1939_tp_result_t;
typedef void (*tbx_j1939_tp_emit_fn)(const tbx_j1939_tp_message_t *, void *);
void tbx_j1939_tp_init(tbx_j1939_tp_t *transport);
/* Call periodically if timeout diagnostics must advance while CAN is idle. */
void tbx_j1939_tp_expire(tbx_j1939_tp_t *transport, uint64_t now_ms);
/* Input is always a physical classic CAN frame of at most 8 bytes. Every raw
 * frame must still be logged by caller, including rejected transport frames.
 * Only PGN 65251 and 65249 BAM sessions are assembled. The completion callback
 * is synchronous and its message pointer is invalid after the callback returns.
 * Consumer must use an explicit application-payload API, never claim DLC >8. */
tbx_j1939_tp_result_t tbx_j1939_tp_feed(tbx_j1939_tp_t *transport,
    uint32_t can_id, bool extended, bool rtr, const uint8_t *data, size_t length,
    uint64_t captured_ms, tbx_j1939_tp_emit_fn emit, void *context);
/* Same parser with exact caller-assigned capture identities for CM/last DT. */
tbx_j1939_tp_result_t tbx_j1939_tp_feed_capture(tbx_j1939_tp_t *transport,
    uint32_t can_id, bool extended, bool rtr, const uint8_t *data, size_t length,
    uint64_t captured_ms, uint64_t capture_id, tbx_j1939_tp_emit_fn emit, void *context);
#endif
