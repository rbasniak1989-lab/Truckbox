#!/usr/bin/env python3
"""Compare ESP32-C6 physical RAM sections; GNU total bss includes flash dummies."""
import argparse,json,subprocess
from pathlib import Path
RAM=(".iram0.text",".iram0.data",".iram0.bss",".dram0.data",".dram0.bss",".noinit")
def sections(path):
    output=subprocess.check_output(["riscv32-esp-elf-size","-A",str(path)],text=True)
    fields={}
    for line in output.splitlines():
        parts=line.split()
        if len(parts)>1 and parts[0] in RAM:fields[parts[0]]=int(parts[1])
    fields["ram_static_bytes"]=sum(fields.values())
    return fields
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("baseline",type=Path);p.add_argument("candidate",type=Path)
    a=p.parse_args();base,new=sections(a.baseline),sections(a.candidate)
    print(json.dumps({"baseline":base,"candidate":new,"static_saved_bytes":base["ram_static_bytes"]-new["ram_static_bytes"],
        "dynamic_known_saved_bytes":64*104+128*20+5120-2048-2048,
        "dynamic_basis":"RX queue64*104, TWAI queue128*20, CAN task5120 removed; storage and bridge stacks each+2048. Excludes allocator/TCB overhead.",
        "physical_free_heap":"NOT_MEASURED"},indent=2))
