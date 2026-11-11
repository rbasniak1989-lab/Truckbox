#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$(mktemp /tmp/tb-j1708-field.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I"$test_dir/.." -I"$test_dir/../../../../j1708_17pid/src" \
 "$test_dir/test_j1708_field.c" "$test_dir/../tbx_j1708_active.c" "$test_dir/../tbx_j1708_field.c" "$test_dir/../tbx_j1708_rx_guard.c" \
 "$test_dir/../../../../j1708_17pid/src/tbx_j1708_requests.c" -lm -o "$binary"
ASAN_OPTIONS=detect_leaks=1 "$binary"
