#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d /tmp/truckbox-wifi-sharing.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../network.c" "$work_dir/router_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();chunks=[]
for name in ['subnets_overlap','router_state','router_disable','router_dns','router_refresh','network_event','tb_network_health_fields']:
    m=re.search(r'^(?:static\s+)?(?:bool|void|esp_err_t|size_t)\s+'+name+r'\([^;]*?\)\s*\{',s,re.M)
    assert m,name
    p=s.index('{',m.start())+1;depth=1
    while depth:
        depth+=(s[p]=='{')-(s[p]=='}');p+=1
    chunks.append(s[m.start():p])
Path(sys.argv[2]).write_text('\n\n'.join(chunks))
PY
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$work_dir" "$test_dir/test_wifi_sharing.c" -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
