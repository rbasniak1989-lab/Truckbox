#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp -d /tmp/tb-store-worker.XXXXXX)
trap 'rm -rf "$w"' EXIT HUP INT TERM
python3 - "$d/../store.c" "$w/worker_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();a=s.index('static void storage_task(');pos=s.index('{',a)+1;end=pos;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
Path(sys.argv[2]).write_text(s[a:end])
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g -I"$w" -I"$d/.." "$d/test_store_worker.c" -o "$w/test"
ASAN_OPTIONS=detect_leaks=0 "$w/test"
