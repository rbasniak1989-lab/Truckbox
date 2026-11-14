#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp /tmp/tb-store-open.XXXXXX)
trap 'rm -f "$w"' EXIT HUP INT TERM
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$d/.." -I"$IDF_PATH/components/json/cJSON" \
    "$d/test_store_open.c" "$d/../store_codec.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$w"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w"
