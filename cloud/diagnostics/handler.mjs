import { history, tripCounterReference } from './domain.mjs';
const headers = { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store',
  'access-control-allow-origin': '*', 'access-control-allow-headers': 'content-type', 'access-control-allow-methods': 'POST, OPTIONS' };
const response = (status, value) => new Response(JSON.stringify(value), { status, headers });
export function diagnosticsHandler(dependencies, catalog) {
  return async request => {
    if (request.method === 'OPTIONS') return new Response(null, { status: 204, headers });
    if (request.method !== 'POST') return response(405, { ok: false, error: 'METHOD_NOT_ALLOWED' });
    try {
      const text = await request.text();
      if (text.length > 8192) return response(413, { ok: false, error: 'REQUEST_TOO_LARGE' });
      let body; try { body = JSON.parse(text); } catch { return response(400, { ok: false, error: 'INVALID_JSON' }); }
      if (typeof body.device_uid !== 'string' || body.device_uid.length > 64 || typeof body.token !== 'string' || body.token.length < 16 || body.token.length > 256)
        return response(401, { ok: false, error: 'UNAUTHORIZED' });
      const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(body.token))), x => x.toString(16).padStart(2, '0')).join('');
      const device = await dependencies.device(body.device_uid);
      if (!device?.active || !device.truck_id || device.token_hash !== hash) return response(401, { ok: false, error: 'UNAUTHORIZED' });
      let coreMatches = false;
      if (body.core_device_uid != null) {
        if (typeof body.core_device_uid !== 'string' || body.core_device_uid.length > 64) return response(400, { ok: false, error: 'INVALID_CORE_IDENTITY' });
        const core = await dependencies.device(body.core_device_uid);
        if (!core?.active || core.truck_id !== device.truck_id) return response(403, { ok: false, error: 'CORE_VEHICLE_MISMATCH' });
        coreMatches = true;
      }
      const cursor = body.before_id == null ? null : String(body.before_id);
      if (cursor !== null && !/^\d{1,19}$/.test(cursor)) return response(400, { ok: false, error: 'INVALID_CURSOR' });
      const faultOffset = body.fault_offset ?? 0;
      if (!Number.isInteger(faultOffset) || faultOffset < 0 || faultOffset > 32768) return response(400, { ok: false, error: 'INVALID_FAULT_OFFSET' });
      const tripUid = body.trip_client_uid == null ? null : body.trip_client_uid;
      if (tripUid !== null && (typeof tripUid !== 'string' || !/^[A-Za-z0-9_-]{1,64}$/.test(tripUid))) return response(400, { ok: false, error: 'INVALID_TRIP' });
      const trip = tripUid ? await dependencies.trip(device.truck_id, tripUid) : null;
      if (tripUid && !trip) return response(404, { ok: false, error: 'TRIP_NOT_FOUND' });
      const rows = await dependencies.page(device.truck_id, cursor, trip);
      const merged = [...rows];
      const groups = new Map();
      for (const row of rows) if (row.raw?.event === 'DIAGNOSTIC_RAW') {
        const r = row.raw;
        if (Number.isInteger(r.boot_id) && Number.isInteger(r.capture_ms) && Number.isInteger(r.mid)) groups.set(`${r.boot_id}:${r.capture_ms}:${r.mid}`, r);
      }
      // Complete at most4 split messages across the page edge. No global scans.
      if (dependencies.fragments) for (const r of [...groups.values()].slice(0, 4))
        merged.push(...await dependencies.fragments(device.truck_id, r));
      // Trip endpoints can be hours apart and outside the current history page.
      if (tripUid && dependencies.boundaries)
        merged.push(...await dependencies.boundaries(device.truck_id, tripUid));
      const projection = history(merged, catalog, device.truck_id);
      const pageFaults = projection.faults.slice(faultOffset, faultOffset + 128);
      const operatingRow = rows.find(row => row.raw?.event === 'VOLVO_OPERATING');
      return response(200, { ok: true, truck_id: device.truck_id, vehicle_matches_core: coreMatches, ...projection, faults: pageFaults, faults_total: projection.faults.length, next_fault_offset: faultOffset + 128 < projection.faults.length ? faultOffset + 128 : null, operating: operatingRow ? { ...operatingRow.raw, observed_ts: operatingRow.ts } : null, catalog_version: catalog.catalog_version,
        next_before_id: rows.length === 200 ? String(rows[rows.length - 1].id) : null,
        page_rows: rows.length, trip_client_uid: tripUid,
        counter_reference: tripUid ? tripCounterReference(merged, tripUid) : null,
        financial_records_modified: false });
    } catch {
      return response(503, { ok: false, error: 'DIAGNOSTICS_TEMPORARILY_UNAVAILABLE' });
    }
  };
}
