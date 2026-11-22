import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,readdir} from 'node:fs/promises';
import {PGlite} from '@electric-sql/pglite';
import {createCloudApp} from './app.ts';
import {CloudRepository,type Database} from './repository.ts';
import {canonicalProof,type UserVerifier} from './security.ts';

// Postgres WASM isolado, dados sintéticos e Auth substituído só neste teste.
// Assinaturas RSA, SQL, constraints, escopos, nonces e resposta HTTP são exercitados de verdade.
const ids={user:'10000000-0000-4000-8000-000000000001',session:'10000000-0000-4000-8000-000000000002',device:'10000000-0000-4000-8000-000000000003',
  tenant:'10000000-0000-4000-8000-000000000004',truck:'10000000-0000-4000-8000-000000000005',trip:'10000000-0000-4000-8000-000000000006',
  otherUser:'20000000-0000-4000-8000-000000000001',otherTenant:'20000000-0000-4000-8000-000000000004',otherTruck:'20000000-0000-4000-8000-000000000005'};
const keys=await crypto.subtle.generateKey({name:'RSASSA-PKCS1-v1_5',modulusLength:2048,publicExponent:new Uint8Array([1,0,1]),hash:'SHA-256'},true,['sign','verify']);
const spki=Buffer.from(await crypto.subtle.exportKey('spki',keys.publicKey)).toString('base64');
const bootstrap=`create role anon;create role authenticated;create schema auth;
  create table auth.users(id uuid primary key);create table auth.sessions(id uuid primary key,user_id uuid not null references auth.users(id),not_after timestamptz);
  create table public.trucks(id uuid primary key,name text);
  create table public.devices(id uuid primary key,truck_id uuid,device_uid text unique,active boolean);
  create table public.gps_latest(truck_id uuid primary key,device_id uuid,source text,ts timestamptz,lat double precision,lon double precision,accuracy_m real,provider text);
  create table public.trips(id uuid primary key,truck_id uuid,origin text,destination text,weight_t double precision,started_at timestamptz,
    freight_total numeric,driving_seconds bigint,stopped_seconds bigint,notes text);
  create table public.operation_state(truck_id uuid primary key,mode text,active_trip_id uuid,empty_started_at timestamptz);`;
const migrationDir=new URL('../supabase/migrations/',import.meta.url);
const migration=(await readdir(migrationDir)).find(x=>x.endsWith('_multimedia_next_v1_access.sql'));
assert.ok(migration);
const schema=await readFile(new URL(migration,migrationDir),'utf8');

