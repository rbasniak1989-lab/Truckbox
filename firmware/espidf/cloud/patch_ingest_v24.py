#!/usr/bin/env python3
"""Prepare a LOCAL compatibility patch; never upload, deploy, or alter the input."""
from __future__ import annotations

import argparse
import difflib
import hashlib
from pathlib import Path

BASELINE_SHA256 = "716bbaa96646b5adafa4e62fd33852f4b3326153f95282adaf2387eec2268fec"
REPLACEMENTS = (
    ("""    const captureTs=(s:any)=>{
      if(typeof s.ts==='string'&&s.ts.length>=20)return s.ts;
      const su=num(s.uptime_ms),sbid=num(s.boot_id);
      if(deviceNowMs!=null&&deviceUptimeMs!=null&&su!=null&&currentBootId!=null&&sbid===currentBootId&&su<=deviceUptimeMs){
        return new Date(deviceNowMs-(deviceUptimeMs-su)).toISOString();
      }
      return new Date().toISOString();
    };""",
     """    const captureUptimeMs=(s:any)=>{
      const ms=s.uptime_ms==null?null:num(s.uptime_ms),us=s.uptime_us==null?null:num(s.uptime_us);
      return ms??(us==null?null:us/1000);
    };
    const captureTs=(s:any)=>{
      if(typeof s.ts==='string'&&s.ts.length>=20&&isoMs(s.ts)!=null)return s.ts;
      const epoch=s.time_valid===true&&typeof s.unix_ms==='number'?s.unix_ms:null;
      if(epoch!=null&&Number.isFinite(epoch)&&epoch>=1704067200000&&epoch<=8640000000000000){
        return new Date(epoch).toISOString();
      }
      const su=captureUptimeMs(s),sbid=num(s.boot_id);
      if(deviceNowMs!=null&&deviceNowMs>=1704067200000&&deviceUptimeMs!=null&&su!=null&&su>=0&&currentBootId!=null&&sbid===currentBootId&&su<=deviceUptimeMs){
        const reconstructed=deviceNowMs-(deviceUptimeMs-su);
        if(reconstructed>=1704067200000&&reconstructed<=8640000000000000)return new Date(reconstructed).toISOString();
      }
      // Receipt time only; original boot/monotonic/time_valid remain unchanged in raw.
      return new Date().toISOString();
    };"""),
    ("      uptime_ms:num(s.uptime_ms),", "      uptime_ms:captureUptimeMs(s),"),
    ("""    const belongsToActiveTrip=(r:any)=>{
      if(!activeTrip)return false;""",
     """    const isOperationalRow=(r:any)=>String(r.raw?.sample_kind??'OPERATIONAL').toUpperCase()==='OPERATIONAL';
    const belongsToActiveTrip=(r:any)=>{
      if(!activeTrip||!isOperationalRow(r))return false;"""),
    ("    const tripRows=activeTrip?rows.filter((r:any)=>r.trip_id===activeTrip.id):[];",
     "    const tripRows=activeTrip?rows.filter((r:any)=>isOperationalRow(r)&&r.trip_id===activeTrip.id):[];"),
    ("""      if(!emergencyRawOnly){
        // Trigger SPN183 trip consumption update by telemetry time, not shared sample sequence.""",
     """      if(!emergencyRawOnly&&rows.some(isOperationalRow)){
        // Trigger SPN183 trip consumption update by telemetry time, not shared sample sequence."""),
    ("        const candidate=rows.reduce((a:any,b:any)=>{",
     "        const candidate=rows.filter(isOperationalRow).reduce((a:any,b:any)=>{"),
)


def patched_text(source: str) -> str:
    result = source
    for old, new in REPLACEMENTS:
        if result.count(old) != 1:
            raise ValueError("Expected ingest-v24 code fragment is missing or duplicated; review the new backend source")
        result = result.replace(old, new, 1)
    return result


def prepare(source: Path, output: Path, diff: Path | None = None) -> None:
    if source.resolve() == output.resolve():
        raise ValueError("Output must differ from input; the deployed-source snapshot is preserved")
    if diff is not None and diff.resolve() in {source.resolve(), output.resolve()}:
        raise ValueError("Diff must differ from both the input and the private review source")
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != BASELINE_SHA256:
        raise ValueError("Source SHA256 differs from the reviewed ingest-v24 snapshot; no patch generated")
    original = data.decode("utf-8")
    updated = patched_text(original)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(updated, encoding="utf-8")
    if diff is not None:
        delta = "".join(difflib.unified_diff(original.splitlines(keepends=True), updated.splitlines(keepends=True),
                                           fromfile="truckbox-ingest-v24/index.ts", tofile="truckbox-ingest-v020-review/index.ts", n=3))
        diff.parent.mkdir(parents=True, exist_ok=True)
        diff.write_text(delta, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Privately downloaded ingest-v24 index.ts")
    parser.add_argument("--output", type=Path, required=True, help="Private LOCAL review copy, outside the public repository")
    parser.add_argument("--diff", type=Path, help="Minimal review diff (contains no authentication source)")
    args = parser.parse_args()
    prepare(args.source, args.output, args.diff)
    print("Local patch prepared. Nothing was deployed.")


if __name__ == "__main__":
    main()
