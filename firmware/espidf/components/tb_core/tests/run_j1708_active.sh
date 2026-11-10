#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp /tmp/truckbox-j1708-active.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$test_dir/.." -I"$test_dir/../../../../j1708_17pid/src" \
    "$test_dir/test_tbx_j1708_active.c" "$test_dir/../tbx_j1708_active.c" "$test_dir/../tbx_j1708_field.c" \
    "$test_dir/../../../../j1708_17pid/src/tbx_j1708_requests.c" \
    -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