async function fixture() {
  const pg=new PGlite();await pg.exec(bootstrap);await pg.exec(schema);
  const wrap=(q: (sql: string,p?: unknown[])=>Promise<{rows: unknown[]}>): Database=>({
    query:async<T>(sql: string,p: unknown[]=[])=> (await q(sql,p)).rows as T[],
    transaction:async<T>(work: (db: Database)=>Promise<T>)=>pg.transaction(async tx=>work(wrap((s,p)=>tx.query(s,p)))),
  });
  const db=wrap((s,p)=>pg.query(s,p));
  await db.query('insert into auth.users values($1),($2)',[ids.user,ids.otherUser]);
  await db.query('insert into auth.sessions(id,user_id) values($1,$2)',[ids.session,ids.user]);
  await db.query("insert into public.trucks values($1,'TEST TRUCK'),($2,'OTHER TENANT')",[ids.truck,ids.otherTruck]);
  await db.query("insert into public.devices values($1,$2,'test-core',true)",[ids.device,ids.truck]);
  await db.query("insert into multimedia_v1.tenants values($1,'TEST ONLY'),($2,'OTHER TEST')",[ids.tenant,ids.otherTenant]);
  await db.query('insert into multimedia_v1.memberships values($1,$2,true),($3,$4,true)',[ids.tenant,ids.user,ids.otherTenant,ids.otherUser]);
  await db.query('insert into multimedia_v1.vehicle_owners values($1,$2),($3,$4)',[ids.truck,ids.tenant,ids.otherTruck,ids.otherTenant]);
  await db.query("insert into multimedia_v1.devices values($1,$2,'TEST DEVICE',now())",[ids.device,spki]);
  await db.query('insert into multimedia_v1.device_requests(device_id,user_id) values($1,$2)',[ids.device,ids.user]);
  await db.query(`insert into multimedia_v1.bindings(tenant_id,vehicle_id,device_id,user_id,active,monitoring_read,expires_at,policy_expires_at,core_device_uid)
    values($1,$2,$3,$4,true,true,now()+interval '2 days',now()+interval '1 day','test-core')`,[ids.tenant,ids.truck,ids.device,ids.user]);
  await db.query("insert into public.trips values($1,$2,'ORIGEM TESTE','DESTINO TESTE',40,now(),987654321,12345,6789,'DO NOT EXPORT THIS NOTE')",[ids.trip,ids.truck]);
  await db.query("insert into public.operation_state values($1,'LOADED_TRIP',$2,null)",[ids.truck,ids.trip]);
  const users: UserVerifier={verify:async(token)=>token==='test-token'?{userId:ids.user,sessionId:ids.session}:null};
  const repository=new CloudRepository(db);
  return {pg,db,repository,app:createCloudApp(users,repository)};
}
async function signed(path='/v1/state',options: {method?:string;body?:string;nonce?:string;at?:number;token?:string;deviceId?:string}={}) {
  const headers=new Headers({authorization:'Bearer '+(options.token??'test-token'),'x-truckbox-device':options.deviceId??ids.device,
    'x-truckbox-time':String(options.at??Date.now()),'x-truckbox-nonce':options.nonce??crypto.randomUUID()});
  const body=options.body??'';const url='https://example.invalid/functions/v1/multimedia-next-v1'+path;
  const request=new Request(url,{method:options.method??'GET',headers,body:body||undefined});
  const proof=await canonicalProof(request,new TextEncoder().encode(body),options.token??'test-token');
  headers.set('x-truckbox-signature',Buffer.from(await crypto.subtle.sign('RSASSA-PKCS1-v1_5',keys.privateKey,new TextEncoder().encode(proof))).toString('base64'));
  return new Request(url,{method:options.method??'GET',headers,body:body||undefined});
}
async function models(f: Awaited<ReturnType<typeof fixture>>) {
  const leg='loaded:'+ids.trip;
  await f.db.query("insert into multimedia_v1.verified_consumption values($1,$2,$3,'LOADED',120,50,'CORE_ACCOUNTING_V2',7,100,true,now())",[ids.tenant,ids.truck,leg]);
  await f.db.query("insert into multimedia_v1.financial_snapshots values($1,$2,$3,'LOADED',100000,20000,40000,true,true,now())",[ids.tenant,ids.truck,leg]);
  await f.db.query('insert into multimedia_v1.journey_windows values($1,$2,$3,now(),10000,20000,30000,4000,22400,true)',[ids.tenant,ids.truck,ids.user]);
}

