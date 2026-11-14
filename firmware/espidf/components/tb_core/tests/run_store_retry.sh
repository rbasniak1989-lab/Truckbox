#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp -d /tmp/tb-store-retry.XXXXXX)
trap 'rm -rf "$w"' EXIT HUP INT TERM
python3 - "$d/../core.c" "$w/retry_handler_under_test.h" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text();start=s.index('static esp_err_t storage_retry_handler(')
end=s.index('{',start)+1;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
Path(sys.argv[2]).write_text(s[start:end])
PY
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$w" -I"$d/.." -I"$IDF_PATH/components/json/cJSON" \
    "$d/test_store_retry.c" "$d/../store_codec.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$w/test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w/test"
