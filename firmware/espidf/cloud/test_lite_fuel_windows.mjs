import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { PGlite } = require(process.env.TRUCKBOX_TEST_PGLITE || '@electric-sql/pglite');
const db = new PGlite();
const readSql = name => readFileSync(new URL(`./${name}`, import.meta.url), 'utf8');
const truck = '10000000-0000-0000-0000-000000000001';
const device = '20000000-0000-0000-0000-000000000001';
const trip = '30000000-0000-0000-0000-000000000001';
const stamp = Date.UTC(2026, 10, 22, 8);
const at = seconds => new Date(stamp + 1000 * seconds).toISOString();
const closeTo = (actual, expected) => assert.ok(Math.abs(actual - expected) < 0.000001, `${actual} != ${expected}`);

async function sample(tripId, seq, seconds, { rate = 36, raw = {}, odo = 10000, distance = 0, late = false } = {}) {
  await db.query(`insert into public.telemetry_samples
    (trip_id,truck_id,device_id,seq,ts,odometer_km,trip_distance_km,fuel_rate_lph,raw,created_at)
    values ($1,$2,$3,$4,$5,$6,$7,$8,$9::jsonb,case when $10 then now()-interval '1 minute' else now() end)
    on conflict (device_id,seq) do nothing`, [tripId, truck, device, seq, at(seconds), odo, distance, rate, JSON.stringify(raw), late]);
}

const core = { source: 'REVB1_LINK_LITE', core_uid: 'test-core', time_valid: true };
const flow = (boot, atMs, coveredMs, microlitres) => ({ ...core, boot_id: boot, f183: [atMs, coveredMs, microlitres] });
const pid250 = (seconds, fuel, mid = 128, estimated = 0) => ({ ...core, boot_id: 1,
  valid_mask: 96, estimated_mask: estimated, ecu_fuel_total_l: fuel,
  unix_ms: stamp + seconds * 1000, sensor_age_ms: 300, a250: [mid, 140, 300, 250] });
async function tail(id) {
  await db.query('select * from public.truckbox_apply_trip_rate_tail($1,180)', [id]);
  return (await db.query('select * from public.trips where id=$1', [id])).rows[0];
}

