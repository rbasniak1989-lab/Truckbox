#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp -d /tmp/tb-backup.XXXXXX)
trap 'rm -rf "$w"' EXIT HUP INT TERM
python3 - "$d/../store_maintenance.h" "$w/backup_under_test.h" <<'PY'
from pathlib import Path
import sys,re
s=Path(sys.argv[1]).read_text();parts=[]
for name in ['maintenance_partition','tb_store_flash_backup']:
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m
 start=m.start();end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 parts.append(s[start:end])
Path(sys.argv[2]).write_text('\n'.join(parts))
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$w" "$d/test_store_backup.c" -o "$w/test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w/test"
