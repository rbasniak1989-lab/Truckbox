import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {PGlite} from '@electric-sql/pglite';
import {createFieldTestApp,type FieldAction,type FieldTestPort} from './field-test.ts';
import {AccessError,canonicalProof,sha256} from './security.ts';
const ids={truck:'10000000-0000-4000-8000-000000000001',core:'10000000-0000-4000-8000-000000000002',device:'10000000-0000-4000-8000-000000000003',trip:'10000000-0000-4000-8000-000000000004'};
const token='tbxft_'+Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString('hex');
const hash=await sha256(new TextEncoder().encode(token));
const keys=await crypto.subtle.generateKey({name:'RSASSA-PKCS1-v1_5',modulusLength:2048,publicExponent:new Uint8Array([1,0,1]),hash:'SHA-256'},true,['sign','verify']);
const spki=Buffer.from(await crypto.subtle.exportKey('spki',keys.publicKey)).toString('base64');
const migration=await readFile(new URL('../supabase/migrations/20261114221000_multimedia_field_test_v1.sql',import.meta.url),'utf8');
async function fixture() {
 const pg=new PGlite();await pg.exec(`create role anon;create role authenticated;create role service_role;
 create table public.trucks(id uuid primary key,name text);
 create table public.devices(id uuid primary key,truck_id uuid,device_uid text,device_type text,active boolean);
 create table public.trips(id uuid primary key,truck_id uuid,origin text,destination text,weight_t double precision,started_at timestamptz);
 create table public.operation_state(truck_id uuid,mode text,active_trip_id uuid,empty_started_at timestamptz);
 create table public.gps_latest(truck_id uuid,device_id uuid,source text,ts timestamptz,lat double precision,lon double precision,provider text,accuracy_m real);
 create table public.gps_samples(truck_id uuid,device_id uuid,source text,ts timestamptz,lat double precision,lon double precision,provider text,accuracy_m real);`);
 await pg.exec(migration);
 await pg.query("insert into public.trucks values($1,'TEST ONLY')",[ids.truck]);
 await pg.query("insert into public.devices values($1,$2,'test-core','CORE',true)",[ids.core,ids.truck]);
 await pg.query("insert into public.trips values($1,$2,'TEST ORIGIN','TEST DESTINATION',20,now())",[ids.trip,ids.truck]);
 await pg.query("insert into public.operation_state values($1,'LOADED_TRIP',$2,null)",[ids.truck,ids.trip]);
 await pg.query("insert into multimedia_field_test_v1.access(vehicle_id,token_hash,expires_at,active,financial_read,journey_read) values($1,$2,now()+interval '71 hours',true,true,true)",[ids.truck,hash]);
 const db: FieldTestPort={async call(action:FieldAction,h:string,device:string,nonce?:string,key?:string,core?:string,version?:number) {
  try {const r=await pg.query<{v: Record<string,unknown>}>('select public.truckbox_multimedia_field_test_v1($1,$2,$3,$4,$5,$6,$7) v',[action,h,device,nonce??null,key??null,core??null,version??null]);return r.rows[0].v;}
  catch(e) {const m=(e as Error).message;throw new AccessError(m.includes('replayed')||m.includes('policy_changed')?409:m.includes('expired')?401:403,'test_denied');}
 }};
 return {pg,app:createFieldTestApp(db)};
}
async function signed(action='state',body?:string,other: {device?:string;nonce?:string;at?:number;token?:string}={}) {
 const path=action==='activate'?'activate-field-test':action==='ack'?'policy/ack':action;
 const headers=new Headers({authorization:'Bearer '+(other.token??token),'x-truckbox-device':other.device??ids.device,'x-truckbox-time':String(other.at??Date.now()),'x-truckbox-nonce':other.nonce??crypto.randomUUID()});
 const url='https://example.invalid/functions/v1/multimedia-next-v1/v1/'+path;const method=action==='state'?'GET':'POST';
 const req=new Request(url,{method,headers,body});
 headers.set('x-truckbox-signature',Buffer.from(await crypto.subtle.sign('RSASSA-PKCS1-v1_5',keys.privateKey,new TextEncoder().encode(await canonicalProof(req,new TextEncoder().encode(body??''),other.token??token)))).toString('base64'));
 return new Request(url,{method,headers,body});
}
const activate=(f: Awaited<ReturnType<typeof fixture>>,core='test-core')=>signed('activate',JSON.stringify({publicKeySpki:spki,coreDeviceUid:core})).then(r=>f.app(r));
test('field access binds exactly one multimedia/Core and all permissions come from Cloud',async()=>{
 const f=await fixture();try {
  assert.equal((await activate(f)).status,200);
  for(const financial of [false,true])for(const journey of [false,true]) {
   await f.pg.query('update multimedia_field_test_v1.access set financial_read=$1,journey_read=$2,policy_version=policy_version+1',[financial,journey]);
   const r=await f.app(await signed());assert.equal(r.status,200);const b=await r.json();assert.equal(b.trip.origin,'TEST ORIGIN');
   assert.equal(b.policy.financialRead,financial);assert.equal(b.policy.journeyRead,journey);assert.equal(b.financial,null);assert.equal(b.journey,null);
   assert.equal(Date.parse(b.leaseExpiresAt)-Date.parse(b.receivedAt),20000);assert.equal(b.coreDeviceUid,'test-core');
  }
  assert.equal((await f.app(await signed('activate',JSON.stringify({publicKeySpki:spki,coreDeviceUid:'test-core'}),{device:ids.trip}))).status,403);
 } finally {await f.pg.close();}
});
test('wrong Core, missing token, forged signature and old time do not activate the test',async()=>{
 const f=await fixture();try {
  assert.equal((await activate(f,'other-core')).status,403);
  assert.equal((await f.app(new Request('https://example.invalid/v1/state'))).status,401);
  const req=await signed('activate',JSON.stringify({publicKeySpki:spki,coreDeviceUid:'test-core'}));const headers=new Headers(req.headers);headers.set('x-truckbox-signature',Buffer.from(new Uint8Array(256)).toString('base64'));
  assert.equal((await f.app(new Request(req.url,{method:'POST',headers,body:await req.text()}))).status,401);
  assert.equal((await f.app(await signed('activate','{}',{at:Date.now()-120000}))).status,401);
  const r=await f.pg.query<{n:number}>('select count(*)::int n from multimedia_field_test_v1.access where device_id is not null');assert.equal(r.rows[0].n,0);
 } finally {await f.pg.close();}
});
test('nonce replay, policy ACK, logout and revoked Core are checked in the database',async()=>{
 const f=await fixture();try {
  assert.equal((await activate(f)).status,200);const req=await signed();assert.equal((await f.app(req.clone())).status,200);assert.equal((await f.app(req)).status,409);
  assert.equal((await f.app(await signed('ack','{"version":1}'))).status,200);
  assert.equal((await f.app(await signed('ack','{"version":1}'))).status,200);
  assert.equal((await f.app(await signed('ack','{"version":2}'))).status,409);
  await f.pg.query('update public.devices set active=false');assert.equal((await f.app(await signed())).status,403);
  await f.pg.query('update public.devices set active=true');assert.equal((await f.app(await signed('logout'))).status,200);assert.equal((await f.app(await signed())).status,403);
 } finally {await f.pg.close();}
});
test('GPS is exclusively Motorista through Cloud and never replaced by a newer Core/Link fix',async()=>{
 const f=await fixture();try {
  await f.pg.query("insert into public.gps_samples values($1,null,'ANDROID_PHONE',now()-interval '1 hour',-15,-47,'gps',10)",[ids.truck]);
  await f.pg.query("insert into public.gps_latest values($1,$2,'CORE_GPS',now(),10,20,'other',1)",[ids.truck,ids.core]);
  assert.equal((await activate(f)).status,200);const r=await f.app(await signed());const b=await r.json();
  assert.equal(b.gps.source,'ANDROID_PHONE');assert.equal(b.gps.latitude,-15);assert.equal(b.gps.longitude,-47);assert.ok(Date.now()-Date.parse(b.gps.sampledAt)>3500000);
 } finally {await f.pg.close();}
});
test('anonymous database roles cannot call the server RPC and permission edits require a new version',async()=>{
 const f=await fixture();try {
  for(const role of ['anon','authenticated']) {
   await f.pg.exec('set role '+role);await assert.rejects(f.pg.query("select public.truckbox_multimedia_field_test_v1('context',$1,$2)",[hash,ids.device]),/permission denied/);
   await assert.rejects(f.pg.query('select * from multimedia_field_test_v1.access'),/permission denied/);await f.pg.exec('reset role');
  }
  await assert.rejects(f.pg.exec('update multimedia_field_test_v1.access set financial_read=false'),/field_policy_version_required/);
  await assert.rejects(f.pg.exec("update multimedia_field_test_v1.access set expires_at=now()+interval '7 days'"),/field_scope_immutable/);
 } finally {await f.pg.close();}
});
