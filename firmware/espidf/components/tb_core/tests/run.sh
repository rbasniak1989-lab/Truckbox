#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp /tmp/truckbox-protocols.XXXXXX)
test_json=$(mktemp /tmp/truckbox-json.XXXXXX)
trap 'rm -f "$test_binary" "$test_json"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    -I"$test_dir/.." "$test_dir/../protocols.c" \
    "$test_dir/test_protocols.c" -o "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary"
ASAN_OPTIONS=detect_leaks=0 "$test_binary" --json > "$test_json"
python3 - "$test_json" <<'PY'
import json
import sys

count = 0
with open(sys.argv[1], encoding="utf-8") as stream:
    for line in stream:
        record = json.loads(line)
        assert record["source"] in ("can", "j1708")
        assert isinstance(record["uptime_us"], int)
        assert record["uptime_us"] >= 0
        if record["source"] == "can":
            assert len(record["data"]) == (0 if record["rtr"] else 2 * record["dlc"])
        else:
            raw = bytes.fromhex(record["raw"])
            assert 2 <= len(raw) <= 21
            assert sum(raw) % 256 == 0
        count += 1
assert count == 4000
print(f"Validated {count} generated records with an independent JSON parser")
PY
