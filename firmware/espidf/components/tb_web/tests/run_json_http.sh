#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d /tmp/tb-json-http.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../tb_web.c" "$work_dir/http_json.h" <<'PY'
from pathlib import Path
import re,sys
source=Path(sys.argv[1]).read_text()
def function(name):
    m=re.search(r'^(?:static\s+)?(?:bool|void|esp_err_t)\s+'+name+r'\s*\([^;]*?\)\s*\{',source,re.M);assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return m.start(),source[m.start():end]
chunks=[function(n) for n in ['response_headers','health_reply','health_handler','json_http_sink','tb_http_send_json','state_handler']]
m=re.search(r'typedef struct\{httpd_req_t \*request;char bytes\[1024\];.*?\}json_http_t;',source);assert m
chunks.append((m.start(),m.group()))
Path(sys.argv[2]).write_text('\n'.join(t for _,t in sorted(chunks)))
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$test_dir/../../tb_base/include" -I"$work_dir" -I"$IDF_PATH/components/json/cJSON" \
  "$test_dir/test_json_http.c" "$test_dir/../../tb_base/tb_json.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=1 "$work_dir/test"
