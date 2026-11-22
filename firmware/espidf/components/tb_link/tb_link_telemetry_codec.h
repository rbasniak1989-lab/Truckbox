#pragma once

#include "tb_lite_wire.h"

typedef struct {
    uint32_t boot_id;
    uint64_t local_seq, captured_ms;
} tb_lite_cursor_t;

typedef struct {
    uint64_t started_ms, odometer_ms, pid250_ms;
    double odometer, pid250;
} tb_lite_reference_t;

typedef enum { TB_LITE_NO_EVENT, TB_LITE_CORE_LOST, TB_LITE_CORE_RESTORED,
               TB_LITE_CORE_BOOT, TB_LITE_MID_STATE, TB_LITE_CORE_SEEN } tb_lite_event_t;

typedef struct {
    uint32_t boot, misses;
    bool disconnected;
} tb_lite_contact_t;

typedef struct {
    uint64_t link_uptime_ms, cloud_last_success_ms;
    uint32_t link_boot_id, link_reset_reason, heap_free, heap_minimum, largest_block;
    uint32_t pending, dropped, capacity, used, oldest_age, replayed, consecutive_failures;
    uint32_t link_disconnects, poll_failures, dropped_operational, dropped_health, recovered_tail_bytes;
    int wifi_rssi, internet_reachable, last_http_status; /* Reachability: -1 unknown. */
    bool wifi_connected, cloud_connected, oldest_age_valid;
    const char *disconnect_reason, *cloud_error_stage;
} tb_lite_link_health_t;

bool tb_lite_snapshot_decode(const char *json, size_t size, tb_lite_snapshot_t *snapshot);
bool tb_lite_candidate(const tb_lite_cursor_t *cursor, const tb_lite_snapshot_t *snapshot,
                       uint64_t link_now_ms, uint64_t *captured_ms);
bool tb_lite_reference_update(tb_lite_reference_t *ref, const tb_lite_snapshot_t *s, uint64_t captured_ms);
bool tb_lite_reference_valid(const tb_lite_reference_t *ref);
size_t tb_lite_operational(char *json, size_t capacity, uint64_t seq, uint64_t captured_ms,
                           const tb_lite_snapshot_t *s, const tb_lite_reference_t *ref);
size_t tb_lite_health(char *json, size_t capacity, uint64_t seq, uint64_t epoch_ms,
                      const tb_lite_snapshot_t *s, bool online, const tb_lite_link_health_t *link,
                      tb_lite_event_t event);
bool tb_lite_record_meta(const char *record, uint64_t *sequence, bool *health, uint64_t *captured);
bool tb_lite_ack_valid(const char *response, size_t size, int status, uint64_t offered,
                       unsigned operational, unsigned health);
bool tb_lite_sequence_range(uint64_t floor, uint64_t previous_end, uint64_t *first, uint64_t *end);
tb_lite_event_t tb_lite_contact_update(tb_lite_contact_t *contact, uint32_t observed_boot);
uint32_t tb_lite_retry_delay(unsigned failures, int status);
