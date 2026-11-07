#!/usr/bin/env python3
"""Generate private ingest v26: integer SQL milliseconds, original microseconds in raw."""
from __future__ import annotations

import argparse
import difflib
import hashlib
from pathlib import Path

from patch_ingest_v24 import BASELINE_SHA256
from patch_ingest_v25 import patched_text as patch_v25


def patched_text(source: str) -> str:
    result = patch_v25(source)
    old = """    const captureUptimeMs=(s:any)=>{
      const ms=s.uptime_ms==null?null:num(s.uptime_ms),us=s.uptime_us==null?null:num(s.uptime_us);
      return ms??(us==null?null:us/1000);
    };"""
    new = """    const captureUptimeMs=(s:any)=>{
      const ms=num(s.uptime_ms),us=num(s.uptime_us);
      const value=ms??(us==null?null:us/1000);
      // SQL uptime_ms is bigint. Preserve sub-millisecond precision in the original raw payload.
      return value!=null&&value>=0&&Number.isSafeInteger(Math.trunc(value))?Math.trunc(value):null;
    };"""
    if result.count(old) != 1 or result.count('      ingest_version:25') != 1:
        raise ValueError('Reviewed captureUptimeMs/version changed; no patch generated')
    return result.replace(old, new, 1).replace('      ingest_version:25', '      ingest_version:26', 1)


def prepare(source: Path, output: Path, diff: Path | None = None) -> None:
    paths = [source.resolve(), output.resolve()] + ([diff.resolve()] if diff else [])
    if len(paths) != len(set(paths)):
        raise ValueError('Input, private output and public diff must differ')
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != BASELINE_SHA256:
        raise ValueError('Source differs from the reviewed v24 snapshot')
    original = data.decode('utf-8')
    updated = patched_text(original)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(updated, encoding='utf-8')
    output.chmod(0o600)
    if diff:
        # Publish the narrow v25 -> v26 change, without any authentication constants.
        baseline = patch_v25(original)
        diff.parent.mkdir(parents=True, exist_ok=True)
        diff.write_text(''.join(difflib.unified_diff(
            baseline.splitlines(keepends=True), updated.splitlines(keepends=True),
            fromfile='truckbox-ingest-v25/index.ts', tofile='truckbox-ingest-v26/index.ts', n=3,
        )), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--diff', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.output, args.diff)
    print('Private v26 source prepared; no deployment performed.')
