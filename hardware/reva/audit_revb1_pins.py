#!/usr/bin/env python3
"""Release BLOCKER for TruckBox Rev.B.1 footprint pin geometry.

Run after producing the FINAL KiCad PCB, not on generator code alone:
    python3 hardware/reva/audit_revb1_pins.py hardware/reva/TruckBox_RevA.kicad_pcb

This is a narrow, independent physical pin / net check. It is NOT a substitute
for schematic ERC, KiCad DRC, BOM/CPL review, datasheets, or testing a PCB.
"""
from pathlib import Path
import re
import sys

BOARD = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).with_name("TruckBox_RevA.kicad_pcb")
EXPECTED = {
    "Q1": {"1": "LINK_PWR_N", "2": "3V3_MAIN", "3": "3V3_LINK"},
    "Q2": {"1": "SD_PWR_N", "2": "3V3_MAIN", "3": "3V3_SD"},
    "Q3": {"1": "ACC_BASE", "2": "GND", "3": "ACC_N"},
    "Q4": {"1": "ACC_N", "2": "3V3_MAIN", "3": "WDT_3V3"},
    "U8": {"1": "WDT_3V3", "2": "GND", "3": "WDT_REXT",
           "4": "WDT_DONE", "5": "WDT_WAKE_TP", "6": "CORE_EN"},
}

def block_at(src, start):
    depth, quote, escape = 0, False, False
    for i in range(start, len(src)):
        c = src[i]
        if quote:
            if escape:
                escape = False
            elif c == "\\":
                escape = True
            elif c == '"':
                quote = False
        elif c == '"':
            quote = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return src[start:i+1]
    raise ValueError(f"unclosed KiCad block at {start}")

def fp_by_ref(src, ref):
    for m in re.finditer(r"\(footprint\s+", src):
        blk = block_at(src, m.start())
        if (re.search(r'\(fp_text\s+reference\s+"?' + re.escape(ref) + r'"?(?=\s|\))', blk)
                or re.search(r'\(property\s+"Reference"\s+"' + re.escape(ref) + r'"', blk)):
            return blk
    raise ValueError(f"component {ref} not found")

def pads(block):
    result = {}
    for m in re.finditer(r'\(pad\s+"?([\w.]+)"?\s+', block):
        pad = block_at(block, m.start())
        nr = m.group(1)
        a = re.search(r'\(at\s+([-\d.]+)\s+([-\d.]+)', pad)
        n = re.search(r'\(net\s+\d+\s+"([^"]+)"\)', pad)
        if a:
            result.setdefault(nr, (float(a.group(1)), float(a.group(2)),
                                   n.group(1) if n else None))
    return result

def audit(src):
    failures = []
    for ref, nets in EXPECTED.items():
        try:
            p = pads(fp_by_ref(src, ref))
            for num, net in nets.items():
                if num not in p:
                    failures.append(f"{ref}: physical pad {num} missing")
                elif p[num][2] != net:
                    failures.append(f"{ref}.{num}: net {p[num][2]!r}, expected {net!r}")
            if not set(nets).issubset(p):
                continue
            if ref.startswith("Q"):
                # 0-degree local SOT-23 convention used by this board:
                # pin 1 UPPER-LEFT, pin 2 LOWER-LEFT, pin 3 RIGHT.
                if not (p["1"][1] < p["2"][1]
                        and abs(p["1"][0] - p["2"][0]) < 0.1
                        and p["3"][0] > p["1"][0]):
                    failures.append(f"{ref}: SOT-23 pad geometry mirrored 1<->2")
            else:
                # TPL5010 / top-view SOT-23-6:
                # left from top: 1,2,3; right from top: 6,5,4.
                if not (p["1"][1] < p["2"][1] < p["3"][1]
                        and p["6"][1] < p["5"][1] < p["4"][1]):
                    failures.append("U8: SOT-23-6 geometry mirrored 1<->3 / 4<->6")
        except ValueError as exc:
            failures.append(str(exc))
    try:
        sim = pads(fp_by_ref(src, "J5"))
        if "C5" not in sim:
            failures.append("J5: SIM contact C5 is missing")
        elif sim["C5"][2] != "GND":
            failures.append(f"J5.C5 must be GND, found {sim['C5'][2]!r}")
    except ValueError as exc:
        failures.append(str(exc))
    return failures

def main():
    if not BOARD.is_file():
        sys.exit(f"BLOCKED: PCB not found: {BOARD}")
    bad = audit(BOARD.read_text(encoding="utf-8"))
    for b in bad:
        print("BLOCKED:", b)
    if bad:
        sys.exit(f"RELEASE BLOCKED: {len(bad)} physical pin/net errors")
    print("Targeted Q1-Q4/U8/J5 geometry and nets passed.")
    print("WARNING: does not certify other footprints, traces, or assembled board.")

if __name__ == "__main__":
    main()
