import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { PGlite } = require(process.env.TRUCKBOX_TEST_PGLITE || '@electric-sql/pglite');
const db = new PGlite();
const sql = name => readFileSync(new URL(name, import.meta.url), 'utf8');
const truck = '637e3c7a-2431-43c9-ac10-3d0c4e6f8de0';
const trip = '5a63747c-daa7-4945-9b56-390b9c6d972a';
const device = '20000000-0000-0000-0000-000000000001';
const uid = 'tb-c6-test';
const reference = Date.parse('2026-11-22T21:25:00Z');
const iso = ms => new Date(ms).toISOString();
const approx = (value, expected) => assert.ok(Math.abs(value - expected) < 1e-7, `${value} ≠ ${expected}`);
let seq = 1000;

async function sample(tripId, ts, {wire, boot = 1, rate = 50, distance = 742.2} = {}) {
  const raw = wire ? {core_uid:uid, source:'REVB1_LINK_LITE', boot_id:boot, time_valid:true, w183:wire} : {};
  await db.query(`insert into public.telemetry_samples
    (trip_id,truck_id,device_id,seq,ts,trip_distance_km,fuel_rate_lph,raw)
    values ($1,$2,$3,$4,$5,$6,$7,$8::jsonb)`, [tripId,truck,device,seq++,iso(ts),distance,rate,JSON.stringify(raw)]);
}
function window(slot, start, ul = 200000, total = slot * ul, covered = 10000, counter = []) {
  return [slot,start+slot*10000,ul,covered,8,2000,288000,total,...counter];
}
function counter(slot, ticks, integratedUl, flags = 3, mid = 128) {
  return [slot*10000-100, ticks, mid, flags, integratedUl,slot*10000-100];
}
async function state(tripId = trip) {
  return (await db.query('select * from public.trips where id=$1',[tripId])).rows[0];
}
async function tail(tripId = trip) {
  await db.query('select * from public.truckbox_apply_trip_rate_tail($1,180)',[tripId]);
  return state(tripId);
}
async function rebuild(tripId = trip) {
  await db.query('select * from public.truckbox_rebuild_trip_fuel_rate($1,180)',[tripId]);
  return state(tripId);
}

