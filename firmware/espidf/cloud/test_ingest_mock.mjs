#!/usr/bin/env node
// Executes a privately supplied function source with mocked Supabase/Deno only.
// Node >= 24; no dependency installation, credentials, database, or network calls.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import { webcrypto } from 'node:crypto';
import vm from 'node:vm';

const args = process.argv.slice(2);
const sourcePath = args[args.indexOf('--source') + 1];
const baselinePath = args.includes('--baseline') ? args[args.indexOf('--baseline') + 1] : null;
if (!args.includes('--source') || !sourcePath) throw new Error('Use: node test_ingest_mock.mjs --source PRIVATE_PATCHED.ts [--baseline PRIVATE_ORIGINAL.ts]');
const T0 = Date.parse('2026-11-06T12:00:00.000Z');
const TOKEN = 'unit-test-only-fake-token';
const tokenHash = Buffer.from(await webcrypto.subtle.digest('SHA-256', new TextEncoder().encode(TOKEN))).toString('hex');
const clone = x => structuredClone(x);

async function invoke(path, samples, options = {}) {
  const writes = [], rpcs = [], pending = [];
  const initialLatest = { device_id: 'test-device', seq: 1, ts: new Date(T0 - 60000).toISOString(), data: { rpm: 1500, speed_kmh: 40, odometer_km: null } };
  const state = { latest: clone(initialLatest), counters: { engine_seconds: 42 } };
  class Query {
    constructor(table) { this.table = table; this.operation = 'select'; }
    select() { return this; }
    eq() { return this; }
    order() { return this; }
    limit() { return this; }
    abortSignal() { return this; }
    upsert(data) { this.operation = 'upsert'; this.data = clone(data); return this; }
    insert(data) { this.operation = 'insert'; this.data = clone(data); return this; }
    update(data) { this.operation = 'update'; this.data = clone(data); return this; }
    async maybeSingle() {
      if (this.table === 'devices') return { data: { id: 'test-device', truck_id: 'test-truck', active: true, token_hash: tokenHash, last_seen_at: new Date().toISOString() }, error: null };
      if (this.table === 'trips') return { data: options.noTrip ? null : { id: 'test-trip', started_at: new Date(T0 - 3600000).toISOString(), start_odometer_km: null, distance_km: 0, fuel_liters: 10, average_kml: 2, rate_last_ts: null }, error: null };
      if (this.table === 'telemetry_latest') return { data: clone(state.latest), error: null };
      if (this.table === 'operation_state') return { data: { runtime_counters: clone(state.counters), last_telemetry_ts: null }, error: null };
      throw new Error('Unexpected mock query');
    }
    then(resolve, reject) {
      return (async () => {
        if (this.table === 'telemetry_samples' && options.bigintContract &&
            this.data.some(row=>row.uptime_ms!=null&&!Number.isSafeInteger(row.uptime_ms)))
          return {error:{code:'22P02',message:'invalid input syntax for type bigint'}};
        if ((this.table === 'telemetry_samples' && options.failPersistence) ||
            (this.table === 'health_minute_samples' && options.failHealthPersistence)) return { error: { message: 'mock persistence failure' } };
        writes.push({ table: this.table, operation: this.operation, data: clone(this.data) });
        if (this.table === 'telemetry_latest') state.latest = clone(this.data);
        if (this.table === 'operation_state' && this.data.runtime_counters) state.counters = clone(this.data.runtime_counters);
        return { error: null, data: null };
      })().then(resolve, reject);
    }
  }
  const sb = { from: table => new Query(table), rpc: async (name, params) => { rpcs.push({ name, params }); return { error: null }; } };
  let handler;
  const source = readFileSync(path, 'utf8').replace("import { createClient } from 'npm:@supabase/supabase-js@2';", 'const createClient = globalThis.__mockCreateClient;');
  const js = stripTypeScriptTypes(source, { mode: 'strip' });
  vm.runInNewContext(js, {
    __mockCreateClient: () => sb,
    Deno: { env: { get: () => 'mock-only' }, serve: fn => { handler = fn; } },
    EdgeRuntime: { waitUntil: promise => pending.push(promise) },
    crypto: webcrypto, TextEncoder, Request, Response, AbortSignal, Date,
    console: { log() {}, error() {} },
  }, { timeout: 5000 });
  assert.equal(typeof handler, 'function');
  const body = { device_uid: 'unit-test-device', token: TOKEN, boot_id: 55, device_time_ms: T0, device_uptime_ms: 20000, samples, ...options.body };
  const response = await handler(new Request('https://unit-test.invalid/ingest', { method: 'POST', body: JSON.stringify(body), headers: { 'content-type': 'application/json' } }));
  await Promise.all(pending);
  return { status: response.status, response: await response.json(), writes, rpcs, state, initialLatest,
    rows: writes.find(w => w.table === 'telemetry_samples')?.data ?? [] };
}

