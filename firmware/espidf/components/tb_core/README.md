# Core acquisition and persistence

This component targets the ESP32-C6-WROOM-1 pinout used by the fabricated B1.
The Core CAN transceiver TX input is pulled high in hardware and is not
connected to an ESP32 output. CAN therefore runs in listen-only mode.

| Function | GPIO |
| --- | --- |
| CAN RX / mode | 18 / 2 (mode LOW enables normal reception) |
| TWAI internal TX | 4, not connected to the transceiver |
| J1708 RX / DE | 20 / 21 (DE LOW, receive only) |
| Unconnected UART1 TX | 5 |
| SPI clock / MOSI / MISO | 6 / 7 / 8 |
| SD CS / FRAM CS | 3 / 10 |
| Link power / SD power | 16 / 17, untouched unless repaired power gates are explicitly enabled |
| Watchdog DONE | 1, always input on this B1 firmware |

USB Serial/JTAG uses GPIO12/13. This component never enables the UART0 console
or uses the physical Core-Link UART traces.

## Signals and transport

CAN frames are available on TCP35000 in the existing Android application
format: TB1,timestampMs,idHex,pgn,sourceAddress,dlc,dataHex. A bounded queue and
four nonblocking clients prevent a slow consumer from blocking acquisition.
Before the wall clock is established, that legacy timestamp is uptime in
milliseconds; persisted samples always include monotonic uptime and boot ID.

Validated J1939 EEC1/CCVS/fuel-rate/distance messages supply speed, RPM, fuel
rate and odometer values. Trip distance and trip fuel integrate successive
speed/rate messages only across gaps of at most three seconds; unobserved
intervals are not invented. Trip counters have NVS checkpoints every 30
seconds and an explicit /api/reset operation.

J1708 uses UART1 at 9600 8N1, receive only. The hardware RX idle timeout is one
character (10 bits). Frames must fit the 21-byte J1708 limit and pass the
additive checksum. UART overruns, oversize/coalesced messages and checksum
failures are visible in /api/state; this is not a claim of timing-perfect
capture on an untested bus. Valid J1587 messages with MID >=128 decode PID84
speed, PID190 RPM, PID183 fuel rate and PID245 distance. PID255 selects page2
for the remainder of a message. Proprietary/unknown messages remain available
as the most recent raw diagnostic frame rather than being misinterpreted.

GPS updates come from the authenticated Link telemetry endpoint or the
existing Android GPS form endpoint. Coordinates and optional fields are
range-checked; Link-reported age is preserved. A fix expires after 15 seconds,
and a Link report of loss of fix invalidates that source immediately.

The uploader receives one operational snapshot per second while fresh vehicle
or GPS observations exist, plus one health sample per minute. Operational
samples omit unavailable fields instead of substituting zero or null. Raw bus
frames are not uploaded as empty operational database rows. The Cloud batch
schema stays compatible with the Android gateway.

## Persistence and acknowledgements

Records use CRC-protected JSON Lines, monotonically reserved sequence ranges
and a durable NVS acknowledgement watermark. The batch API flattens the
internal on-disk wrapper for the Cloud ingest contract. Sending a batch does
not acknowledge it. The Link must return a successful acknowledgement for a
sequence actually present in the transmitted batch before records can be
removed.

The internal flash fallback is intended for commissioning and short outages.
It holds up to 24 segments of 48 KiB (approximately 1.125 MiB); the SD backend
holds up to 128 segments of 1 MiB (128 MiB). Record sizes determine actual
retention time. Mounting and recovery run in the storage worker, leaving
the web/OTA services available while a large backlog is being checked.
SD logging requires the B1 power-gate repair and an existing readable FAT
filesystem; this firmware does not format an inserted card. Internal flash
is formatted automatically only when the entire storage partition was first
verified erased. Corrupt or unknown existing filesystems are preserved and
reported as unavailable.

Queue, capacity, corruption and I/O losses are explicit diagnostics. An
unacknowledged backlog is preserved when full; new records are dropped with a
counter rather than silently erasing old data.
The selected medium remains pinned while it contains unacknowledged records.
Removing an SD card with a backlog does not silently start a new flash queue
whose later acknowledgements could discard those older records. SD directories
are separated by hardware device identity.

The optional MB85RS64 FRAM stores a two-slot metadata journal, not the entire
telemetry backlog. SPI remains off by default while the board power-gate
footprints are uncorrected. FRAM initialization preserves unknown data and
requires validated write-enable behavior, not just an all-FF read.

## Tests

Run the scripts in tests/ on a host with a C compiler and Python3. The protocol
tests include sanitizers and independent JSON parsing. Storage and FRAM codec
tests cover checksum corruption and interrupted record/slot writes. They do
not replace ESP-IDF builds or physical CAN, J1708, SD and FRAM tests.
