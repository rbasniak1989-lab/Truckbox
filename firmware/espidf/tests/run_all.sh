#!/bin/sh
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "$IDF_PATH"
export ASAN_OPTIONS=detect_leaks=0
cd "$test_root"
sh components/tb_base/tests/run_host_tests.sh
sh components/tb_base/tests/run_json_memory.sh
sh components/tb_core/tests/run_diagnostic_memory.sh
sh components/tb_web/tests/run_json_http.sh
sh components/tb_base/tests/run_peer_address.sh
sh components/tb_web/tests/run_policy_test.sh
sh components/tb_core/tests/run.sh
sh components/tb_core/tests/run_store_format.sh
sh components/tb_core/tests/run_fram.sh
sh components/tb_core/tests/run_store_transactions.sh
sh components/tb_core/tests/run_store_codec.sh
sh components/tb_core/tests/run_store_recovery.sh
sh components/tb_core/tests/run_store_worker.sh
sh components/tb_core/tests/run_worker_fairness.sh
sh components/tb_core/tests/run_legacy_profile.sh
sh components/tb_core/tests/run_j1939.sh
sh components/tb_core/tests/run_j1939_tp.sh
python3 ../j1939/tests/test_inventory.py
python3 ../j1939/tests/test_reference_equivalence.py
sh ../j1708_17pid/tests/run_all.sh
sh components/tb_core/tests/run_j1587_bridge.sh
sh components/tb_core/tests/run_trip.sh
sh components/tb_core/tests/run_accounting.sh
sh components/tb_core/tests/run_capture_integration.sh
sh components/tb_core/tests/run_core_gps.sh
sh components/tb_core/tests/run_telemetry.sh
sh components/tb_core/tests/run_j1708_active.sh
sh components/tb_core/tests/run_j1708_field.sh
sh components/tb_core/tests/run_field_audit.sh
sh components/tb_core/tests/run_request_event_policy.sh
sh components/tb_core/tests/run_j1708_uart.sh
sh components/tb_link/tests/run_wifi_uplink.sh
test_binary=$(mktemp /tmp/tb-link-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Icomponents/tb_link components/tb_link/tb_nmea.c \
    components/tb_link/tests/test_nmea.c -lm -o "$test_binary"
"$test_binary"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -Icomponents/tb_link components/tb_link/tb_gps_outbox.c \
    components/tb_link/tests/test_gps_outbox.c -o "$test_binary"
"$test_binary"
python3 -m unittest discover -s tests -p 'test_*.py' -v
sh components/tb_core/tests/run_offline_2h.sh
printf '%s\n' 'Host tests passed; ESP32 physical tests remain pending.'
