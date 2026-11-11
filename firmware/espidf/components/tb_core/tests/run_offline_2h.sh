#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
core=$(CDPATH= cd -- "$test_dir/.." && pwd)
module=$(CDPATH= cd -- "$core/../../../j1708_17pid/src" && pwd)
work=$(mktemp -d /tmp/tbx-offline2h.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 - "$core/capture.c" "$work/offline_capture_under_test.h" <<'PY'
from pathlib import Path
import re,sys
s=Path(sys.argv[1]).read_text();chunks=[]
end=s.index('} request_event_t;')+len('} request_event_t;');start=s.rfind('typedef struct {',0,end);assert start>=0
chunks.append(s[start:end])
for name in ['add_sample_identity','queue_sample','guard_values','guard_record','request_store_guard','request_store_event','tb_j1708_store_matched_response','request_metric_snapshot','volvo_operating_snapshot']:
 m=re.search(r'^static (?:void|bool) '+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 chunks.append(re.sub(r'\bs\.', 'capture_fixture.',s[m.start():end]))
# Exercise OPERATIONAL and full HEALTH builders from the actual task source.
start=s.index('static void snapshot_task(')
a=s.index('if (tb_operational_due(',start);begin=s.index('{',a)+1;end=begin;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
chunks.append('static void offline_operational(uint64_t now){'+re.sub(r'\bs\.', 'capture_fixture.',s[begin:end-1])+'}')
a=s.index('if((seconds+1U)%10U==0U && tb_config_j1708_auto_enabled())',start);begin=s.index('{',a)+1;end=begin;depth=1
while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
chunks.append('static void offline_transmission(uint64_t now){'+re.sub(r'\bs\.', 'capture_fixture.',s[begin:end-1])+'}')
a=s.index('        cJSON *health =',start);b=s.index('        /* Append optional sensors',a)
health=s[a:b].replace('if (!health) continue;','if (!health) return NULL;')
health=re.sub(r'\bs\.', 'capture_fixture.',health)
chunks.append('static cJSON *offline_health(uint64_t now){static uint64_t window_start;static uint32_t old_can,old_j1708,old_checksum,old_overruns,old_request_sent,old_request_answered,old_request_timeouts;'+health+'window_start=now;old_can=capture_fixture.can_frames;old_j1708=capture_fixture.j1708_frames;old_checksum=capture_fixture.j1708_checksum_errors;old_overruns=capture_fixture.uart_overruns;return health;}')
Path(sys.argv[2]).write_text('\n\n'.join(chunks))
PY
for source in diagnostics ecu_counters; do
 cc -O1 -std=c11 -D_POSIX_C_SOURCE=200809L -DTB_DIAGNOSTIC_HOST_TEST -DTB_COUNTER_HOST_TEST \
  -Desp_timer_get_time=offline_clock -Dtb_boot_id=offline_boot -Dtb_store_submit=offline_submit \
  -Dgettimeofday=offline_gettimeofday -DxSemaphoreCreateMutex=offline_mutex -DxSemaphoreTake=offline_take -DxSemaphoreGive=offline_give \
  -Wall -Wextra -Werror -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -I"$core" -I"$module" -I"$IDF_PATH/components/json/cJSON" -c "$core/$source.c" -o "$work/$source.o"
done
cc -O1 -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
 -fsanitize=address,undefined -fno-omit-frame-pointer -g \
 -I"$work" -I"$core" -I"$module" -I"$IDF_PATH/components/json/cJSON" \
 "$core/tbx_transmission.c" "$core/tbx_volvo_operating.c" "$core/tbx_ecu_counters.c" "$module/tbx_j1587_diagnostics.c" "$work/diagnostics.o" "$work/ecu_counters.o" "$core/tbx_j1708_auto.c" "$test_dir/offline_export.c" "$core/protocols.c" "$core/tbx_j1587_bridge.c" \
 "$module/tbx_j1587_decoder.c" "$module/tbx_j1587_passive.c" "$module/tbx_j1587_passive_full.c" \
 "$module/tbx_j1708_requests.c" "$core/tbx_j1708_rx_guard.c" "$core/tbx_j1708_active.c" "$core/tbx_j1708_field.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work/export"
cc -O1 -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wpedantic \
 -fsanitize=address,undefined -fno-omit-frame-pointer -g -I"$core" -I"$IDF_PATH/components/json/cJSON" \
 "$test_dir/offline_storage.c" "$core/store_codec.c" "$IDF_PATH/components/json/cJSON/cJSON.c" -lm -o "$work/store"
for clock in 0 1; do
 TB_OFFLINE_CLOCK="$clock" ASAN_OPTIONS=detect_leaks=0 "$work/export" "$work/offline.jsonl"
 ASAN_OPTIONS=detect_leaks=0 "$work/store" "$work/offline.jsonl"
done