const diagnostics = [
  { seq: 2, sample_kind: 'RAW', network: 'J1939', boot_id: 55, uptime_us: 9000000, time_valid: true, unix_ms: T0 - 5000, raw: '01020304' },
  { seq: 3, sample_kind: 'DECODED', network: 'J1708', boot_id: 55, uptime_us: 10000000, time_valid: false, unix_ms: 0, readings: [{ pid: 190, value: 1200 }] },
  // Even unexpected sensor/counter fields on diagnostics must not affect aggregates.
  { seq: 4, sample_kind: 'J1708_REQUEST_EVENT', boot_id: 55, uptime_us: 11000000, time_valid: false, event: 'TIMEOUT', pid: 26, rpm: 0, fuel_rate_lph: 999, engine_seconds: 999 },
];

if (baselinePath) {
  const before = await invoke(baselinePath, diagnostics);
  assert.equal(before.status, 200);
  assert.equal(before.state.latest.seq, 4, 'original v24 must reproduce the diagnostic/latest regression');
  assert.ok(before.rpcs.some(r => r.name === 'truckbox_apply_trip_rate_tail'), 'original v24 must wrongly schedule trip-rate calculation');
  console.log('PASS: original v24 regression reproduced locally');
}

{
  const got = await invoke(sourcePath, diagnostics);
  assert.equal(got.status, 200);
  assert.equal(got.response.ack_seq, 4);
  assert.equal(got.response.accepted, 3);
  assert.deepEqual(got.state.latest, got.initialLatest);
  assert.deepEqual(got.state.counters, { engine_seconds: 42 });
  assert.equal(got.rpcs.length, 0);
  assert.equal(got.rows.length, diagnostics.length);
  for (const [index, row] of got.rows.entries()) {
    assert.deepEqual(row.raw, diagnostics[index], 'original diagnostic payload must be retained');
    assert.equal(row.trip_id, null);
    assert.equal(row.lat, null);
    assert.equal(row.lon, null);
  }
  assert.equal(got.rows[0].ts, new Date(T0 - 5000).toISOString());
  assert.equal(got.rows[1].ts, new Date(T0 - 10000).toISOString());
  assert.equal(got.rows[1].uptime_ms, 10000);
  assert.equal(got.rows[2].ts, new Date(T0 - 9000).toISOString());
  console.log('PASS: RAW/DECODED/request events persist and ACK without latest/trip/runtime side effects');
}
{
  const operational = { seq: 5, sample_kind: 'OPERATIONAL', boot_id: 55, uptime_ms: 20000, ts: new Date(T0).toISOString(), rpm: 1300, speed_kmh: 20, odometer_km: 101, fuel_rate_lph: 30, engine_seconds: 50 };
  const got = await invoke(sourcePath, [operational, { ...diagnostics[2], seq: 6, time_valid: true, unix_ms: T0 + 1000 }]);
  assert.equal(got.status, 200);
  assert.equal(got.response.ack_seq, 6);
  assert.equal(got.state.latest.seq, 5);
  assert.equal(got.state.latest.data.rpm, 1300);
  assert.equal(got.state.counters.engine_seconds, 50);
  assert.equal(got.rpcs.length, 1);
  assert.equal(got.rows[0].trip_id, 'test-trip');
  assert.equal(got.rows[1].trip_id, null);
  assert.equal(got.rows[1].raw.engine_seconds, 999);
  assert.equal(got.rows[1].raw.cloud_fuel_source, undefined);
  console.log('PASS: mixed batches update aggregates from OPERATIONAL only, ACKing every persisted sequence');
}
{
  const got = await invoke(sourcePath, [{ seq: 7, boot_id: 55, uptime_ms: 20000, rpm: 1400, fuel_rate_lph: 25 }], { noTrip: true });
  assert.equal(got.status, 200);
  assert.equal(got.state.latest.seq, 7);
  assert.equal(got.state.latest.data.rpm, 1400);
  assert.equal(got.response.ack_seq, 7);
  console.log('PASS: legacy samples without sample_kind retain operational behavior');
}
{
  const input = [{ ...diagnostics[0], seq: 8, boot_id: 54, time_valid: false, unix_ms: T0 - 99999 }];
  const before = Date.now();
  const got = await invoke(sourcePath, input);
  const after = Date.now();
  assert.equal(got.status, 200);
  assert.ok(Date.parse(got.rows[0].ts) >= before && Date.parse(got.rows[0].ts) <= after, 'unanchored old boot uses receipt time');
  assert.deepEqual(got.rows[0].raw, input[0]);
  assert.equal(got.rows[0].raw.time_valid, false);
  assert.equal(got.rows[0].lat, null);
  assert.equal(got.rows[0].lon, null);
  console.log('PASS: previous-boot monotonic samples keep original time quality and no invented GPS');
}
{
  const got = await invoke(sourcePath, diagnostics, { failPersistence: true });
  assert.equal(got.status, 500);
  assert.equal(got.response.ack_seq, undefined);
  assert.equal(got.rpcs.length, 0);
  assert.deepEqual(got.state.latest, got.initialLatest);
  assert.deepEqual(got.state.counters, { engine_seconds: 42 });
  console.log('PASS: persistence failure prevents ACK and aggregate side effects');
}
console.log('Cloud ingest compatibility mock tests: PASS (local only; no deployment/network/database)');
{
  // New Core guard diagnostic remains a normal durable request event. All
  // compact arrays/bit flags must survive ingest without trip/latest effects.
  const guard={seq:21,sample_kind:'J1708_REQUEST_EVENT',event:'GUARD_DECISION',boot_id:55,uptime_ms:15000,
    guard_v:1,guard_id:'55:15000000',mid:172,source:'LIVE_PROTECTION_DECISION',reason:'RX_ERROR_RATE_PERSISTENT',
    stopped:true,predicates:529,streak:3,fails:0,candidate_only:true,crc_hit:true,frame_hit:false,persist_hit:true,
    w:[5000,15000,599,40,0,0,0,0],ref:[0,3000,1800,1,0],totals:[9000,120,0,0,0,0,18,18,18,0,0,0,0],
    ci_ppm:[0,0],policy:[10000,3,99,60000,3,450,1200,20],mode:[3,0]};
  const got=await invoke(sourcePath,[guard]);
  assert.equal(got.status,200);assert.equal(got.response.ack_seq,21);
  assert.deepEqual(got.rows[0].raw,guard);assert.equal(got.rows[0].uptime_ms,15000);
  assert.deepEqual(got.state.latest,got.initialLatest);assert.equal(got.rpcs.length,0);
  assert.equal(got.rows[0].trip_id,null);assert.equal(got.rows[0].lat,null);assert.equal(got.rows[0].lon,null);
  console.log('PASS: compact GUARD_DECISION persists unchanged and ACKs without trip/latest or external-validation effects');
}
if (args.includes('--v25')) {
  const got = await invoke(sourcePath, diagnostics);
  for (const [index, row] of got.rows.entries()) {
    assert.equal(row.sample_kind, diagnostics[index].sample_kind);
    for (const key of ['rpm','fuel_rate_lph','engine_running','trip_fuel_l','gear','lat','lon'])
      assert.equal(row[key], null, `diagnostic ${key} must not reach SQL operational triggers`);
  }
  const nullable = await invoke(sourcePath, [{seq: 10, rpm: null, gear: null, fuel_rate_lph: '', speed_kmh: false}], {noTrip:true});
  assert.equal(nullable.status, 200);
  for (const key of ['rpm','gear','fuel_rate_lph','speed_kmh']) assert.equal(nullable.rows[0][key], null);
  for (const bad of [null, -1, 1.5, '', false, {}, 9007199254740992]) {
    const invalid = await invoke(sourcePath, [{seq:11}, {seq:bad}]);
    assert.equal(invalid.status, 400);
    assert.equal(invalid.response.ack_seq, undefined);
    assert.equal(invalid.writes.length, 0);
  }
  const malformed = await invoke(sourcePath, [], {body:{samples:{seq:12}}});
  assert.equal(malformed.status, 400);
  const badToken = await invoke(sourcePath, [{seq:12}], {body:{token:'wrong-token'}});
  assert.equal(badToken.status, 401);
  assert.equal(badToken.writes.length, 0);
  const health = {seq:13,sample_kind:'HEALTH',boot_id:55,uptime_us:10000000,time_valid:false,metrics:{queue_depth:0}};
  const hgot = await invoke(sourcePath, [health], {noTrip:true});
  assert.equal(hgot.status, 200);
  assert.equal(hgot.response.ack_seq, 13);
  assert.deepEqual(hgot.writes.find(w=>w.table==='health_minute_samples').data[0].context.firmware_record, health);
  const hfailed = await invoke(sourcePath, [health], {failHealthPersistence:true});
  assert.equal(hfailed.status, 500);
  assert.equal(hfailed.response.ack_seq, undefined);
  console.log('PASS: v25 preserves diagnostic kinds, NULLs and health provenance; malformed sequences/auth/storage errors never ACK');
}
if (args.includes('--v26')) {
  const samples = [{seq:14,sample_kind:'RAW',boot_id:55,uptime_us:339721,time_valid:false}];
  const v25Path = args[args.indexOf('--v25-source') + 1];
  if (args.includes('--v25-source')) {
    const before=await invoke(v25Path,samples,{bigintContract:true});
    assert.equal(before.status,500);assert.equal(before.response.ack_seq,undefined);
    console.log('PASS: v25 fractional uptime reproduces PostgreSQL bigint rejection before ACK');
  }
  for (const [time,expected] of [
    [{uptime_us:339721},339],[{uptime_ms:339.721},339],
    [{uptime_ms:null,uptime_us:1234567},1234],[{uptime_us:1},0],
    [{uptime_us:0},0],[{uptime_us:-1},null],[{uptime_ms:1e20},null],[{},null],
  ]) {
    const input={seq:15,sample_kind:'RAW',time_valid:false,...time};
    const got=await invoke(sourcePath,[input],{bigintContract:true});
    assert.equal(got.status,200);assert.equal(got.response.ack_seq,15);
    assert.equal(got.rows[0].uptime_ms,expected);
    assert.deepEqual(got.rows[0].raw,input);
  }
  console.log('PASS: v26 writes integer/null milliseconds and preserves original microseconds without GPS');
}
