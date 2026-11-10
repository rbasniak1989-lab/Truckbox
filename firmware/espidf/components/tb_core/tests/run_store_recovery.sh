#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d /tmp/tb-recovery.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -O1 -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g -I"$test_dir/.." -I"$IDF_PATH/components/json/cJSON" "$test_dir/test_store_recovery.c" "$test_dir/../store_codec.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work/test"
ASAN_OPTIONS=detect_leaks=0 "$work/test"
