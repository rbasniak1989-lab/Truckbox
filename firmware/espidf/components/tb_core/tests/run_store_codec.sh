#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
b=$(mktemp /tmp/tb-codec.XXXXXX)
trap 'rm -f "$b"' EXIT HUP INT TERM
cc -O1 -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g -I"$d/.." "$d/test_store_codec.c" "$d/../store_codec.c" -o "$b"
ASAN_OPTIONS=detect_leaks=0 "$b"
