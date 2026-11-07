#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
repo_root=$(cd ../../.. && pwd)
module="$repo_root/j1708_17pid/src"
test_bin=$(mktemp /tmp/tbx-j1587-bridge.XXXXXX)
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic ${SANITIZERS:-} \
  -I. -I"$module" tbx_j1587_bridge.c \
  "$module/tbx_j1587_decoder.c" "$module/tbx_j1587_passive.c" \
  "$module/tbx_j1587_passive_full.c" "$module/tbx_j1708_requests.c" \
  tests/test_j1587_bridge.c -lm -o "$test_bin"
"$test_bin"
