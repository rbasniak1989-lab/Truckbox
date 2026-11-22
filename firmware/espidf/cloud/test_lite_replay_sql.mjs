import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { PGlite } = require(process.env.TRUCKBOX_TEST_PGLITE || "@electric-sql/pglite");
const db = new PGlite();
try {
  await db.exec(`
    create table public.telemetry_latest(truck_id text primary key, device_id text, seq bigint,
      ts timestamptz, data jsonb, updated_at timestamptz);
    create table public.operation_state(truck_id text primary key, mode text, runtime_counters jsonb,
      last_telemetry_ts timestamptz, last_telemetry_seq bigint, last_health_ts timestamptz);
    create table public.telemetry_samples(device_id text, seq bigint, ts timestamptz,
      primary key(device_id,seq));
    insert into public.telemetry_latest values
      ('truck', 'device', 9, '2026-11-22 06:34:09+00', '{"fuel":42,"distance":59}', now());
    insert into public.operation_state values
      ('truck', 'TRIP', '{"engine":422}', '2026-11-22 06:34:09+00', 9, '2026-11-22 06:34:09+00');
  `);
  const latest = () => db.query("select seq,ts,data,updated_at from public.telemetry_latest").then(r => r.rows[0]);
  const before = await latest();
  await db.exec(readFileSync(new URL("./telemetry_replay_review.sql", import.meta.url), "utf8"));
  await db.exec(`
    -- This replay arrived later and has a higher sequence, but its capture is old.
    insert into public.telemetry_samples values ('device',1000008,'2026-11-22 06:32:39+00')
      on conflict do nothing;
    insert into public.telemetry_samples values ('device',1000008,'2026-11-22 06:32:39+00')
      on conflict do nothing;
    insert into public.telemetry_latest values
      ('truck','device',1000008,'2026-11-22 06:32:39+00','{"fuel":39,"distance":54}', now() + interval '1 second')
      on conflict (truck_id) do update set seq=excluded.seq, ts=excluded.ts,
        data=excluded.data, updated_at=excluded.updated_at;
    update public.operation_state set last_health_ts='2026-11-22 06:32:39+00',
      last_telemetry_ts='2026-11-22 06:32:39+00', last_telemetry_seq=1000008,
      runtime_counters='{"engine":301}' where truck_id='truck';
  `);
  assert.deepEqual(await latest(), before);
  const progress = (await db.query("select runtime_counters,last_telemetry_seq,last_health_ts=last_telemetry_ts as same from public.operation_state")).rows[0];
  assert.equal(Number(progress.last_telemetry_seq), 9);
  assert.equal(progress.same, true);
  assert.deepEqual(progress.runtime_counters, { engine: 422 });
  assert.equal(Number((await db.query("select count(*) as n from public.telemetry_samples")).rows[0].n), 1);
  await db.exec(`
    update public.telemetry_latest set updated_at=now()+interval '2 seconds', seq=9, ts=ts where truck_id='truck';
    update public.telemetry_latest set device_id='other-device', seq=999, ts=ts where truck_id='truck';
    update public.operation_state set last_telemetry_ts=last_telemetry_ts, last_telemetry_seq=8,
      runtime_counters='{"engine":401}' where truck_id='truck';
  `);
  assert.deepEqual(await latest(), before);
  assert.deepEqual((await db.query("select runtime_counters from public.operation_state")).rows[0].runtime_counters,
    { engine: 422 });
  await db.exec(`
    update public.telemetry_latest set ts='2026-11-22 06:36:09+00', seq=1000009, data='{"fuel":43,"distance":61}'
      where truck_id='truck';
    update public.operation_state set last_health_ts='2026-11-22 06:37:09+00' where truck_id='truck';
  `);
  assert.equal(Number((await latest()).seq), 1000009);
  assert.deepEqual((await latest()).data, { fuel: 43, distance: 61 });
  await db.exec(readFileSync(new URL("./telemetry_replay_rollback.sql", import.meta.url), "utf8"));
  await db.exec("update public.telemetry_latest set ts='2026-11-22 06:32:09+00', seq=8 where truck_id='truck'");
  assert.equal(Number((await latest()).seq), 8);
  console.log("PostgreSQL isolado: replay persiste uma vez, painel/runtime/saúde monotônicos, leitura nova e rollback PASS");
} finally {
  await db.close();
}
