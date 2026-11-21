#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TB_LITE_SNAPSHOT_PATH "/api/telemetry/snapshot"
#define TB_LITE_SNAPSHOT_MAX 1440u
#define TB_LITE_RECORD_MAX 1152u
#define TB_LITE_SAFE_SEQ UINT64_C(9007199254740991)
#define TB_LITE_EPOCH_MS UINT64_C(1704067200000)
#define TB_LITE_MAX_AGE_MS 65000u
#define TB_LITE_OPERATIONAL_MS 10000u
#define TB_LITE_HEALTH_MS 60000u

/* Order is part of version 1: bit n qualifies the nth field. */
typedef enum {
    TB_LITE_SPEED, TB_LITE_RPM, TB_LITE_PEDAL, TB_LITE_GEAR, TB_LITE_SELECTED_GEAR,
    TB_LITE_ODOMETER, TB_LITE_PID250, TB_LITE_FUEL_RATE, TB_LITE_TORQUE,
    TB_LITE_OIL_PRESSURE, TB_LITE_FUEL_PRESSURE, TB_LITE_BOOST,
    TB_LITE_AIR1, TB_LITE_AIR2, TB_LITE_COOLANT, TB_LITE_OIL_TEMP,
    TB_LITE_INTAKE, TB_LITE_GEARBOX, TB_LITE_LOAD, TB_LITE_FIELDS
} tb_lite_field_t;

#define TB_LITE_BIT(field) (UINT32_C(1) << (field))
#define TB_LITE_ALL (TB_LITE_BIT(TB_LITE_FIELDS) - 1U)

typedef struct {
    uint16_t pid;
    uint8_t mid;
    uint32_t ttl_ms;
    const char *key;
} tb_lite_signal_t;

extern const tb_lite_signal_t tb_lite_signals[TB_LITE_FIELDS];

enum { TB_LITE_RX_READY = 1u, TB_LITE_TX_ARMED = 2u, TB_LITE_MID_CONFLICT = 4u };

typedef struct {
    char uid[33];
    uint32_t boot_id, age_ms, valid, stale, estimated;
    uint64_t local_seq, uptime_ms, unix_ms;
    double values[TB_LITE_FIELDS];
    uint32_t frames, sent, answered, timeouts, guard, heap_minimum;
} tb_lite_snapshot_t;

typedef struct {
    char *data;
    size_t capacity, used;
    bool failed;
} tb_lite_buffer_t;

bool tb_lite_append(tb_lite_buffer_t *buf, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
bool tb_lite_uid_valid(const char *uid);
size_t tb_lite_snapshot_encode(const tb_lite_snapshot_t *snapshot, char *buffer, size_t capacity);
