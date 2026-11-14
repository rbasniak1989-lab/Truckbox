#!/bin/sh
set -eu
d=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
w=$(mktemp -d /tmp/tb-fatfs-rebind.XXXXXX)
trap 'rm -rf "$w"' EXIT HUP INT TERM
: "${IDF_PATH:?Activate ESP-IDF 5.5.2}"
python3 - "$d/../store.c" "$w/rebind_under_test.h" "$IDF_PATH/components/fatfs/vfs/vfs_fat.c" <<'PY'
from pathlib import Path
import sys,re
s=Path(sys.argv[1]).read_text();sdk=Path(sys.argv[3]).read_text()
maintenance=Path(sys.argv[1]).with_name('store_maintenance.h').read_text()
def extract(source,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',source,re.M);assert m,name
 start=m.start();end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
trace=re.search(r'static struct \{ bool active,called;TaskHandle_t owner;int result,flags; \} open_trace;',s).group()
parts=[extract(s,'rebind_flash_volume'),trace,'FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);']
parts += [extract(s,n) for n in ['__wrap_f_open','open_trace_begin','open_trace_end','inspect_open_failure','recovered_file_readonly']]
parts += [extract(sdk,n) for n in ['fat_mode_conv','fresult_to_errno','get_stat_mode']]
parts += [extract(maintenance,n) for n in ['maintenance_partition','maintenance_reformat_volume']]
Path(sys.argv[2]).write_text('\n'.join(parts))
PY
cc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Wl,--wrap=f_open \
    -I"$w" -I"$d/fatfs_stubs" -I"$IDF_PATH/components/fatfs/src" \
    "$d/test_fatfs_rebind.c" "$IDF_PATH/components/fatfs/src/ff.c" \
    "$IDF_PATH/components/fatfs/src/ffunicode.c" -o "$w/test"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$w/test"
