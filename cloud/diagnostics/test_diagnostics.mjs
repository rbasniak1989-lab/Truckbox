import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { parse194, describe, history, assembleRaw, counterDelta, tripCounterReference } from './domain.mjs';
import { diagnosticsHandler } from './handler.mjs';
const catalog = JSON.parse(readFileSync(new URL('./volvo-fh-e5-0.1.0.json', import.meta.url)));
const fixtures = JSON.parse(readFileSync(new URL('../../firmware/espidf/test_evidence/v0217_cloud_readonly.json', import.meta.url)));
const fixtures10 = JSON.parse(readFileSync(new URL('../../firmware/espidf/test_evidence/v0217_cloud_10nov_readonly.json', import.meta.url)));
test('actual Cloud11Nov and preserved NEXIQ value bytes; four required cases', () => {
  const cases = [['12B501', 18, 'SID', 5, 'ACTIVE', 1], ['0235', 2, 'SID', 5, 'ACTIVE', null],
    ['25A101', 37, 'PID', 1, 'ACTIVE', 1], ['25E100', 37, 'PID', 1, 'INACTIVE', 0]];
  for (const [hex, identifier, kind, fmi, state, occurrences] of cases) {
    const p = parse194(hex); assert.equal(p.valid, true); assert.equal(p.codes.length, 1);
    assert.deepEqual([p.codes[0].identifier, p.codes[0].identifier_type, p.codes[0].fmi, p.codes[0].state, p.codes[0].occurrences], [identifier, kind, fmi, state, occurrences]);
  }
  const h = history(fixtures10.cloud_samples.concat(fixtures.cloud_samples), catalog, 'truck-test');
  assert.equal(h.faults.length, 2); assert.equal(h.faults.find(x => x.mid === 128).identifier, 18);
  assert.equal(h.faults.find(x => x.mid === 136).identifier, 2);
  assert.deepEqual(h.faults.find(x => x.mid === 136).raw_legacy.slice(0, 3), ['0235', 14, 136]);
});
test('multiple faults, extended identifiers, unknowns, incomplete and empty tables', () => {
  assert.equal(parse194('12b501023525e100').codes.length, 3);
  assert.equal(parse194('b705').codes[0].identifier, 439);
  assert.equal(parse194('1015').codes[0].identifier_type, 'SID');
  assert.equal(parse194('1015').codes[0].identifier, 272);
  for (const p of ['1', 'GG', '12b5', '120001']) assert.equal(parse194(p).valid, false);
  assert.deepEqual(parse194('').codes, []);
  assert.equal(parse194('0025'.repeat(119)).codes.length, 119);
});
test('candidate OEM definitions are scoped/versioned; water code is not water detection', () => {
  const d = describe({ mid: 128, identifier: 18, identifier_type: 'SID', fmi: 5 }, catalog);
  assert.equal(d.oem_confirmed, false); assert.match(d.driver_pt, /não confirma água/);
  assert.match(d.confidence, /CONFLICTING/);
  const unknown = describe({ mid: 140, identifier: 300, identifier_type: 'PSID', fmi: 5 }, catalog);
  assert.equal(unknown.confidence, 'UNKNOWN'); assert.equal(unknown.severity, 'UNCLASSIFIED');
  assert.equal(describe({ mid: 128, identifier: 18, identifier_type: 'SID', fmi: 5 }, catalog,
    { manufacturer: 'Volvo', family: 'VN', year: 2007 }).confidence, 'UNKNOWN');
  assert.equal(catalog.fmi['15'].includes('reservado'), true);
});
const sample = (id, boot, uptime, hex, mid = 130) => ({ id, seq: id, ts: '2026-11-11T10:00:00Z',
  raw: { boot_id: boot, uptime_ms: uptime, time_valid: false, j1587_raw: { '194': [hex, 0, mid] } } });
