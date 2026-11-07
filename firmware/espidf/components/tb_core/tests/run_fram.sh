#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_binary=$(mktemp /tmp/truckbox-fram.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM

cc -std=c11 -Wall -Wextra -Werror -Wpedantic \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -fno-omit-frame-pointer -g \
    "$test_dir/test_fram.c" -o "$test_binary"

# LSan does not run under the managed executor's ptrace. The codec allocates no
# memory; AddressSanitizer and UndefinedBehaviorSanitizer remain enabled.
ASAN_OPTIONS=detect_leaks=0 "$test_binary"

# This oracle does not call the production CRC or serialization implementation.
python3 - <<'PY'
import binascii
import struct

slot = bytearray(b"\xff" * 64)
slot[:4] = b"TBFR"
slot[4:8] = bytes((1, 32, 0, 0))
struct.pack_into("<IIQQ", slot, 8, 42, 0, 1000, 999)
assert binascii.crc32(slot[:60]) == 0x0BDCF881
print("Independent Python CRC/serialization golden vector passed")
PY
