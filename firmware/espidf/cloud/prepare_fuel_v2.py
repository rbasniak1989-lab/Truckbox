"""Generate a review-only patch against the exact observed ingest v26.

No deployment, network access, database changes or history rebuild is performed.
SQL guards in fuel_v2_review.sql must be reviewed together with this patch.
"""
from pathlib import Path
import argparse
import difflib
import hashlib

BASE_SHA256 = "329573d097b495ee19f44065ed7324928b301e42b7a01ba978c271370e7cabd3"
REPLACEMENTS = (
    (".select('id,weight_t,started_at,start_odometer_km,distance_km,fuel_liters,average_kml,fuel_source,rate_fuel_liters,rate_last_ts,rate_last_seq,rate_last_lph,rate_gap_count,rate_gap_seconds')",
     ".select('id,client_uid,weight_t,started_at,start_odometer_km,distance_km,fuel_liters,average_kml,fuel_source,rate_fuel_liters,rate_last_ts,rate_last_seq,rate_last_lph,rate_gap_count,rate_gap_seconds')"),
    ("    let rateUpdateStatus='SKIPPED';", "    rows=rows.map((r:any)=>normalizeCoreFuelV2(r,activeTrip));\n    let rateUpdateStatus='SKIPPED';"),
    ("if(shouldQueueTripRate){", "if(shouldQueueTripRate && !tripRows.some((r:any)=>isCoreFuelV2(r.raw))){"),
    ("if(activeTrip?.id&&candidate.trip_id===activeTrip.id&&activeTrip.fuel_liters!=null){",
     "if(!isCoreFuelV2(candidate.raw)&&activeTrip?.id&&candidate.trip_id===activeTrip.id&&activeTrip.fuel_liters!=null){"),
)

def prepare(original: Path, output: Path, diff: Path):
    if len({original.resolve(), output.resolve(), diff.resolve()}) != 3:
        raise ValueError("Source, review output and diff must differ")
    source = original.read_text()
    if hashlib.sha256((source.rstrip()+'\n').encode()).hexdigest() != BASE_SHA256:
        raise ValueError("Deployed source SHA256 differs; fetch/audit the new version first")
    changed = source
    for old, new in REPLACEMENTS:
        if changed.count(old) != 1:
            raise ValueError("Reviewed fragment missing or duplicated")
        changed = changed.replace(old, new)
    helper = Path(__file__).with_name("fuel_v2.mjs").read_text().replace("export function ", "function ")
    # Helpers have TypeScript annotations only in generated TypeScript. This
    # keeps pure policy testable with Node without loading Deno/Supabase.
    for old, new in (("isCoreFuelV2(s)","isCoreFuelV2(s:any)"),
                     ("normalizeCoreFuelV2(row, trip)","normalizeCoreFuelV2(row:any, trip:any)"),
                     ("fuelIntervalKey(deviceUid, event)","fuelIntervalKey(deviceUid:any, event:any)")):
        helper = helper.replace(old,new)
    changed = helper + "\n" + changed
    output.write_text(changed)
    diff.write_text("".join(difflib.unified_diff(source.splitlines(True), changed.splitlines(True),
        fromfile="truckbox-ingest-v26.ts", tofile="truckbox-ingest-fuel-v2-review.ts",n=2)))

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("original",type=Path);p.add_argument("output",type=Path);p.add_argument("diff",type=Path)
    args=p.parse_args();prepare(args.original,args.output,args.diff)
