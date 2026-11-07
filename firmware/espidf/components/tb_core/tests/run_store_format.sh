#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp /tmp/truckbox-store.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$test_dir/.." "$test_dir/test_store_format.c" -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
