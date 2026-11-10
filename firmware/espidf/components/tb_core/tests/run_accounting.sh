#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
b=$(mktemp /tmp/tb-accounting.XXXXXX)
trap 'rm -f "$b"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g -I"$d/.." "$d/test_accounting.c" "$d/../tbx_trip.c" "$d/../tbx_fuel_account.c" -lm -o "$b"
ASAN_OPTIONS=detect_leaks=0 "$b"
