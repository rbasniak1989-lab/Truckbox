#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d /tmp/truckbox-peer.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../network.c" "$work_dir/peer_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();chunks=[]
for name in ['tb_network_note_link_address','tb_network_link_host','tb_network_note_link_socket']:
 m=re.search(r'^void '+name+r'\([^;]*?\)\s*\{',s,re.M);assert m
 p=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[p]=='{')-(s[p]=='}');p+=1
 chunks.append(s[m.start():p])
Path(sys.argv[2]).write_text('\n\n'.join(chunks))
PY
cc -std=c11 -D_DEFAULT_SOURCE -DCONFIG_LWIP_IPV6=1 -Wall -Wextra -Werror -Wpedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$work_dir" "$test_dir/test_peer_address.c" -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
