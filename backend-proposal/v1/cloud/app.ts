import {createHandler} from '../handler.ts';
import type {Ports,Scope} from '../contracts.ts';
import {CloudRepository} from './repository.ts';
import {AccessError,authenticateDevice,type UserVerifier} from './security.ts';

const json=(status: number,value: unknown)=>new Response(JSON.stringify(value),{status,headers:{'content-type':'application/json','cache-control':'no-store','vary':'Authorization, X-TruckBox-Device'}});
const same=(a: Scope,b: Scope)=>Object.keys(a).every(k=>a[k as keyof Scope]===b[k as keyof Scope]);
const routeOf=(url: URL)=>{
  if(url.pathname.startsWith('/v1/'))return url.pathname;
  for(const prefix of ['/functions/v1/multimedia-next-v1','/multimedia-next-v1']) if(url.pathname.startsWith(prefix+'/v1/'))return url.pathname.slice(prefix.length);
  return url.pathname;
};

/** Aplicação HTTP completa, injetável em Deno/Node. Não é uma função já implantada. */
export function createCloudApp(users: UserVerifier,repository: CloudRepository,now: ()=>Date=()=>new Date()) {
  return async(req: Request): Promise<Response>=>{
    try {
      const url=new URL(req.url);const route=routeOf(url);
      if(!['/v1/enroll','/v1/policy/ack','/v1/state','/v1/policy','/v1/trip','/v1/consumption','/v1/financial','/v1/journey'].includes(route))return json(404,{error:'unknown_contract'});
      const post=route==='/v1/enroll'||route==='/v1/policy/ack';
      if((post&&req.method!=='POST')||(!post&&req.method!=='GET'))return json(405,{error:'method_not_allowed'});
      const {identity,enrolled,body}=await authenticateDevice(req,users,repository,now());
      if(enrolled)return json(202,{contractVersion:1,status:'pending_cloud_approval'});
      const context=await repository.context(identity);
      if(!context||!context.session.monitoringRead)return json(403,{error:'binding_unavailable'});
      const requested=url.searchParams.get('vehicle_id');
      if(requested&&requested!==context.session.scope.vehicleId)return json(403,{error:'scope_denied'});
      if(route==='/v1/policy/ack') {
        let value: unknown;
        try {value=JSON.parse(new TextDecoder().decode(body));}catch {return json(400,{error:'invalid_ack'});}
        if(!value||typeof value!=='object'||!('version' in value)||typeof value.version!=='number'||!Number.isSafeInteger(value.version)||value.version<1)return json(400,{error:'invalid_ack'});
        await repository.acknowledge(context.session.scope,value.version);
        const after=await repository.context(identity);
        if(!after||!same(after.session.scope,context.session.scope)||after.policy.version!==value.version)return json(409,{error:'policy_version_changed'});
        return json(200,{contractVersion:1,version:value.version,status:'applied',requestNonce:req.headers.get('x-truckbox-nonce')});
      }
      const ports: Ports={
        authenticate:async()=> (await repository.context(identity))?.session??null,
        policy:async(scope)=>{const c=await repository.context(identity);return c&&same(c.session.scope,scope)?c.policy:null;},
        trip:scope=>repository.trip(scope),consumption:scope=>repository.consumption(scope),
        financial:scope=>repository.financial(scope),journey:scope=>repository.journey(scope),now,
      };
      const handler=createHandler(ports);
      const internal=(path: string)=>new Request('https://internal.invalid'+path,{headers:new Headers({'x-policy-version':String(context.policy.version)})});
      if(route!=='/v1/state') {
        const headers=new Headers(req.headers);
        return handler(new Request('https://internal.invalid'+route+url.search,{method:'GET',headers}));
      }
      const operationBefore=await repository.operationKey(context.session.scope);
      const p=await handler(internal('/v1/policy'));
      const policy=p.ok?await p.json():null;
      const t=await handler(internal('/v1/trip'));
      const c=await handler(internal('/v1/consumption'));
      const f=policy?.financialRead===true?await handler(internal('/v1/financial')):null;
      const j=policy?.journeyRead===true?await handler(internal('/v1/journey')):null;
      const gps=await repository.gps(context.session.scope);
      const after=await repository.context(identity);
      if(!after||!after.session.monitoringRead||!same(after.session.scope,context.session.scope)||
         JSON.stringify(after.policy)!==JSON.stringify(context.policy))return json(403,{error:'session_or_policy_changed'});
      if(operationBefore!==await repository.operationKey(context.session.scope))return json(409,{error:'operation_changed'});
      // Um erro de autorização durante qualquer leitura impede serializar os objetos já obtidos.
      if([t,c,f,j].some(r=>r?.status===401||r?.status===403||r?.status===409))return json(403,{error:'authorization_changed'});
      const sentAt=now();
      return json(200,{
        contractVersion:1,scope:context.session.scope,vehicleLabel:context.vehicleLabel,coreDeviceUid:context.coreDeviceUid,requestNonce:req.headers.get('x-truckbox-nonce'),
        policy,trip:t.ok?(await t.json()).trip:null,legs:c.ok?(await c.json()).legs:[],
        financial:f?.ok?(await f.json()).financial:null,journey:j?.ok?(await j.json()).journey:null,gps,
        availability:{trip:t.ok?'available':'data_unavailable',consumption:c.ok?'available':'data_unavailable',
          financial:f?(f.ok?'available':'data_unavailable'):'not_authorized',journey:j?(j.ok?'available':'data_unavailable'):'not_authorized',gps:gps?'received_not_physically_verified':'data_unavailable'},
        receivedAt:sentAt.toISOString(),leaseExpiresAt:new Date(sentAt.getTime()+20000).toISOString(),
      });
    } catch(error) {
      if(error instanceof AccessError)return json(error.status,{error:error.code});
      return json(503,{error:'service_unavailable'});
    }
  };
}
