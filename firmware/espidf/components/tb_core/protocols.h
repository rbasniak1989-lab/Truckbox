#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Formats one classic CAN record. Remote frames carry no payload. The supplied
 * timestamp is monotonic uptime, not UTC. Returns false for an invalid frame or
 * insufficient capacity, leaving out empty rather than exposing partial JSON. */
bool tb_protocol_can_json(char *out, size_t capacity, uint64_t uptime_us,
                          uint32_t identifier, bool extended, bool rtr,
                          uint8_t dlc, const uint8_t *data);

/* J1708 messages may contain proprietary data rather than J1587 parameters.
 * Checksum validation therefore accepts a MID and checksum (2..21 bytes).
 * The JSON formatter preserves these frames and reports J1587 layout validity
 * separately; a valid checksum does not establish semantic J1587 conformance. */
bool tb_protocol_j1708_checksum_valid(const uint8_t *frame, size_t length);
bool tb_protocol_j1708_json(char *out, size_t capacity, uint64_t uptime_us,
                            const uint8_t *frame, size_t length);
