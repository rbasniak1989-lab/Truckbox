#!/bin/sh
set -eu
module=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d /tmp/tbx-j1708-tests.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
for name in tbx_j1708_requests tbx_j1587_decoder tbx_j1587_passive tbx_j1587_passive_full tbx_j1587_diagnostics; do
    extra=""
    if [ "$name" = tbx_j1587_passive_full ]; then extra="$module/src/tbx_j1587_passive.c"; fi
    cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined \
        -fno-omit-frame-pointer -g "$module/src/$name.c" $extra \
        "$module/tests/test_$name.c" -lm -o "$work/test"
    ASAN_OPTIONS=detect_leaks=0 "$work/test"
done
