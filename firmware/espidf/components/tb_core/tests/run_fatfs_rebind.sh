#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp -d /tmp/tb-fatfs-rebind.XXXXXX)
trap 'rm -rf "$w"' EXIT HUP INT TERM
: "${IDF_PATH:?Activate ESP-IDF 5.5.2}"
python3 - "$d/../store.c" "$w/rebind_under_test.h" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text();start=s.index('static int rebind_flash_volume(')
end=s.index('{',start)+1;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
Path(sys.argv[2]).write_text(s[start:end])
PY
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$w" -I"$d/fatfs_stubs" -I"$IDF_PATH/components/fatfs/src" \
    "$d/test_fatfs_rebind.c" "$IDF_PATH/components/fatfs/src/ff.c" \
    "$IDF_PATH/components/fatfs/src/ffunicode.c" -o "$w/test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w/test"
