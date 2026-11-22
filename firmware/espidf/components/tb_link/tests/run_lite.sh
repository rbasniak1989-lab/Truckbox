#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
base="$root/components/tb_base"
link="$root/components/tb_link"
cjson="${IDF_PATH:?Set IDF_PATH to ESP-IDF v5.5.2}/components/json/cJSON"
test_binary=$(mktemp /tmp/tblite-tests.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
for test in test_lite test_link_continuity; do
    cc -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -fno-omit-frame-pointer \
        -fsanitize=address,undefined -I"$base/include" -I"$link" -I"$cjson" \
        "$base/lite_wire.c" "$link/tb_link_telemetry_codec.c" "$link/tb_gps_outbox.c" "$link/tb_link_journal.c" \
        "$link/tests/$test.c" "$cjson/cJSON.c" -lm -o "$test_binary"
    ASAN_OPTIONS=detect_leaks=0 "$test_binary"
done
