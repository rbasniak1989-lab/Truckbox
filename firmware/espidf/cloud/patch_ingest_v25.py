#!/usr/bin/env python3
"""Prepare ingest v25 from the reviewed v24 snapshot; never deploy or expose credentials."""
from __future__ import annotations

import argparse
import difflib
import hashlib
from pathlib import Path

from patch_ingest_v24 import BASELINE_SHA256, patched_text as patch_v020


def patched_text(source: str) -> str:
    result = patch_v020(source)
    replacements = (
        ("const num=(v:any)=>typeof v==='number'&&Number.isFinite(v)?v:(Number.isFinite(Number(v))?Number(v):null);",
         """const num=(v:any)=>{
  if((typeof v!=='number'&&typeof v!=='string')||(typeof v==='string'&&!v.trim()))return null;
  const n=Number(v);return Number.isFinite(n)?n:null;
};"""),
        ("const int=(v:any)=>{const n=Number(v);return Number.isSafeInteger(n)?n:null};",
         "const int=(v:any)=>{const n=num(v);return n!=null&&Number.isSafeInteger(n)?n:null};"),
        ("    const validObj=(s:any)=>s!==null&&typeof s==='object'&&!Array.isArray(s);",
         """    const validObj=(s:any)=>s!==null&&typeof s==='object'&&!Array.isArray(s);
    // Reject the entire batch before writing: a cumulative ACK must not skip malformed records.
    for(const key of ['samples','operational','health','events']){
      if(body[key]!=null&&!Array.isArray(body[key]))return J({error:'invalid_batch_array'},400);
      for(const s of body[key]??[]){
        const seq=validObj(s)?int(s.seq):null;
        if(seq==null||seq<0)return J({error:'invalid_record_sequence'},400);
      }
    }"""),
        ("""    if(emergencyRawOnly){
      const incomingSeqs=[""",
         """    // Keep diagnostics in the existing durable table, with their original kind and raw payload.
    // Null operational columns also prevent SQL fuel/calibration triggers from using diagnostics.
    const diagnosticMetadata=new Set(['truck_id','device_id','trip_id','sample_kind','seq','ts','uptime_ms','raw']);
    for(const row of rows){
      const kind=String(row.raw?.sample_kind??'OPERATIONAL').toUpperCase();
      if(kind!=='OPERATIONAL'){
        row.sample_kind=kind;
        for(const key of Object.keys(row))if(!diagnosticMetadata.has(key))(row as any)[key]=null;
      }
    }

    if(emergencyRawOnly){
      const incomingSeqs=["""),
        ("context:{...(typeof h.context==='object'&&h.context?h.context:{}),trip_weight_t:tripOk?activeTrip?.weight_t??null:null}",
         "context:{...(typeof h.context==='object'&&h.context?h.context:{}),trip_weight_t:tripOk?activeTrip?.weight_t??null:null,firmware_record:h}"),
        ("      ingest_version:9", "      ingest_version:25"),
    )
    for old, new in replacements:
        if result.count(old) != 1:
            raise ValueError("Reviewed v24 fragment changed; no v25 source generated")
        result = result.replace(old, new, 1)
    return result


def prepare(source: Path, output: Path, diff: Path | None = None) -> None:
    paths = [source.resolve(), output.resolve()] + ([diff.resolve()] if diff else [])
    if len(paths) != len(set(paths)):
        raise ValueError("Source, private output and public diff must use distinct files")
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != BASELINE_SHA256:
        raise ValueError("Source differs from the reviewed v24 snapshot; review before patching")
    original = data.decode("utf-8")
    updated = patched_text(original)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(updated, encoding="utf-8")
    output.chmod(0o600)
    if diff:
        diff.parent.mkdir(parents=True, exist_ok=True)
        diff.write_text("".join(difflib.unified_diff(
            original.splitlines(keepends=True), updated.splitlines(keepends=True),
            fromfile="truckbox-ingest-v24/index.ts", tofile="truckbox-ingest-v25/index.ts", n=3,
        )), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--diff", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.output, args.diff)
    print("Private v25 source prepared; no cloud deployment performed.")
