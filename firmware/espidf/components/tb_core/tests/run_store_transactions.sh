#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${IDF_PATH:?Activate ESP-IDF or set IDF_PATH for its bundled cJSON}"
test_binary=$(mktemp /tmp/truckbox-store-txn.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$test_dir/.." -I"$IDF_PATH/components/json/cJSON" \
    "$test_dir/test_store_transactions.c" "$IDF_PATH/components/json/cJSON/cJSON.c" \
    -lm -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
