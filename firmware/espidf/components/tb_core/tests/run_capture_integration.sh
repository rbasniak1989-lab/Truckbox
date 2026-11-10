#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work_dir=$(mktemp -d /tmp/truckbox-capture-test.XXXXXX)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
python3 - "$test_dir/../capture.c" "$work_dir/capture_under_test.h" <<'PY'
from pathlib import Path
import re, sys
source = Path(sys.argv[1]).read_text()
def function(name):
    match = re.search(r'^(?:static\s+)?(?:void|bool|uint32_t|esp_err_t)\s+' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source, re.M)
    assert match, f'missing production function: {name}'
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return match.start(), source[match.start():end]
state_start = source.index('static struct {')
state_end = source.index('} s;', state_start) + len('} s;')
typedefs = re.findall(r'typedef struct\s*\{[^}]*\}\s*trip_blob(?:_v1)?_t;', source)
assert len(typedefs) == 2
names = ['persist_trip', 'mirror_trip_locked', 'pgn_of', 'update_signals', 'update_j1587_signal_locked', 'tb_capture_reset_trip']
chunks = [source[state_start:state_end], *typedefs]
# Host runner tests acquisition and signal arbitration; flash JSON serialization is
# exercised by the ESP-IDF build and the separate storage tests.
chunks += ['static void tb_j1708_store_matched_response(const tbx_j1587_observation_t *v, uint64_t now) { (void)v; (void)now; }']
chunks += [text for _, text in sorted(function(name) for name in names)]
_, startup = function('tb_capture_start')
body = startup[startup.index('{') + 1:]
cut = re.search(r'^.*tb_telemetry_start\(', body, re.M)
assert cut, 'restore extraction must stop before telemetry/driver initialization'
restore = body[:cut.start()]
restore = re.sub(r'    const tbx_j1708_active_ops_t active_ops = .*?    s.request_events=xQueueCreate\(16,sizeof\(request_event_t\)\);', '    tbx_j1708_request_init(&s.requests);', restore, flags=re.S)
restore = restore.replace(' || !s.request_events', '')
chunks += ['static esp_err_t host_start_restore(void) {\n' + restore + '\nreturn ESP_OK;\n}']
Path(sys.argv[2]).write_text('static const char *TAG = "capture_host";\n' + '\n\n'.join(chunks) + '\n')
PY
cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$work_dir" -I"$test_dir" -I"$test_dir/.." \
    -I"$test_dir/../../../../j1708_17pid/src" \
    "$test_dir/test_capture_integration.c" "$test_dir/../tbx_trip.c" "$test_dir/../tbx_fuel.c" "$test_dir/../tbx_j1708_active.c" \
    "$test_dir/../../../../j1708_17pid/src/tbx_j1708_requests.c" \
    -lm -o "$work_dir/test"
ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"
