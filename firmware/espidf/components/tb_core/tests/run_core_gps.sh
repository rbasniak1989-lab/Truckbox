#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${IDF_PATH:?Activate ESP-IDF or set IDF_PATH for its bundled cJSON}"
work_dir=$(mktemp -d /tmp/truckbox-core-gps.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../core.c" "$work_dir/core_gps_under_test.h" <<'PY'
from pathlib import Path
import re, sys
source=Path(sys.argv[1]).read_text()
def function(name):
    match=re.search(r'^(?:static\s+)?(?:void|bool)\s+'+re.escape(name)+r'\s*\([^;]*?\)\s*\{',source,re.M)
    assert match, name
    pos=source.index('{',match.start())+1
    depth=1
    while depth:
        depth+=(source[pos]=='{')-(source[pos]=='}')
        pos+=1
    return match.start(),source[match.start():pos]
start=source.index('static struct {')
end=source.index('} s;',start)+len('} s;')
fix=re.search(r'typedef struct\s*\{[^}]*\}\s*gps_fix_t;',source)
assert fix
names=['json_optional_number','tb_core_append_gps_at','optional_json_number','gps_from_json','accept_gps']
chunks=[fix.group(),source[start:end]]+[text for _,text in sorted(function(name) for name in names)]
Path(sys.argv[2]).write_text('\n\n'.join(chunks)+'\n')
PY
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$work_dir" -I"$test_dir" -I"$IDF_PATH/components/json/cJSON" \
    "$test_dir/test_core_gps.c" "$IDF_PATH/components/json/cJSON/cJSON.c" \
    -lm -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
