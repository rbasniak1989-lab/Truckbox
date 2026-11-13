import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createHandler} from './handler.ts';
import type {Ports,Policy,Scope} from './contracts.ts';

const scope: Scope={tenantId:'test-tenant',vehicleId:'test-vehicle',multimediaDeviceId:'test-tablet',userId:'test-user',sessionId:'test-session'};
function fixture(f: boolean,j: boolean) {
  let p: Policy|null={scope,version:1,issuedAt:'2026-11-13T11:00:00Z',expiresAt:'2026-11-13T13:00:00Z',financialRead:f,journeyRead:j};
  let financialReads=0,journeyReads=0;
  const ports: Ports={
    now:()=>new Date('2026-11-13T12:00:00Z'),
    authenticate:async req=>req.headers.get('authorization')==='Bearer test-only'?{scope,monitoringRead:true}:null,
    policy:async()=>p,
    financial:async()=>{financialReads++;return {scope,legKind:'LOADED',contractedCents:1400000,incurredCents:300000,projectedCents:500000}},
    journey:async()=>{journeyReads++;return {scope,reference:'2026-11-13T12:00:00Z',drivingSeconds:3600,restConfirmedSeconds:0,waitingSeconds:0,otherStopSeconds:0,unknownSeconds:82800}},
    consumption:async()=>({scope,legs:[{legId:'loaded',kind:'LOADED',distanceKm:100,consumedLiters:60},{legId:'empty',kind:'EMPTY',distanceKm:100,consumedLiters:30}]}),
  };
  const req=(path: string,auth=true)=>new Request('https://homolog.invalid'+path,{headers:{...(auth?{authorization:'Bearer test-only'}:{}),'x-policy-version':'1'}});
  return {ports,req,handler:createHandler(ports),setPolicy:(v: Policy|null)=>{p=v},policy:()=>p,reads:()=>[financialReads,journeyReads]};
}
for(const f of [false,true])for(const j of [false,true])test(`API matrices financial=${f}, journey=${j}`,async()=>{
  const x=fixture(f,j);
  const financial=await x.handler(x.req('/v1/financial')),journey=await x.handler(x.req('/v1/journey'));
  assert.equal(financial.status,f?200:403);assert.equal(journey.status,j?200:403);
  const a=await financial.text(),b=await journey.text();
  assert.equal(a.includes('contractedCents'),f);assert.equal(b.includes('drivingSeconds'),j);
  assert.ok(!a.includes('drivingSeconds'));assert.ok(!b.includes('contractedCents'));
  assert.deepEqual(x.reads(),[Number(f),Number(j)]);
});
test('missing identity and cross-vehicle ID do not read protected stores',async()=>{
  const x=fixture(true,true);
  assert.equal((await x.handler(x.req('/v1/financial',false))).status,401);
  assert.equal((await x.handler(x.req('/v1/journey?vehicle_id=other'))).status,403);
  assert.deepEqual(x.reads(),[0,0]);
});
test('unknown expired or wrong-scope policy denies without query',async()=>{
  for(const transform of [(p: Policy)=>null,(p: Policy)=>({...p,expiresAt:'2026-11-13T12:00:00Z'}),(p: Policy)=>({...p,scope:{...scope,multimediaDeviceId:'other'}})]){
    const x=fixture(true,true);x.setPolicy(transform(x.policy()!));
    assert.equal((await x.handler(x.req('/v1/financial'))).status,403);assert.deepEqual(x.reads(),[0,0]);
  }
});
test('revocation during fetch discards response before serialization',async()=>{
  const x=fixture(true,true);
  x.ports.financial=async()=>{x.setPolicy({...x.policy()!,version:2,financialRead:false});return {scope,legKind:'LOADED',contractedCents:1400000,incurredCents:300000,projectedCents:500000}};
  const r=await x.handler(x.req('/v1/financial'));assert.equal(r.status,403);assert.ok(!(await r.text()).includes('1400000'));
});
test('policy version replay is rejected before reading',async()=>{
  const x=fixture(true,true);x.setPolicy({...x.policy()!,version:2});
  assert.equal((await x.handler(x.req('/v1/financial'))).status,409);assert.deepEqual(x.reads(),[0,0]);
});
test('consumption is computed independently per leg without financial or journey fields',async()=>{
  const x=fixture(false,false),r=await x.handler(x.req('/v1/consumption'));
  assert.equal(r.status,200);const b=await r.json();
  assert.equal(b.legs[0].averageKml,100/60);assert.equal(b.legs[1].averageKml,100/30);
  assert.ok(!JSON.stringify(b).includes('Cents'));assert.ok(!JSON.stringify(b).includes('driving'));
});
test('backend source with a different scope is rejected',async()=>{
  const x=fixture(true,true);x.ports.financial=async()=>({scope:{...scope,userId:'other'},legKind:'LOADED',contractedCents:1,incurredCents:1,projectedCents:1});
  assert.equal((await x.handler(x.req('/v1/financial'))).status,403);
});
test('invalid journey total cannot claim a 24 hour window',async()=>{
  const x=fixture(false,true);x.ports.journey=async()=>({scope,reference:'2026-11-13T12:00:00Z',drivingSeconds:1,restConfirmedSeconds:0,waitingSeconds:0,otherStopSeconds:0,unknownSeconds:0});
  assert.equal((await x.handler(x.req('/v1/journey'))).status,503);
});
test('unavailable adapter returns honest 503 and no fabricated DTO',async()=>{
  const x=fixture(true,true);x.ports.financial=async()=>null;
  assert.equal((await x.handler(x.req('/v1/financial'))).status,503);
});
test('empty leg never returns contracted revenue from a previous freight',async()=>{
  const x=fixture(true,false);
  x.ports.financial=async()=>({scope,legKind:'EMPTY',contractedCents:1400000,incurredCents:300000,projectedCents:500000});
  const r=await x.handler(x.req('/v1/financial')),body=await r.json();
  assert.equal(r.status,200);assert.equal(body.financial.operationKind,'EMPTY');
  assert.ok(!('contractedCents' in body.financial));
});
