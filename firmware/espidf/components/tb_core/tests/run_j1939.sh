#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -pedantic -I"$root" "$root/tbx_j1939.c" "$root/tests/test_tbx_j1939.c" -lm -o "$work_dir/test_j1939"
"$work_dir/test_j1939"