try {
  await db.exec(`
    create table public.trucks(id uuid primary key,fuel_calibration_factor numeric);
    create table public.events(type text, data jsonb);
    create table public.trips(id uuid primary key,truck_id uuid,status text,started_at timestamptz,ended_at timestamptz,
      start_odometer_km float8,end_odometer_km float8,distance_km float8,fuel_liters float8,average_kml float8,fuel_source text,
      start_core_fuel_liters float8,rate_fuel_liters float8,rate_last_ts timestamptz,rate_last_seq bigint,rate_last_lph float8,
      rate_gap_count integer,rate_gap_seconds float8,rate_gap_recovered_liters float8,rate_gap_recovery_count integer,updated_at timestamptz);
    create table public.telemetry_samples(id bigint generated always as identity,trip_id uuid,truck_id uuid,device_id uuid,
      seq bigint,ts timestamptz,trip_distance_km float8,odometer_km float8,fuel_rate_lph float8,
      trip_fuel_l float8,trip_average_kml float8,raw jsonb,created_at timestamptz,
      sample_kind text default 'OPERATIONAL',cloud_rate_applied boolean not null default false,unique(device_id,seq));
    create table public.telemetry_latest(truck_id uuid primary key,ts timestamptz,data jsonb,updated_at timestamptz);
    create function public.truckbox_core_gap_recovery(float8,float8,float8,float8,float8,float8) returns float8
    language sql as 'select 0::float8';
  `);
  await db.query(`insert into public.trucks values ($1,1)`, [truck]);
  // The rollback file contains the captured, unmodified implementations used as baseline in the fixture.
  await db.exec(readSql('fuel_window_rollback.sql'));
  await db.exec(readSql('fuel_window_review.sql'));
  await db.query(`insert into public.trips(id,truck_id,status,started_at,start_odometer_km) values ($1,$2,'ACTIVE',$3,10000)`, [trip,truck,at(0)]);
  await sample(trip, 1, 0);
  await sample(trip, 2, 10);
  await sample(trip, 3, 20, { raw: flow(11, 20000, 10000, 100000) });
  await sample(trip, 4, 30, { raw: flow(11, 30000, 20000, 288889), rate: 100 });
  let current = await tail(trip);
  closeTo(current.fuel_liters, 0.288889);
  assert.equal(current.fuel_source, 'J1587_PID183_WINDOW_CLOUD_V1');
  const weighted = (await db.query(`select raw->'pid183_window' as window from public.telemetry_samples where seq=4`)).rows[0].window;
  assert.equal(weighted.covered_ms, 10000);
  closeTo(weighted.litres, 0.188889);
  assert.ok(Math.abs(weighted.average_lph - 68) < 0.001);

  // Earlier markers added after the first rebuild leave the sum unchanged.
  await sample(trip, 5, 25, { raw: flow(11, 25000, 15000, 150000), late: true });
  current = await tail(trip);
  closeTo(current.fuel_liters, 0.288889);
  const nextMarker = flow(11, 60000, 45000, 788889);
  await sample(trip, 6, 60, { raw: nextMarker, rate: null });
  await sample(trip, 6, 60, { raw: nextMarker, rate: null });
  await sample(trip, 7, 65, { raw: nextMarker, rate: null });
  await sample(trip, 8, 70, { raw: flow(12, 2000, 0, 0) });
  await sample(trip, 9, 80, { raw: flow(12, 12000, 10000, 200000), distance: 2 });
  current = await tail(trip);
  closeTo(current.fuel_liters, 0.988889);
  assert.equal(Number(current.rate_last_seq), 9);
  assert.ok(current.rate_gap_seconds >= 5);
  await db.query(`select * from public.truckbox_rebuild_trip_fuel_rate($1,180)`, [trip]);
  closeTo((await tail(trip)).fuel_liters, 0.988889);

  const closures = [
    { index: 2, confirmed: true }, { index: 3, rightMid: 129 }, { index: 4, rightEstimated: 64 },
    { index: 5, regression: true }, { index: 6, adjustment: true }, { index: 7, futureOdometer: true }
  ];
  for (const { index, rightMid = 128, rightEstimated = 0, regression, adjustment, futureOdometer, confirmed } of closures) {
    const id = `30000000-0000-0000-0000-00000000000${index}`;
    const begin = index * 10000;
    const end = begin + 3600;
    await db.query(`insert into public.trips(id,truck_id,status,started_at,start_odometer_km,distance_km,
        fuel_liters,average_kml,fuel_source) values ($1,$2,'ACTIVE',$3,10000,60,28,60.0/28,'J1587_PID183_WINDOW_CLOUD_V1')`,
      [id,truck,at(begin)]);
    await sample(id, 100 + index, begin, { raw: pid250(begin,2000), odo: 10000 });
    if (regression) await sample(id, 200 + index, begin + 1000, { raw: pid250(begin + 1000,1990), odo: 10020 });
    if (adjustment) await db.query(`insert into public.events values ('MANUAL_FUEL_ADJUSTMENT',jsonb_build_object('trip_id',$1::text,'liters',2))`, [id]);
    const rightAt = futureOdometer ? end : end - 3;
    await sample(id, 300 + index, rightAt, { raw: pid250(rightAt,2030,rightMid,rightEstimated), odo: 10060 });
    const updated = (await db.query(`update public.trips set status='COMPLETED',ended_at=$1,end_odometer_km=10060,
                                       distance_km=60,fuel_liters=31,average_kml=60.0/31
                                     where id=$2 returning fuel_source,fuel_liters,average_kml`, [at(end),id])).rows[0];
    if (confirmed) {
      assert.equal(updated.fuel_source, 'J1587_PID250_ECU_TOTAL_DELTA');
      closeTo(updated.fuel_liters,30);
      closeTo(updated.average_kml,2);
      closeTo((await tail(id)).fuel_liters,30);
    } else {
      assert.equal(updated.fuel_source, 'J1587_PID183_WINDOW_CLOUD_V1');
      closeTo(updated.fuel_liters,adjustment ? 31 : 28);
    }
  }
  await db.exec(readSql('fuel_window_rollback.sql'));
  assert.equal((await db.query(`select to_regprocedure('public.truckbox_lite_flow_window_v1(uuid,jsonb,uuid,jsonb)') as fn`)).rows[0].fn,null);
  console.log('PostgreSQL local: legado, PID183 ponderado, perda de cobertura, replay, reboots, PID250 pareado e rollback PASS');
} finally { await db.close(); }
