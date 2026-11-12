#!/usr/bin/env sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
core_dir=$(CDPATH= cd -- "$test_dir/.." && pwd)
repo_firmware=$(CDPATH= cd -- "$core_dir/../../.." && pwd)
cjson_dir="${IDF_PATH:?Set IDF_PATH to ESP-IDF v5.5.2}/components/json/cJSON"
module="$repo_firmware/j1708_17pid/src"
test_binary=$(mktemp /tmp/tbx-telemetry.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$core_dir" -I"$module" -I"$cjson_dir" \
    "$test_dir/test_telemetry.c" "$core_dir/tbx_transmission.c" "$core_dir/tbx_volvo_operating.c" "$core_dir/protocols.c" \
    "$core_dir/tbx_j1939.c" "$core_dir/tbx_j1939_tp.c" "$core_dir/tbx_j1587_bridge.c" \
    "$module/tbx_j1587_decoder.c" "$module/tbx_j1587_passive.c" \
    "$module/tbx_j1587_passive_full.c" "$module/tbx_j1708_requests.c" \
    "$cjson_dir/cJSON.c" -lm -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
