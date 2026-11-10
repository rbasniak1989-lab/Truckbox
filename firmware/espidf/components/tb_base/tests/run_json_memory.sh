#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${IDF_PATH:?Activate ESP-IDF}"
work_dir=$(mktemp -d /tmp/tb-json-memory.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$test_dir/../include" -I"$IDF_PATH/components/json/cJSON" \
  "$test_dir/../tb_json.c" "$test_dir/test_json_memory.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$work_dir/test"
