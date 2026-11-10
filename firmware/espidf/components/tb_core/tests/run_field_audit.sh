#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d /tmp/tb-field-audit.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 - "$test_dir/../capture.c" "$work/audit_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text()
end=s.index('} request_event_t;')+len('} request_event_t;');start=s.rfind('typedef struct {',0,end)
assert start>=0;out=s[start:end]+'\n'
m=re.search(r'^static void request_store_event\([^;]*?\)\s*\{',s,re.M);assert m
end=s.index('{',m.start())+1;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
Path(sys.argv[2]).write_text(out+s[m.start():end])
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I"$work" -I"$test_dir/.." -I"$test_dir/../../../../j1708_17pid/src" -I"$IDF_PATH/components/json/cJSON" \
 "$test_dir/test_field_audit.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work/test"
ASAN_OPTIONS=detect_leaks=1 "$work/test"
