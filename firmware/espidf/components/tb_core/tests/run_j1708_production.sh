#!/bin/sh
set -eu
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
module="$dir/../../../../j1708_17pid/src"
binary=$(mktemp /tmp/tb216-production.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I"$dir/.." -I"$module" "$dir/test_j1708_production.c" "$dir/../tbx_j1708_auto.c" "$dir/../tbx_transmission.c" \
 "$dir/../tbx_j1708_active.c" "$dir/../tbx_j1708_field.c" "$dir/../tbx_j1708_rx_guard.c" "$module/tbx_j1708_requests.c" -lm -o "$binary"
ASAN_OPTIONS=detect_leaks=1 "$binary"