test('history keeps modules/codes separate; missing broadcast never resolves; no duplicate activation', () => {
  const h = history([sample(1, 1, 1000, '25A101'), sample(2, 1, 2000, '25A101'), sample(3, 1, 3000, ''),
    sample(4, 1, 4000, '25E100'), sample(5, 2, 1000, '25A102'), sample(6, 2, 2000, '0235', 136)], catalog, 'vehicle');
  assert.equal(h.faults.length, 2); const f = h.faults.find(x => x.mid === 130);
  assert.equal(f.state, 'ACTIVE'); assert.equal(f.transitions, 2); assert.equal(f.first.uptime_ms, 1000);
  assert.equal(f.last.boot, 2); assert.equal(f.occurrences, 2); assert.equal(f.resolved_by_absence, false);
  // Upload of an old NVS observation after reboot must not replace newer state.
  const old = { ...sample(99, 3, 1000, ''), raw: { event: 'DIAGNOSTIC_DTC', mid: 130, parameter_hex: '25E100', first: [1, 1000, null], last: [1, 4000, null] } };
  const result = history([sample(5, 2, 1000, '25A102'), old], catalog, 'vehicle');
  assert.equal(result.faults[0].state, 'ACTIVE');
});
test('PID192 completed RAW projection; partial groups stay explicitly incomplete', () => {
  const make = (offset, hex) => ({ id: offset + 1, seq: offset + 1, raw: { event: 'DIAGNOSTIC_RAW', boot_id: 306, mid: 128, capture_ms: 1234, uptime_ms: 1234,
    total_bytes: 18, offset, parameter_hex: hex } });
  const payload = '12B501'.repeat(6), a = make(0, payload.slice(0, 28)), b = make(14, payload.slice(28));
  assert.equal(assembleRaw([a]).incomplete.length, 1); assert.equal(assembleRaw([a, b]).codes.length, 6);
  assert.equal(assembleRaw([a, { ...b, raw: { ...b.raw, offset: 0 } }]).codes.length, 0);
});
test('counter resets, MID changes, missing time, jumps and independent collection instants', () => {
  const a = { raw: 1000, mid: 128, boot: 1, uptime_ms: 1000, unix_ms: null }, b = { ...a, raw: 1010, uptime_ms: 3601000 };
  assert.ok(Math.abs(counterDelta(a, b, 0).value - 4.73176473) < 1e-9);
  assert.equal(counterDelta(a, { ...b, raw: 1 }, 0).quality, 'RESET_OR_ECU_REPLACEMENT');
  assert.equal(counterDelta(a, { ...b, mid: 130 }, 0).value, null);
  assert.equal(counterDelta(a, { ...b, boot: 2 }, 0).quality, 'TIME_INTERVAL_UNKNOWN');
  assert.equal(counterDelta(a, { ...b, raw: 10000000 }, 0).quality, 'IMPLAUSIBLE_JUMP');
  const reading = (pid, raw, ms) => [pid, 128, [0, 8, 16, 24].map(n => ((raw >>> n) & 255).toString(16).padStart(2, '0')).join(''), 0, pid === 235 ? 'h' : 'L', 1, ms, null, 'ECU_COUNTER_EM_VALIDACAO'];
  const start = { id: 1, raw: { event: 'COUNTER_CAPTURE', reason: 4, capture_id: 1, trip_client_uid: 'trip1', counters: [reading(250, 1000, 1000), reading(236, 100, 2000), reading(235, 100, 3000)] } };
  const end = { id: 2, raw: { event: 'COUNTER_CAPTURE', reason: 5, capture_id: 2, trip_client_uid: 'trip1', counters: [reading(250, 1010, 3601000), reading(236, 102, 3602000), reading(235, 101, 3603000)] } };
  const result = tripCounterReference([start, end], 'trip1'); assert.ok(result.fuel_l > 4.73); assert.ok(result.nonidle_fuel_l > 3.78); assert.equal(result.simultaneous, false);
  end.raw.counters[1][6] += 10000; assert.equal(tripCounterReference([start, end], 'trip1').nonidle_fuel_l, null);
  assert.equal(tripCounterReference([start], 'trip1').missing_boundary, true);
});
test('read-only handler requires device auth and scopes every query to its truck; no financial writes', async () => {
  const token = 'test-only-fake-token-12345';
  const hash = Buffer.from(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(token))).toString('hex');
  const queried = [];
  const dependencies = { device: async uid => ({ active: uid === 'test-core', token_hash: hash, truck_id: 'scoped-truck' }),
    trip: async (truck, uid) => { queried.push(truck); return uid === 'trip1' ? { id: 'trip1' } : null; },
    page: async truck => { queried.push(truck); return fixtures.cloud_samples; } };
  const handler = diagnosticsHandler(dependencies, catalog);
  const call = body => handler(new Request('https://local.invalid', { method: 'POST', body: JSON.stringify(body) }));
  assert.equal((await call({ device_uid: 'test-core', token: 'wrong-token-123456' })).status, 401);
  assert.equal(queried.length, 0);
  const result = await call({ device_uid: 'test-core', token, truck_id: 'foreign-truck', trip_client_uid: 'trip1' });
  const body = await result.json(); assert.equal(result.status, 200); assert.equal(body.faults.length, 2);
  assert.equal(body.financial_records_modified, false); assert.deepEqual(new Set(queried), new Set(['scoped-truck']));
  assert.equal((await call({ device_uid: 'test-core', token, trip_client_uid: 'foreign-trip' })).status, 404);
  dependencies.page = async () => { throw Error('offline'); };
  assert.equal((await call({ device_uid: 'test-core', token })).status, 503);
});

test('bundled Android cache is the exact versioned small catalog', () => {
  const cached = JSON.parse(readFileSync(new URL('../../app/src/main/assets/volvo-diagnostics-cache.json', import.meta.url)));
  assert.deepEqual(cached, catalog);
});
