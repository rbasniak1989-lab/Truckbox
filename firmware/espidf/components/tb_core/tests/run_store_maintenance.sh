#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp -d /tmp/tb-maintenance.XXXXXX)
trap 'rm -rf "$w"' EXIT HUP INT TERM
python3 - "$d/../core.c" "$w/maintenance_handlers_under_test.h" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text();parts=[]
for name in ['storage_maintenance_handler','storage_backup_handler']:
 start=s.index('static esp_err_t '+name+'(');end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 parts.append(s[start:end])
Path(sys.argv[2]).write_text('\n'.join(parts))
PY
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$w" -I"$d/.." -I"$IDF_PATH/components/json/cJSON" \
    "$d/test_store_maintenance.c" "$d/../store_codec.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$w/test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w/test"
