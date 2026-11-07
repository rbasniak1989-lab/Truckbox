#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d /tmp/truckbox-link-wifi.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../tb_link.c" "$work_dir/uplink_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text()
names=['capture_http_event','result_json','uplink_error','safe_sequence','uplink_netif','https_post_json','tb_link_uplink']
chunks=[re.search(r'typedef struct\s*\{\s*char data\[4096\];[^}]*}\s*http_capture_t;',s).group()]
for name in names:
    m=re.search(r'^(?:static\s+)?(?:char\s*\*|bool|esp_err_t|esp_netif_t\s*\*)\s*'+name+r'\([^;]*?\)\s*\{',s,re.M)
    assert m,name
    p=s.index('{',m.start())+1;depth=1
    while depth:
        depth+=(s[p]=='{')-(s[p]=='}');p+=1
    chunks.append(s[m.start():p])
Path(sys.argv[2]).write_text('\n\n'.join(chunks))
PY
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$work_dir" -I"$IDF_PATH/components/json/cJSON" \
  "$test_dir/test_wifi_uplink.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
