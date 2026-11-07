#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -pedantic -I"$root" "$root/tbx_trip.c" "$root/tests/test_tbx_trip.c" -lm -o "$work_dir/test_trip"
"$work_dir/test_trip"
