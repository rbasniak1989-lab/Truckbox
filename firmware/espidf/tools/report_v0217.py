#!/usr/bin/env python3
"""Report real ELF RAM/type sizes; requires pyelftools from the IDF environment."""
import argparse
import json
from pathlib import Path
from elftools.elf.elffile import ELFFile

RAM = {".iram0.text", ".iram0.data", ".iram0.bss", ".dram0.data", ".dram0.bss", ".noinit"}
BASE_TYPES = {"tbx_j1708_request_scheduler_t", "tbx_j1708_active_t", "tbx_j1708_auto_t", "request_event_t"}
NEW_TYPES = BASE_TYPES | {"tbx_volvo_operating_t", "tbx_ecu_counters_t", "tbx_dtc_history_t",
    "tbx_dtc_sections_t", "diagnostic_checkpoint_t", "counter_checkpoint_t"}


def measure(path, expected):
    with path.open("rb") as file:
        elf = ELFFile(file)
        sections = {s.name: s.header.sh_size for s in elf.iter_sections() if s.name in RAM}
        sizes = {}
        for cu in elf.get_dwarf_info().iter_CUs():
            for die in cu.iter_DIEs():
                name = die.attributes.get("DW_AT_name")
                if not name or not isinstance(name.value, bytes):
                    continue
                key = name.value.decode(errors="replace")
                if key not in expected or key in sizes:
                    continue
                resolved = die
                while "DW_AT_type" in resolved.attributes and "DW_AT_byte_size" not in resolved.attributes:
                    resolved = resolved.get_DIE_from_attribute("DW_AT_type")
                if "DW_AT_byte_size" in resolved.attributes:
                    sizes[key] = resolved.attributes["DW_AT_byte_size"].value
            if expected <= sizes.keys():
                break
        return {"sections": sections, "ram_static_bytes": sum(sections.values()), "types": sizes}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    baseline = measure(args.baseline, BASE_TYPES)
    candidate = measure(args.candidate, NEW_TYPES)
    print(json.dumps({"baseline": baseline, "candidate": candidate,
        "static_added_bytes": candidate["ram_static_bytes"] - baseline["ram_static_bytes"],
        "physical_free_heap": "NOT_MEASURED", "partition_nvs_bytes": 0x6000,
        "new_nvs_live_blob_bytes": 2 * (candidate["types"]["diagnostic_checkpoint_t"] + candidate["types"]["counter_checkpoint_t"])}, indent=2))
