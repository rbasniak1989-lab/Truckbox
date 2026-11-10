#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
idf_root=${IDF_PATH:-/workspace/.idf/esp-idf}
cjson_dir="$idf_root/components/json/cJSON"
test_binary=$(mktemp /tmp/truckbox-config-tests.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
for role in 0 1 2; do
    profile=""
    if [ "$role" = 2 ]; then role=0;profile="-DCONFIG_TB_J1708_ONLY=1"; fi
    cc -std=c11 -D_POSIX_C_SOURCE=200809L -DCONFIG_TB_ROLE_LINK="$role" $profile \
        -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
        -I"$test_dir/stubs" -I"$test_dir/../include" -I"$cjson_dir" \
        "$test_dir/test_config.c" "$cjson_dir/cJSON.c" -lm -o "$test_binary"
    "$test_binary"
done
