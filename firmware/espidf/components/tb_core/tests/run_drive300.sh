#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d /tmp/tb-drive300.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
export CAPTURE_TEST_FILE="$d/test_drive300.c"
# The existing runner builds exact capture functions and uses a fixture main.
# Forward its binary argument through the runner's ASAN invocation.
python3 - "$d/run_capture_integration.sh" "$work/run.sh" "$d" "$work/drive.jsonl" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text().replace('test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)','test_dir="'+sys.argv[3]+'"')
s=s.replace('ASAN_OPTIONS=detect_leaks=0 "$work_dir/test"','ASAN_OPTIONS=detect_leaks=0 "$work_dir/test" "'+sys.argv[4]+'"')
Path(sys.argv[2]).write_text(s)
PY
sh "$work/run.sh"
cc -O1 -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
 -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I"$d/.." -I"$IDF_PATH/components/json/cJSON" \
 "$d/test_drive_storage.c" "$d/../store_codec.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work/store"
ASAN_OPTIONS=detect_leaks=0 "$work/store" "$work/drive.jsonl"
