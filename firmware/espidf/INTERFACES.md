# Truckbox B1 firmware implementation contract

ESP-IDF v5.5.2, ESP32-C6-WROOM-1, 8 MiB flash. Two builds, `truckbox_core` and `truckbox_link`, from one project. All console output uses USB Serial/JTAG; no application UART0 console. Two 3 MiB OTA application slots and rollback. Do not drive GPIO1 on the Core: the B1 watchdog footprint is incorrect.

## Ownership

Root: project/build/release, shared config/network/runtime, integration.
Web agent: `components/tb_web/` only.
Core agent: `components/tb_core/` only.
Link agent: `components/tb_link/` only.
Review agent: read-only pin audit.

## Network topology

Core runs STA on the user's 2.4 GHz router/hotspot and keeps its private WPA2 AP.
In 0.2.2, a configured external `wifi_ssid` makes Link use router DHCP and upload
HTTPS through Wi-Fi without LTE. Core and Link must share a reachable LAN.
Link reports to `core_host`; Core learns its socket address only after pair-key
authentication and validation against its local subnets, with a 60-second TTL.
Core uses that address for forwarding batches and OTA. If Link's external SSID
is empty, the original private STA 192.168.4.2 topology and LTE path remain.
UART0 receives GNSS and UART1 is reserved for optional LTE; the physical UART
Core–Link remains unused. Core performs no cloud HTTPS or Internet NAT.
AP credentials, pair authentication, Android APIs and NVS are preserved.
No credentials are embedded in the public sources. See WIFI_LINK_CLOUD_v0.2.2.md.

## Confirmed pins (module WROOM, not MINI)

Core: CAN RX18, transceiver mode2 (LOW normal), spare TWAI TX4 not connected to transceiver, J1708 RX20/DE21, SPI CLK6/MOSI7/MISO8, SD CS3, FRAM CS10. GPIO16/17 are faulty power-gate controls and must remain inputs until repaired/explicitly commissioned. GPIO1 watchdog must remain input. USB12/13, BOOT9. CAN capture is physically receive-only: TXD has a hardware pull-up and no MCU connection.

Link: GNSS UART0 RX17 (TX16 unused), LTE UART1 RX10/TX11, modem PWRKEY0 (confirm new transistor footprint before enabling pulses). Physical Core-Link UART22/23 is unused and remains input.

## Common APIs

See `components/tb_base/include/tb.h`. Configuration writes use NVS and are committed before reboot. Secret values are never returned by public state/config endpoints. Core records telemetry as bounded JSON Lines with monotonic sequence IDs; acknowledge only data accepted by a consumer. No simulated vehicle data. Backend uploader accepts an HTTPS endpoint and API token configured by the user; no hardcoded service URL.

OTA accepts an application `.bin` for the matching role only. It verifies ESP image/chip/project name/size using IDF and requires an authenticated upload. Never change the boot partition until the complete image is validated. Boot confirmation requires network and web services to start; missing Wi-Fi credentials, GPS fix, modem signal or vehicle frames are not firmware boot failures. Hardware tests remain necessary; firmware does not correct mirrored footprints.
