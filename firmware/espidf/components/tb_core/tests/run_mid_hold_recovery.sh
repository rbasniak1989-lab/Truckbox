#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=$(mktemp /tmp/tb-mid-hold-recovery.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g     "$test_dir/test_mid_hold_recovery.c" -o "$bin"
ASAN_OPTIONS=detect_leaks=0 "$bin"
