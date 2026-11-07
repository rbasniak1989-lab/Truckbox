#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${IDF_PATH:?Activate ESP-IDF or set IDF_PATH for its bundled cJSON}"
work_dir=$(mktemp -d /tmp/truckbox-bridge-memory.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../core.c" "$work_dir/bridge_under_test.h" <<'PY'
from pathlib import Path
import re, sys
source = Path(sys.argv[1]).read_text()
def function(name):
    match = re.search(r'^static\s+bool\s+' + name + r'\s*\([^;]*?\)\s*\{', source, re.M)
    assert match, name
    pos = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]
limits = re.findall(r'^#define (?:MAX_SAFE_SEQUENCE|BRIDGE_BATCH_LIMIT|BRIDGE_BATCH_BYTES) .+$', source, re.M)
structure = re.search(r'typedef struct\s*\{[^}]*\}\s*bridge_batch_t;', source)
assert len(limits) == 3 and structure
Path(sys.argv[2]).write_text('\n'.join(limits) + '\n' + structure.group() + '\n' +
    '\n'.join(function(n) for n in ('numeric_sequence', 'prepare_bridge_batch', 'batch_contains')))
PY
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$work_dir" -I"$IDF_PATH/components/json/cJSON" \
    "$test_dir/test_bridge_memory.c" "$IDF_PATH/components/json/cJSON/cJSON.c" \
    -lm -o "$work_dir/test"
# The managed runner uses ptrace, which prevents LeakSanitizer startup.
# The tracked allocator independently requires zero live bytes on every path.
ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
