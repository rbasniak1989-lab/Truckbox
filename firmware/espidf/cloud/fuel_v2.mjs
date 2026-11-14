// Pure normalization policy; no network/database writes. Historical trips are
// not rebuilt. ECU interval deltas and repeated audit rows must never be summed.
export function isCoreFuelV2(s) {
  return s?.trip_accounting?.v === 2 && Number.isFinite(s?.trip_fuel_l) && s.trip_fuel_l >= 0;
}
export function normalizeCoreFuelV2(row, trip) {
  if (!isCoreFuelV2(row.raw)) return row;
  const a = row.raw.trip_accounting, op = a.op;
  const matched = trip && typeof trip.client_uid === 'string' && Array.isArray(op) &&
    op.length === 3 && op[0] === trip.client_uid && Number.isFinite(op[1]) && op[1] >= 0 && typeof op[2] === 'boolean';
  const fuel = matched ? op[1] : null;
  return {...row, cloud_rate_applied: true, trip_fuel_l: fuel,
    trip_average_kml: fuel > 0 && row.trip_distance_km >= 0 ? row.trip_distance_km / fuel : null,
    raw: {...row.raw, core_fuel_l: row.raw.trip_fuel_l,
      core_trip_fuel_l: row.raw.trip_fuel_l, fuel_accounting_v: 2,
      cloud_fuel_source: matched ? 'CORE_PID183_PID250_V2' : 'CORE_V2_OPERATION_REFERENCE_REQUIRED',
      operation_allocation_estimated: matched ? op[2] : null}};
}
export function fuelIntervalKey(deviceUid, event) {
  if (event?.event !== 'FUEL_RECONCILIATION' || event?.v !== 2 ||
      !Array.isArray(event.anchor) || event.anchor.length !== 7 ||
      !Array.isArray(event.previous) || event.previous.length !== 4) return null;
  // Include real endpoint boot/uptime and raw counters, not just a resettable
  // per-device interval counter or the repeated snapshot's timestamp/sequence.
  return JSON.stringify([deviceUid,...event.previous,event.anchor[0],event.anchor[1],event.anchor[3],event.anchor[4]]);
}
