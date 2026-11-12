#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module="$d/../../../../j1708_17pid/src"
binary=$(mktemp /tmp/tb217-diagnostic.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
cc -std=c11 -D_POSIX_C_SOURCE=200809L -DTB_DIAGNOSTIC_HOST_TEST -DTB_COUNTER_HOST_TEST \
  -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$IDF_PATH/components/json/cJSON" -I"$d/.." -I"$module" \
  "$IDF_PATH/components/json/cJSON/cJSON.c" "$d/../diagnostics.c" "$d/../ecu_counters.c" \
  "$d/../tbx_ecu_counters.c" "$d/../tbx_volvo_operating.c" "$d/../tbx_j1587_bridge.c" \
  "$module/tbx_j1587_diagnostics.c" "$module/tbx_j1587_decoder.c" "$module/tbx_j1587_passive.c" \
  "$module/tbx_j1587_passive_full.c" "$module/tbx_j1708_requests.c" "$d/test_volvo_diagnostics.c" -lm -o "$binary"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1 "$binary"
