#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp /tmp/tb-crash.XXXXXX)
trap 'rm -f "$w"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$IDF_PATH/components/json/cJSON" "$d/test_crash.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -o "$w"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w"
