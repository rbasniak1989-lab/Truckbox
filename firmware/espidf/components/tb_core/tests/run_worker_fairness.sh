#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d /tmp/truckbox-worker-fairness.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
ref=${1:-}
for role in decode store; do
    python3 - "$test_dir/.." "$work_dir/worker_under_test.h" "$role" "$ref" <<'PY'
from pathlib import Path
import re,subprocess,sys
core=Path(sys.argv[1]);role=sys.argv[3];ref=sys.argv[4]
file,name=('telemetry.c','worker_task') if role=='decode' else ('store.c','storage_task')
if ref:
    root=core.parents[3]
    source=subprocess.check_output(['git','show',ref+':firmware/espidf/components/tb_core/'+file],cwd=root,text=True)
else: source=(core/file).read_text()
m=re.search(r'^static void '+name+r'\([^)]*\)\s*\{',source,re.M)
assert m,name
pos=source.index('{',m.start())+1;depth=1
while depth:
    depth+=(source[pos]=='{')-(source[pos]=='}');pos+=1
Path(sys.argv[2]).write_text(source[m.start():pos]+'\n')
PY
    mode=""
    if [ "$role" = store ]; then mode="-DTEST_STORE"; fi
    cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
        -fsanitize=address,undefined -fno-omit-frame-pointer -g $mode \
        -I"$work_dir" -I"$test_dir/.." "$test_dir/test_worker_fairness.c" -o "$work_dir/test"
    if [ -n "$ref" ]; then ASAN_OPTIONS=detect_leaks=0 "$work_dir/test" --expect-starvation
    else ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
    fi
done