test('four remote policy combinations project money and journey independently',async()=>{
  const f=await fixture();try {
    await models(f);
    for(const financial of [false,true]) for(const journey of [false,true]) {
      await f.db.query('update multimedia_v1.bindings set financial_read=$1,journey_read=$2,policy_version=policy_version+1',[financial,journey]);
      const r=await f.app(await signed());assert.equal(r.status,200);const body=await r.json();
      assert.equal(body.policy.financialRead,financial);assert.equal(body.policy.journeyRead,journey);
      assert.equal(body.financial!==null,financial);assert.equal(body.journey!==null,journey);
      assert.equal(body.trip.origin,'ORIGEM TESTE');assert.equal(body.legs[0].averageKml,2.4);
      const text=JSON.stringify(body);assert.ok(!text.includes('987654321'));assert.ok(!text.includes('DO NOT EXPORT'));assert.ok(!text.includes('driving_seconds'));
      if(!financial)assert.ok(!text.includes('contractedCents'));
      if(!journey)assert.ok(!text.includes('drivingSeconds'));
      assert.equal(r.headers.get('cache-control'),'no-store');
    }
  } finally {await f.pg.close();}
});
test('trip metadata remains available when verified counters and optional models do not exist',async()=>{
  const f=await fixture();try {
    const r=await f.app(await signed());assert.equal(r.status,200);const body=await r.json();
    assert.equal(body.trip.id,ids.trip);assert.equal(body.trip.consumption,null);assert.deepEqual(body.legs,[]);
    assert.equal(body.financial,null);assert.equal(body.journey,null);assert.equal(body.availability.consumption,'data_unavailable');
  } finally {await f.pg.close();}
});
test('a scoped active trip projects consolidated fuel without exposing its checkpoint or history',async()=>{
  const f=await fixture();try {
    await f.pg.exec(`alter table public.trips add column status text, add column fuel_source text,
                    add column distance_km float8, add column fuel_liters float8, add column fuel_window_summary jsonb`);
    const meter=JSON.stringify({quality:'RECONCILED_PID250',sampledAt:new Date().toISOString(),manualLitres:542,checkpoint:'PRIVATE'});
    await f.db.query(`update public.trips set status='ACTIVE',fuel_source='J1587_PID183_WINDOW_CORE_V2',
                      distance_km=742.2,fuel_liters=542.75,fuel_window_summary=$2::jsonb where id=$1`,[ids.trip,meter]);
    const r=await f.app(await signed());assert.equal(r.status,200);const body=await r.json();
    assert.equal(body.trip.consumption.averageKml,742.2/542.75);
    assert.equal(body.legs[0].fuel.quality,'RECONCILED_PID250');
    assert.ok(!JSON.stringify(body).includes('PRIVATE'));
  } finally {await f.pg.close();}
});
test('GPS comes only from the authenticated vehicle Cloud record and keeps its real time and source',async()=>{
  const f=await fixture();try {
    await f.db.query("insert into public.gps_latest values($1,$2,'ANDROID_PHONE',now()-interval '1 hour',-15.0,-47.0,12.5,'TEST_PROVIDER')",[ids.truck,ids.device]);
    await f.db.query("insert into public.gps_latest values($1,null,'OTHER',now(),60.0,120.0,null,null)",[ids.otherTruck]);
    const r=await f.app(await signed());assert.equal(r.status,200);const body=await r.json();
    assert.equal(body.gps.latitude,-15);assert.equal(body.gps.longitude,-47);assert.equal(body.gps.source,'ANDROID_PHONE');
    assert.ok(Date.now()-Date.parse(body.gps.sampledAt)>3500000);assert.equal(body.gps.verification,'RECEIVED_NOT_PHYSICALLY_VERIFIED');
    assert.equal(Date.parse(body.leaseExpiresAt)-Date.parse(body.receivedAt),20000);
    await f.db.query('update public.gps_latest set lat=999 where truck_id=$1',[ids.truck]);
    assert.equal((await(await f.app(await signed())).json()).gps,null);
    await f.db.query('update multimedia_v1.bindings set monitoring_read=false,policy_version=policy_version+1');
    const denied=await f.app(await signed());assert.equal(denied.status,403);assert.ok(!(await denied.text()).includes('latitude'));
  } finally {await f.pg.close();}
});
test('requests without authentication never receive data',async()=>{
  const f=await fixture();try {const r=await f.app(new Request('https://example.invalid/v1/state'));assert.equal(r.status,401);assert.deepEqual(await r.json(),{error:'authentication_required'});}finally{await f.pg.close();}
});
test('signature binds exact body, route, bearer and time',async()=>{
  const f=await fixture();try {
    const original=await signed();const headers=new Headers(original.headers);
    assert.equal((await f.app(new Request(original.url.replace('/state','/financial'),{headers}))).status,401);
    headers.set('authorization','Bearer other-token');assert.equal((await f.app(new Request(original.url,{headers}))).status,401);
    assert.equal((await f.app(await signed('/v1/state',{at:Date.now()-120000}))).status,401);
  } finally {await f.pg.close();}
});
test('same signed request cannot be replayed',async()=>{
  const f=await fixture();try {const r=await signed();assert.equal((await f.app(r.clone())).status,200);const repeat=await f.app(r);assert.equal(repeat.status,409);assert.deepEqual(await repeat.json(),{error:'replayed_request'});}finally{await f.pg.close();}
});
test('device, session and membership revocations take effect on next request',async()=>{
  for(const sql of ["update multimedia_v1.bindings set active=false,policy_version=policy_version+1",'delete from auth.sessions','update multimedia_v1.memberships set active=false']) {
    const f=await fixture();try {await f.db.query(sql);const r=await f.app(await signed());assert.equal(r.status,sql.includes('auth.sessions')?401:403);assert.ok(!(await r.text()).includes('ORIGEM'));}finally{await f.pg.close();}
  }
});
test('a client supplied vehicle cannot widen the authenticated scope',async()=>{
  const f=await fixture();try {const r=await f.app(await signed('/v1/state?vehicle_id='+ids.otherTruck));assert.equal(r.status,403);assert.deepEqual(await r.json(),{error:'scope_denied'});}finally{await f.pg.close();}
});
test('policy changes require a new version and preserve an audit trail',async()=>{
  const f=await fixture();try {
    await assert.rejects(f.db.query('update multimedia_v1.bindings set financial_read=true'),/policy_version_must_advance/);
    await f.db.query('update multimedia_v1.bindings set financial_read=true,policy_version=policy_version+1');
    const audit=await f.db.query<{new_version:number|string}>('select new_version from multimedia_v1.policy_audit order by id');
    assert.deepEqual(audit.map(x=>Number(x.new_version)),[1,2]);
    await assert.rejects(f.db.query('update multimedia_v1.bindings set policy_version=1'),/policy_version_rollback/);
  } finally {await f.pg.close();}
});
test('anonymous and authenticated database roles cannot read the private schema',async()=>{
  const f=await fixture();try {
    const rows=await f.db.query<{n:number|string}>("select count(*) n from pg_tables where schemaname='multimedia_v1' and rowsecurity");assert.equal(Number(rows[0].n),12);
    for(const role of ['anon','authenticated']) {
      await f.db.query('set role '+role);
      await assert.rejects(f.db.query('select * from multimedia_v1.bindings'),/permission denied/);
      await f.db.query('reset role');
    }
  } finally {await f.pg.close();}
});
test('empty movement does not inherit the previous freight or loaded consumption',async()=>{
  const f=await fixture();try {
    await models(f);await f.db.query('update multimedia_v1.bindings set financial_read=true,policy_version=policy_version+1');
    await f.db.query("update public.operation_state set mode='EMPTY_MODE',active_trip_id=null,empty_started_at=now()");
    const r=await f.app(await signed());assert.equal(r.status,200);const body=await r.json();
    assert.equal(body.trip.kind,'EMPTY');assert.equal(body.trip.origin,null);assert.equal(body.trip.weightT,null);assert.equal(body.trip.consumption,null);
    assert.equal(body.financial,null);assert.deepEqual(body.legs,[]);assert.ok(!JSON.stringify(body).includes(ids.trip));
  } finally {await f.pg.close();}
});
test('enrollment proves key possession and creates no permission or new binding',async()=>{
  const f=await fixture();try {
    await f.db.query('update multimedia_v1.bindings set active=false,policy_version=policy_version+1');
    const body=JSON.stringify({publicKeySpki:spki,label:'TEST DEVICE'});
    const r=await f.app(await signed('/v1/enroll',{method:'POST',body}));assert.equal(r.status,202);
    assert.equal((await r.json()).status,'pending_cloud_approval');
    assert.equal((await f.app(await signed())).status,403);
    const n=await f.db.query<{n:number|string}>('select count(*) n from multimedia_v1.bindings where active');assert.equal(Number(n[0].n),0);
  } finally {await f.pg.close();}
});
test('a new device creates only a pending request, and a modified enrollment body fails proof',async()=>{
  const f=await fixture();try {
    const deviceId='30000000-0000-4000-8000-000000000003';
    const body=JSON.stringify({publicKeySpki:spki,label:'NEW TEST DEVICE'});
    const req=await signed('/v1/enroll',{method:'POST',body,deviceId});
    const tampered=new Request(req.url,{method:'POST',headers:req.headers,body:body.replace('NEW TEST','CHANGED TEST')});
    assert.equal((await f.app(tampered)).status,401);
    assert.equal((await f.app(req)).status,202);
    assert.equal((await f.app(await signed('/v1/state',{deviceId}))).status,403);
    const n=await f.db.query<{n:number|string}>('select count(*) n from multimedia_v1.device_requests where device_id=$1',[deviceId]);assert.equal(Number(n[0].n),1);
  } finally {await f.pg.close();}
});
test('policy ACK is scoped, versioned and idempotent under a fresh nonce',async()=>{
  const f=await fixture();try {
    for(let i=0;i<2;i++) {
      const req=await signed('/v1/policy/ack',{method:'POST',body:JSON.stringify({version:1})});
      const nonce=req.headers.get('x-truckbox-nonce');
      const r=await f.app(req);assert.equal(r.status,200);const value=await r.json();assert.equal(value.status,'applied');assert.equal(value.requestNonce,nonce);
    }
    const rows=await f.db.query<{n:number|string}>('select count(*) n from multimedia_v1.policy_acks');assert.equal(Number(rows[0].n),1);
    await f.db.query('update multimedia_v1.bindings set policy_version=policy_version+1,financial_read=true');
    assert.equal((await f.app(await signed('/v1/policy/ack',{method:'POST',body:JSON.stringify({version:1})}))).status,409);
  } finally {await f.pg.close();}
});
test('a revoked Core record does not get a token-hash bypass through the new Cloud API',async()=>{
  const f=await fixture();try {await f.db.query('update public.devices set active=false');assert.equal((await f.app(await signed())).status,403);}finally{await f.pg.close();}
});
