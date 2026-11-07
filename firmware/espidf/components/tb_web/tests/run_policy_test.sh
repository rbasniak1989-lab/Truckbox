#!/usr/bin/env sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
component_dir=$(dirname -- "$test_dir")
: "${IDF_PATH:?Set IDF_PATH to an ESP-IDF v5.5.2 checkout}"
test_binary=$(mktemp /tmp/truckbox-policy-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
"${CC:-cc}" -std=gnu17 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I"$test_dir/host_include" -I"$component_dir" \
    -I"$IDF_PATH/components/bootloader_support/include" \
    -I"$IDF_PATH/components/esp_app_format/include" \
    "$component_dir/tb_image_policy.c" "$test_dir/test_image_policy.c" -o "$test_binary"
"$test_binary"
