#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$(mktemp /tmp/tb-legacy-profile.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
cc -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=undefined \
  "$test_dir/test_legacy_profile.c" -o "$binary"
"$binary"
