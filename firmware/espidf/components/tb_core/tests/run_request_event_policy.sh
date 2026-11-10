#!/bin/sh
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tmp=$(mktemp -d /tmp/tb-req-policy.XXXXXX)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
python3 - "$test_dir/../capture.c" "$tmp/policy.h" <<'PY'
import re,sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
m=re.search(r'^static bool request_event_should_persist\(const char \*name, bool work_mode\)\s*\{',s,re.M)
assert m is not None,"missing real production policy"
start=s.index('{',m.start())
depth=1
end=start+1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}')
    end+=1
Path(sys.argv[2]).write_text(s[m.start():end]+'\n')
assert 'BOOT_DIAGNOSTIC' in s and 'record_boot_diagnostic();' in s
assert 'cJSON_AddNumberToObject(context,"reset_reason"' in s
assert 'cJSON_AddNumberToObject(metrics,"heap_minimum"' in s
PY
cat >"$tmp/test.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "policy.h"
int main(void){
    const char *routine[]={"TX_ECHO","RESPONSE_CANDIDATE"};
    const char *critical[]={"REQUEST","RESPONSE_VALUE","PHYSICAL_EVIDENCE","TIMEOUT","LINK_ERROR","TX_ERROR","AUTO_DISARMED",
                            "STOPPED","WORK_ENABLED","ARMED","CONFIGURED"};
    for(unsigned i=0;i<sizeof(routine)/sizeof(*routine);i++){
        assert(request_event_should_persist(routine[i],false));
        assert(!request_event_should_persist(routine[i],true));
    }
    for(unsigned i=0;i<sizeof(critical)/sizeof(*critical);i++){
        assert(request_event_should_persist(critical[i],false));
        assert(request_event_should_persist(critical[i],true));
    }
    assert(request_event_should_persist(NULL,true));
    return 0;
}
C
cc -std=c11 -Wall -Wextra -Werror -Wpedantic -I"$tmp" "$tmp/test.c" -o "$tmp/test"
"$tmp/test"
echo 'J1708 event persistence policy: PASSED'
