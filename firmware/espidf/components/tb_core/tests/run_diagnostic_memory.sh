#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${IDF_PATH:?Activate ESP-IDF}"
work_dir=$(mktemp -d /tmp/tb-diagnostic-memory.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../capture.c" "$test_dir/../core.c" "$work_dir" <<'PY'
from pathlib import Path
import re,sys

def function(source,name):
    m=re.search(r'^(?:static\s+)?(?:void|bool|size_t|cJSON\s*\*)\s*'+re.escape(name)+r'\s*\([^;]*?\)\s*\{',source,re.M)
    assert m,name
    start=source.index('{',m.start());end=start+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
source=Path(sys.argv[1]).read_text();out=Path(sys.argv[3])
start=source.index('static struct {');end=source.index('} s;',start)+len('} s;')
names=['add_signal_field','request_stats_locked','capture_json_mode','tb_capture_add_json','tb_capture_add_json_compact','tb_capture_request_stats_json']
(out/'capture_diagnostic.h').write_text(source[start:end]+'\n'+'\n'.join(function(source,n) for n in names))
source=Path(sys.argv[2]).read_text()
m=re.search(r'typedef struct\s*\{\s*char \*payload;.*?\}\s*bridge_batch_t;',source,re.S);assert m
(out/'bridge_prepare.h').write_text(m.group(0)+'\n'+function(source,'numeric_sequence')+'\n'+function(source,'prepare_bridge_batch'))
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$test_dir" -I"$test_dir/.." -I"$work_dir" -I"$test_dir/../../tb_base/include" \
  -I"$test_dir/../../../../j1708_17pid/src" -I"$IDF_PATH/components/json/cJSON" \
  "$test_dir/test_diagnostic_memory.c" "$test_dir/../../tb_base/tb_json.c" \
  "$test_dir/../tbx_j1708_active.c" "$test_dir/../tbx_j1708_field.c" "$test_dir/../../../../j1708_17pid/src/tbx_j1708_requests.c" \
  "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$work_dir/test"
