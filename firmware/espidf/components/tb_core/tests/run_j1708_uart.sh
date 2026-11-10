#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d /tmp/tb-j1708-uart.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 - "$test_dir/../capture.c" "$work/capture_uart_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text()
chunks=[]
for name in ['request_abort_tx','request_rx_edge','request_bus_idle','request_begin_tx']:
    m=re.search(r'^static\s+(?:void|bool|tbx_j1708_tx_result_t)\s+(?:IRAM_ATTR\s+)?'+name+r'\([^;]*?\)\s*\{',s,re.M)
    assert m,name
    end=s.index('{',m.start())+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    chunks.append(s[m.start():end])
Path(sys.argv[2]).write_text('\n\n'.join(chunks))
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g -I"$work" -I"$test_dir/.." \
  -I"$test_dir/../../../../j1708_17pid/src" \
  "$test_dir/test_j1708_uart.c" -o "$work/test"
ASAN_OPTIONS=detect_leaks=0 "$work/test"
