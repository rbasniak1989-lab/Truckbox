# Link: Wi-Fi/LTE uplink, GNSS and persistent GPS outbox

Target: ESP-IDF 5.5.2, ESP32-C6-WROOM-1. This component is compiled only for the Link role.

## Physical interfaces

- UART0 receives ATGM336H NMEA on GPIO17 at the configured baud rate (default 9600). It does not send GNSS commands.
- UART1 runs standard AT/PPP for the A7638E through the official `esp_modem` 1.4.0 generic DCE, RX GPIO10 and TX GPIO11, 115200 baud, no hardware flow control.
- Q50 PWRKEY is activated with GPIO0 HIGH for 1000 ms at most once per boot, only when cellular operation is explicitly enabled and command synchronization fails after mode detection. The firmware does not repeatedly toggle a nonresponding modem.
- Physical Core-Link UART GPIO22/23 is unused. Link uses the router LAN when an external wifi_ssid is configured, or the original Core private AP otherwise. Both UARTs remain available for GNSS and optional LTE.

The standard AT/PPP implementation still requires validation against the actual A7638E modem firmware and carrier. SIM, APN and radio failures are reported and retried; they are not treated as firmware boot failures. No SIM PIN is guessed. Correct the board's power-gate/SIM-ground faults before expecting operational cellular service.

## HTTPS transport

Wi-Fi uploads use the router STA interface explicitly, without requiring a modem, APN or PPP. The Link's SNTP clock is synchronized from the Internet without GPS. With the original private STA mode, PPP remains available and HTTPS is bound to it. A private Core AP connection is never treated as Internet access. See ../../WIFI_LINK_CLOUD_v0.2.2.md for topology and configuration.

HTTPS requires the ESP certificate bundle and a synchronized UTC clock. UTC can initially come from a valid RMC sentence; SNTP starts after PPP comes up. TLS certificate verification is always enabled. Redirects are disabled, response memory is capped at 4095 bytes and asynchronous requests have a 30-second application deadline plus bounded network operation timeouts.

Core batches arrive at `POST /api/uplink`, authenticated with the private pairing key. Link adds the Core's individual cloud token, preserves UID/boot/uptime and forwards samples. Both JSON trees are freed before waiting for HTTPS. A 2xx status alone is insufficient: `ok: true` and an integral `ack_seq` present in the exact offered sample IDs are mandatory. Failure never acknowledges the Core queue.

The GPS endpoint uses its own configured device UID and token. Records contain `source: TRUCKBOX_GNSS`, `provider: ATGM336H` and an idempotent `client_uid` derived from the Link hardware MAC and GNSS UTC timestamp. Speed, heading and altitude are omitted when not measured. HDOP is not misrepresented as an accuracy in meters.

## Persistence and cadence

GNSS status is sent to the Core once per second. Fresh, UTC-stamped fixes are appended to the Link flash outbox once per five seconds, even if cellular service or cloud credentials are unavailable.

The Link's independent `storage` FAT partition is initialized automatically only if a complete pre-mount scan proves that every byte is erased. Unknown/corrupt storage is preserved and reported.

The GPS queue uses CRC32-protected JSON lines, synchronized appends, bounded 768 KiB retention and an append-only acknowledgement journal. Checkpoints occur every eight accepted points; a reset can replay the accepted points since the last checkpoint, including eight if the checkpoint write was interrupted. Cloud `client_uid` deduplication handles such replay. Compaction copies only unacknowledged records and retains the original until the replacement is synchronized. Recovery truncates incomplete final writes but refuses a complete line with an invalid CRC. A full queue retains old unacknowledged points and counts rejected new ones.

The queue contains coordinates and timestamps, not cloud authentication tokens. Authentication fields are added only when sending over HTTPS. Upload retries use exponential backoff, capped at five minutes. Authentication errors do not discard records.

## Portable verification

From a host with GCC:

```sh
gcc -std=c11 -Wall -Wextra -Werror -I . tb_nmea.c tests/test_nmea.c -lm -o /tmp/test_nmea
/tmp/test_nmea
gcc -std=c11 -Wall -Wextra -Werror -I . tb_gps_outbox.c tests/test_gps_outbox.c -o /tmp/test_gps_outbox
/tmp/test_gps_outbox
```

The tests cover checksum/range/date handling, hemisphere conversion, loss of fix, UTC midnight rollover, persistent queue replay, retry, compaction, incomplete-write recovery, retention limits and CRC corruption.