try {
  await db.exec(`
    create role anon;
    create role authenticated;
    create role service_role bypassrls;
    create table public.trucks(id uuid primary key,fuel_calibration_factor numeric,fuel_calibration_started_at timestamptz);
    create table public.devices(id uuid primary key,truck_id uuid not null,device_uid text not null);
    create table public.events(type text,ts timestamptz,data jsonb);
    create table public.trips(id uuid primary key,truck_id uuid,status text,started_at timestamptz,ended_at timestamptz,
      start_odometer_km float8,end_odometer_km float8,distance_km float8,fuel_liters float8,average_kml float8,fuel_source text,
      start_core_fuel_liters float8,end_core_fuel_liters float8,rate_fuel_liters float8,rate_last_ts timestamptz,rate_last_seq bigint,
      rate_last_lph float8,rate_gap_count integer,rate_gap_seconds float8,rate_gap_recovered_liters float8,rate_gap_recovery_count integer,
      live_baseline_at timestamptz,live_base_distance_km float8,live_core_start_odometer_km float8,updated_at timestamptz);
    create table public.telemetry_samples(id bigint generated always as identity,trip_id uuid,truck_id uuid,device_id uuid,
      seq bigint,ts timestamptz,trip_distance_km float8,odometer_km float8,fuel_rate_lph float8,
      trip_fuel_l float8,trip_average_kml float8,raw jsonb,created_at timestamptz default now(),
      sample_kind text default 'OPERATIONAL',cloud_rate_applied boolean not null default false,unique(device_id,seq));
    create table public.telemetry_latest(truck_id uuid primary key,ts timestamptz,data jsonb,updated_at timestamptz);
    grant select,update on public.trips,public.telemetry_samples,public.telemetry_latest to service_role;
    grant insert on public.telemetry_samples to service_role;
    grant select on public.trucks,public.devices,public.events to service_role;
    grant select on public.trips,public.telemetry_samples to authenticated;
    create function public.truckbox_core_gap_recovery(float8,float8,float8,float8,float8,float8) returns float8
    language sql as 'select 0::float8';
  `);
  await db.exec(sql('fuel_window_rollback.sql'));
  await db.exec(sql('fixtures/manual_fuel_trigger_before_v2.sql'));
  await db.exec(`create trigger trg_truckbox_protect_manual_trip_fuel_adjustments before insert or update of
    fuel_liters,average_kml,distance_km,start_core_fuel_liters,end_core_fuel_liters on public.trips
    for each row execute function public.truckbox_protect_manual_trip_fuel_adjustments()`);
  await db.query('insert into public.trucks values ($1,1,null)',[truck]);
  await db.query('insert into public.devices values ($1,$2,$3)',[device,truck,uid]);
  await db.query(`insert into public.trips(id,truck_id,status,started_at,distance_km,fuel_liters,average_kml,
    fuel_source,rate_fuel_liters,rate_last_ts,rate_last_lph,live_baseline_at,live_base_distance_km,live_core_start_odometer_km)
    values ($1,$2,'ACTIVE','2026-11-15',742.16,542.75,742.16/542.75,'SPN183_RATE_CLOUD_V2',542.75,$3,2,
    '2026-11-22 21:22:39.609642+00',742,1205072.442)`,[trip,truck,iso(reference)]);
  await sample(trip, reference-70000, {rate:100,distance:700});
  await sample(trip, reference-60000, {rate:100,distance:701});
  await db.query('insert into public.telemetry_latest values ($1,$2,$3::jsonb,now())',[truck,iso(reference),JSON.stringify({trip_id:trip})]);
  await db.exec(sql('migrations/20261122215843_truckbox_fuel_continuous.sql'));
  approx((await state()).fuel_liters,542.75);
  const wrongClock = window(2,reference,888);
  wrongClock[3]=9999999999999999;
  await sample(trip, reference+12000,{wire:[window(1,reference,18889),wrongClock]});
  let result=await tail();
  approx(result.fuel_liters,542.768889);
  approx(result.distance_km,742.2);
  approx(result.average_kml,742.2/542.768889);
  assert.equal(result.fuel_window_summary.manualLitres,542);
  assert.equal(result.fuel_window_summary.manualKm,742);

  // A new HTTP seq carries a duplicate window after the Link loses its RAM cursor.
  await sample(trip, reference+14000,{wire:[window(1,reference,28889)]});
  approx((await rebuild()).fuel_liters,542.768889);
  // Out-of-order arrival fills a window once. New Core boot is a distinct key.
  await sample(trip, reference+34000,{wire:[window(3,reference,24000,61500)]});
  result=await tail();
  assert.equal(result.fuel_window_summary.quality,'ESTIMATED_GAPS');
  approx(result.fuel_liters,542.8115);
  approx(result.fuel_window_summary.recoveredLitres,0.018611);
  await sample(trip, reference+21000,{wire:[window(2,reference,18611,37500)]});
  result=await rebuild();
  approx(result.fuel_liters,542.8115);
  approx(result.fuel_window_summary.recoveredLitres,0);
  assert.equal(result.fuel_window_summary.quality,'INTEGRATED_PID183');
  await sample(trip, reference+45000,{wire:[window(1,reference+30000,10000)],boot:2});
  approx((await tail()).fuel_liters,542.8215);
  // A concurrent old latest with a higher transport seq cannot move capture time backward.
  await db.query(`update public.telemetry_latest set ts=$1,data=$2::jsonb where truck_id=$3`,
    [iso(reference-20000),JSON.stringify({raw:{source:'REVB1_LINK_LITE'}}),truck]);
  assert.equal(iso(Date.parse((await db.query('select ts from public.telemetry_latest')).rows[0].ts)),iso(reference));

  // Restore consumption from plain legacy firmware beyond the occupied windows.
  await sample(trip, reference+50000,{rate:36});
  await sample(trip, reference+60000,{rate:36});
  approx((await rebuild()).fuel_liters,542.9215);
  await db.query(`update public.trips set status='COMPLETED',fuel_liters=750,distance_km=601 where id=$1`,[trip]);
  approx((await rebuild()).fuel_liters,542.9215);
  assert.ok((await db.query('select count(*) as n from public.telemetry_samples where ts<$1 and cloud_rate_applied',[iso(reference)])).rows[0].n == 0);

  const paired = '30000000-0000-0000-0000-000000000002';
  const start = reference+3600000;
  await db.query(`insert into public.trips(id,truck_id,status,started_at,distance_km) values($1,$2,'ACTIVE',$3,0)`,[paired,truck,iso(start)]);
  for(let slot=1;slot<=31;slot++) {
    const ticks=1000+(slot===31?1:0);
    await sample(paired,start+slot*10000+1000,{wire:[window(slot,start,200000,slot*200000,10000,counter(slot,ticks,slot*200000-2000))],boot:100,distance:30});
  }
  result=await tail(paired);
  approx(result.fuel_liters,6.2);
  assert.equal(result.fuel_window_summary.validatedSpans,0);
  console.log('PID250 unchanged or one tick: 31 windows, 6.200000 L via PID183, no correction');
  // A reference after a counter reset requires a separate stable span.
  for(let slot=32;slot<=62;slot++){
    const ticks=300+Math.round((slot-32)*0.47);
    await sample(paired,start+slot*10000+1000,{wire:[window(slot,start,200000,slot*200000,10000,counter(slot,ticks,slot*200000-2000))],boot:100,distance:60});
  }
  result=await rebuild(paired);
  assert.equal(result.fuel_window_summary.quality,'RECONCILED_PID250');
  assert.equal(result.fuel_window_summary.validatedSpans,1);
  assert.ok(result.fuel_window_summary.reconciliationLitres>0 && result.fuel_window_summary.reconciliationLitres<=0.3+1e-9);
  approx(result.fuel_liters,12.4+result.fuel_window_summary.reconciliationLitres);
  const correction=result.fuel_window_summary.reconciliationLitres;
  console.log(`PID250 stable span: 12.400000 L integrated, ${correction.toFixed(6)} L bounded correction`);
  for(let slot=63;slot<=67;slot++) {
    await sample(paired,start+slot*10000+1000,{wire:[window(slot,start,200000,slot*200000,10000,counter(slot,319,slot*200000-2000,1))],boot:100,distance:62});
  }
  result=await tail(paired);
  assert.equal(result.fuel_window_summary.quality,'INTEGRATED_PID183');
  approx(result.fuel_liters,13.4+correction);
  await db.exec('set role authenticated');
  const scoped=await db.query('select * from public.truckbox_rebuild_trip_fuel_rate($1)',[trip]);
  approx(scoped.rows[0].fuel_liters,542.9215);
  await db.exec('reset role');
  console.log('PostgreSQL migration PASS: 742 km / 542 L + post-anchor volumes, replay, stale/invalid, reboot, legacy, closure and PID250 confidence');
} finally { await db.close(); }
