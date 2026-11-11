// Value-byte parser and read-only projections. No writes, ECU commands or ACKs.
export function parse194(hex) {
  if (typeof hex !== 'string' || hex.length > 478 || hex.length % 2 || !/^[0-9a-f]*$/i.test(hex))
    return { valid: false, codes: [], error: 'INVALID_HEX' };
  const bytes = Uint8Array.from(hex.match(/../g) ?? [], x => parseInt(x, 16));
  const codes = [];
  for (let i = 0; i < bytes.length;) {
    if (i + 2 > bytes.length) return { valid: false, codes, error: 'TRUNCATED_IDENTIFIER' };
    const a = bytes[i], b = bytes[i + 1], size = b & 0x80 ? 3 : 2;
    if (i + size > bytes.length) return { valid: false, codes, error: 'MISSING_OCCURRENCES' };
    codes.push({ identifier: a + (b & 0x20 ? 0 : 256), identifier_type: b & 0x10 ? 'SID' : 'PID',
      page2: !(b & 0x20), fmi: b & 15, state: b & 0x40 ? 'INACTIVE' : 'ACTIVE',
      occurrences: size === 3 ? bytes[i + 2] : null, parameter_hex: hex.slice(i * 2, (i + size) * 2).toUpperCase() });
    i += size;
  }
  return { valid: true, codes, error: null };
}
export function describe(code, catalog, profile = { manufacturer: 'Volvo', family: 'FH Euro 5', year: 2013 }) {
  const match = catalog.definitions.find(x => x.manufacturer === profile.manufacturer && x.family === profile.family &&
    profile.year >= x.year_from && profile.year <= x.year_to && x.mid === code.mid &&
    x.identifier_type === code.identifier_type && x.identifier === code.identifier && x.fmi === code.fmi);
  return { catalog_version: catalog.catalog_version,
    module: catalog.modules[String(code.mid)] ?? `Módulo MID ${code.mid}`,
    generic_fmi_pt: catalog.fmi[String(code.fmi)] ?? 'FMI desconhecido',
    ...(match ?? { ...catalog.unknown_policy, confidence: 'UNKNOWN', component: 'Componente OEM desconhecido',
      possible_causes: [], recommended_checks: ['Registrar o código completo e consultar a documentação específica do chassi.'], sources: [] }) };
}
const finite = x => typeof x === 'number' && Number.isFinite(x);
const validMid = x => Number.isInteger(x) && x >= 128 && x <= 255;
function point(sample, age = 0) {
  const raw = sample.raw ?? {}, ms = finite(raw.uptime_ms) ? raw.uptime_ms - age * 1000 : null;
  const parsed = Date.parse(sample.ts);
  const utc = raw.time_valid === true && Number.isFinite(parsed) ? parsed - age * 1000 : null;
  return { boot: Number.isInteger(raw.boot_id) ? raw.boot_id : null, uptime_ms: ms !== null && ms >= 0 ? ms : null,
    unix_ms: utc, time_quality: utc !== null ? 'SYSTEM_CLOCK' : 'BOOT_MONOTONIC_OR_UNKNOWN',
    seq: sample.seq == null ? null : String(sample.seq), sample_id: String(sample.id ?? '') };
}
function ref(array, fallback) {
  return Array.isArray(array) && array.length === 3 && Number.isInteger(array[0]) && finite(array[1]) ?
    { ...fallback, boot: array[0], uptime_ms: array[1], unix_ms: finite(array[2]) ? array[2] : null,
      time_quality: finite(array[2]) ? 'SYSTEM_CLOCK' : 'BOOT_MONOTONIC_OR_UNKNOWN' } : fallback;
}
export function extractCodes(sample) {
  const raw = sample.raw ?? {}, results = [];
  const add = (mid, hex, age = 0, first = null, last = null, context = null) => {
    if (!validMid(mid) || !finite(age) || age < 0) return;
    const parsed = parse194(hex);
    if (!parsed.valid) return; // invalid bytes remain in the source row; never clear a previous state
    const at = point(sample, age);
    for (const code of parsed.codes) results.push({ ...code, mid, first: ref(first, at), last: ref(last, at),
      context, raw_legacy: raw.j1587_raw?.['194'] ?? raw.metrics?.j1587_raw?.['194'] ?? null,
      trip_client_uid: typeof raw.trip_client_uid === 'string' ? raw.trip_client_uid : null });
  };
  if (raw.event === 'DIAGNOSTIC_DTC') add(raw.mid, raw.parameter_hex, 0, raw.first, raw.last, raw.context);
  for (const bucket of [raw.j1587_raw, raw.metrics?.j1587_raw]) {
    const value = bucket?.['194'];
    if (Array.isArray(value) && value.length >= 3) add(value[2], value[0], value[1]);
  }
  if (Array.isArray(raw.values)) for (const value of raw.values)
    if (value?.pid === 194) add(value.mid, value.raw);
  return results;
}
export function assembleRaw(rows) {
  const groups = new Map(), result = [], incomplete = [];
  for (const row of rows) {
    const r = row.raw ?? {};
    if (r.event !== 'DIAGNOSTIC_RAW' || !validMid(r.mid) || !Number.isInteger(r.total_bytes) || r.total_bytes > 239 ||
      r.total_bytes < 0 || !Number.isInteger(r.offset) || typeof r.parameter_hex !== 'string' || !/^(?:[0-9a-f]{2})*$/i.test(r.parameter_hex)) continue;
    const key = `${r.boot_id}:${r.mid}:${r.capture_ms}`;
    let group = groups.get(key);
    if (!group) { group = { row, size: r.total_bytes, hex: Array(r.total_bytes), invalid: false }; groups.set(key, group); }
    if (group.size !== r.total_bytes || r.offset < 0 || r.offset + r.parameter_hex.length / 2 > group.size) { group.invalid = true; continue; }
    for (let i = 0; i < r.parameter_hex.length; i += 2) {
      const index = r.offset + i / 2, byte = r.parameter_hex.slice(i, i + 2).toUpperCase();
      if (group.hex[index] != null && group.hex[index] !== byte) group.invalid = true;
      group.hex[index] = byte;
    }
  }
  for (const [key, g] of groups) {
    if (g.invalid || Array.from(g.hex).some(x => x == null)) { incomplete.push(key); continue; }
    const r = g.row.raw, parsed = parse194(g.hex.join(''));
    if (!parsed.valid) { incomplete.push(key); continue; }
    for (const code of parsed.codes) result.push({ ...code, mid: r.mid, first: point(g.row), last: point(g.row),
      context: null, raw_legacy: null, trip_client_uid: r.trip_client_uid ?? null });
  }
  return { codes: result, incomplete };
}
function compare(a, b) {
  if (Number.isInteger(a.boot) && a.boot === b.boot && finite(a.uptime_ms) && finite(b.uptime_ms)) return a.uptime_ms - b.uptime_ms;
  if (Number.isInteger(a.boot) && Number.isInteger(b.boot) && a.boot !== b.boot) return a.boot - b.boot;
  if (finite(a.unix_ms) && finite(b.unix_ms)) return a.unix_ms - b.unix_ms;
  const x = /^\d+$/.test(a.seq ?? '') ? BigInt(a.seq) : 0n, y = /^\d+$/.test(b.seq ?? '') ? BigInt(b.seq) : 0n;
  return x < y ? -1 : x > y ? 1 : 0;
}
export function history(rows, catalog, truckId, profile) {
  const assembled = assembleRaw(rows), faults = new Map();
  const codes = rows.flatMap(extractCodes).concat(assembled.codes).sort((a, b) => compare(a.last, b.last));
  for (const c of codes) {
    const key = `${truckId}:${c.mid}:${c.identifier_type}:${c.identifier}:${c.fmi}`;
    const previous = faults.get(key), first = previous && compare(previous.first, c.first) < 0 ? previous.first : c.first;
    const transitions = (previous?.transitions ?? 0) + (previous && previous.state !== c.state ? 1 : 0);
    faults.set(key, { ...c, key, first, transitions, severity: 'UNCLASSIFIED', resolved_by_absence: false,
      definition: describe(c, catalog, profile) });
  }
  return { faults: [...faults.values()], incomplete_raw_groups: assembled.incomplete,
    history_complete: false, note: 'Histórico restrito às páginas consultadas; ausência não significa resolução.' };
}
export function counterDelta(a, b, index) {
  if (!a || !b || !Number.isInteger(a.raw) || !Number.isInteger(b.raw) || a.raw < 0 || a.raw >= 0xFFFFFFFF || b.raw < 0 || b.raw >= 0xFFFFFFFF) return { value: null, quality: 'MISSING' };
  if (a.mid !== b.mid) return { value: null, quality: 'CHANGED_ECU_SOURCE' };
  if (b.raw < a.raw) return { value: null, quality: a.raw >= 0xF0000000 && b.raw < 0x10000000 ? 'POSSIBLE_ROLLOVER_OR_RESET' : 'RESET_OR_ECU_REPLACEMENT' };
  const elapsed = a.unix_ms != null && b.unix_ms != null ? b.unix_ms - a.unix_ms :
    a.boot === b.boot ? b.uptime_ms - a.uptime_ms : null;
  if (!finite(elapsed) || elapsed < 0) return { value: null, quality: 'TIME_INTERVAL_UNKNOWN' };
  const value = (b.raw - a.raw) * (index === 2 ? .05 : .473176473);
  if (value > (index === 2 ? 1 : 1000) * elapsed / 3600000 + (index === 2 ? .1 : .946352946))
    return { value: null, quality: 'IMPLAUSIBLE_JUMP' };
  return { value, quality: 'ECU_COUNTER_DELTA_SCALE_PROVISIONAL' };
}
export function counterSamples(rows) {
  return rows.filter(row => row.raw?.event === 'COUNTER_CAPTURE').map(row => ({ ...row.raw, row_id: String(row.id ?? ''), row_seq: String(row.seq ?? ''),
    readings: (row.raw.counters ?? []).map(c => {
      if (!Array.isArray(c) || c.length < 9 || typeof c[2] !== 'string' || !/^[0-9a-f]{8}$/i.test(c[2]) || !validMid(c[1])) return null;
      const bytes = c[2].match(/../g).map(x => parseInt(x, 16));
      const raw = bytes.reduce((sum, x, i) => sum + x * 2 ** (8 * i), 0);
      return { pid: c[0], mid: c[1], raw, value: c[3], unit: c[4], boot: c[5], uptime_ms: c[6], unix_ms: c[7], quality: c[8] };
    }) }));
}
export function tripCounterReference(rows, uid) {
  const captures = counterSamples(rows).filter(x => x.trip_client_uid === uid);
  const starts = captures.filter(x => x.reason === 4).sort((a, b) => compare({ ...(a.readings.find(Boolean) ?? {}), seq: a.row_seq }, { ...(b.readings.find(Boolean) ?? {}), seq: b.row_seq }));
  const ends = captures.filter(x => x.reason === 5).sort((a, b) => compare({ ...(b.readings.find(Boolean) ?? {}), seq: b.row_seq }, { ...(a.readings.find(Boolean) ?? {}), seq: a.row_seq }));
  const start = starts[0], end = ends[0];
  if (!start || !end) return { fuel_l: null, idle_fuel_l: null, idle_hours: null, nonidle_fuel_l: null,
    source: 'INTEGRATED_OR_ESTIMATED_FALLBACK_REQUIRED', missing_boundary: true };
  const delta = [250, 236, 235].map((pid, i) => counterDelta(start.readings.find(x => x?.pid === pid), end.readings.find(x => x?.pid === pid), i));
  const aligned = (readings) => {
    const p = [250, 236].map(pid => readings.find(x => x?.pid === pid));
    return p.every(Boolean) && p[0].boot === p[1].boot && Math.abs(p[0].uptime_ms - p[1].uptime_ms) <= 5000;
  };
  const nonidle = aligned(start.readings) && aligned(end.readings) && delta[0].value !== null && delta[1].value !== null && delta[0].value >= delta[1].value ? delta[0].value - delta[1].value : null;
  return { fuel_l: delta[0].value, idle_fuel_l: delta[1].value, idle_hours: delta[2].value, nonidle_fuel_l: nonidle,
    source: delta[0].value == null ? 'INTEGRATED_OR_ESTIMATED_FALLBACK_REQUIRED' : 'ECU_BOUNDARY_CAPTURE_INTERVAL',
    qualities: delta.map(x => x.quality), missing_boundary: false, simultaneous: false,
    start_capture: start.capture_id, end_capture: end.capture_id,
    boundary_precision: 'READINGS_AFTER_EVENT_NOT_EXACT_INSTANT' };
}
