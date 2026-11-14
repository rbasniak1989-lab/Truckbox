import type {ConsumptionSource,FinancialSource,GpsData,JourneySource,Policy,Scope,Session,TripData,TripSource} from '../contracts.ts';
import {AccessError,type DeviceIdentity,type DeviceRegistry,type UserIdentity} from './security.ts';
export interface Database {
  query<T=Record<string,unknown>>(text: string,params?: unknown[]): Promise<T[]>;
  transaction<T>(work: (db: Database)=>Promise<T>): Promise<T>;
}
type Binding={tenant_id: string; vehicle_id: string; device_id: string; user_id: string; monitoring_read: boolean; policy_version: string | number; financial_read: boolean; journey_read: boolean; policy_issued_at: Date | string; policy_expires_at: Date | string; core_device_uid: string | null; vehicle_label: string};
const iso=(v: Date|string)=>new Date(v).toISOString();
const scopeEqual=(a: Scope,b: Scope)=>Object.keys(a).every(k=>a[k as keyof Scope]===b[k as keyof Scope]);

/** Único adaptador ao banco. Todas as consultas usam IDs do vínculo autenticado, nunca da query Android. */
export class CloudRepository implements DeviceRegistry {
  private readonly db: Database;
  constructor(db: Database) {this.db=db;}
  async sessionActive(user: UserIdentity): Promise<boolean> {
    const rows=await this.db.query('select id from auth.sessions where id=$1 and user_id=$2 and (not_after is null or not_after>now())',[user.sessionId,user.userId]);
    return rows.length===1;
  }
  async publicKey(deviceId: string): Promise<string | null> {
    const rows=await this.db.query<{public_key_spki: string}>('select public_key_spki from multimedia_v1.devices where id=$1',[deviceId]);
    return rows[0]?.public_key_spki??null;
  }
  async enroll(user: UserIdentity,deviceId: string,spki: string,label: string): Promise<void> {
    await this.db.transaction(async db=>{
      const sessions=await db.query('select id from auth.sessions where id=$1 and user_id=$2 and (not_after is null or not_after>now())',[user.sessionId,user.userId]);
      if(sessions.length!==1) throw new AccessError(401,'authentication_required');
      await db.query('select pg_advisory_xact_lock(hashtextextended($1,0))',[user.userId]);
      const existing=await db.query('select 1 from multimedia_v1.device_requests where user_id=$1 and device_id=$2',[user.userId,deviceId]);
      const count=await db.query<{n: string|number}>("select count(*) as n from multimedia_v1.device_requests where user_id=$1 and requested_at>now()-interval '1 day'",[user.userId]);
      if(!existing.length&&Number(count[0]?.n)>=10) throw new AccessError(429,'enrollment_limit');
      await db.query('insert into multimedia_v1.devices(id,public_key_spki,label) values($1,$2,$3) on conflict(id) do nothing',[deviceId,spki,label]);
      const key=await db.query<{public_key_spki: string}>('select public_key_spki from multimedia_v1.devices where id=$1 for update',[deviceId]);
      if(key[0]?.public_key_spki!==spki) throw new AccessError(409,'device_key_conflict');
      await db.query('insert into multimedia_v1.device_requests(device_id,user_id) values($1,$2) on conflict do nothing',[deviceId,user.userId]);
    });
  }
  async claimNonce(deviceId: string,nonce: string,expiresAt: Date): Promise<boolean> {
    return this.db.transaction(async db=>{
      await db.query('delete from multimedia_v1.request_nonces where device_id=$1 and expires_at<now()',[deviceId]);
      const rows=await db.query('insert into multimedia_v1.request_nonces(device_id,nonce,expires_at) values($1,$2,$3) on conflict do nothing returning nonce',[deviceId,nonce,expiresAt.toISOString()]);
      return rows.length===1;
    });
  }
  private async binding(identity: DeviceIdentity): Promise<Binding | null> {
    const rows=await this.db.query<Binding>(`select b.tenant_id,b.vehicle_id,b.device_id,b.user_id,b.monitoring_read,b.policy_version,
      b.financial_read,b.journey_read,b.policy_issued_at,b.policy_expires_at,b.core_device_uid,t.name as vehicle_label
      from multimedia_v1.bindings b
      join multimedia_v1.memberships m on m.tenant_id=b.tenant_id and m.user_id=b.user_id and m.active
      join multimedia_v1.vehicle_owners o on o.tenant_id=b.tenant_id and o.vehicle_id=b.vehicle_id
      join public.trucks t on t.id=b.vehicle_id
      join public.devices core on core.truck_id=b.vehicle_id and core.device_uid=b.core_device_uid and core.active
      join auth.sessions s on s.user_id=b.user_id and s.id=$3 and (s.not_after is null or s.not_after>now())
      where b.user_id=$1 and b.device_id=$2 and b.active and b.expires_at>now()`,[identity.userId,identity.deviceId,identity.sessionId]);
    return rows.length===1?rows[0]:null;
  }
  async context(identity: DeviceIdentity): Promise<{session: Session;policy: Policy;vehicleLabel: string;coreDeviceUid: string|null}|null> {
    const b=await this.binding(identity);if(!b)return null;
    const scope: Scope={tenantId:b.tenant_id,vehicleId:b.vehicle_id,multimediaDeviceId:b.device_id,userId:b.user_id,sessionId:identity.sessionId};
    return {session:{scope,monitoringRead:b.monitoring_read},policy:{scope,version:Number(b.policy_version),issuedAt:iso(b.policy_issued_at),expiresAt:iso(b.policy_expires_at),financialRead:b.financial_read,journeyRead:b.journey_read},vehicleLabel:b.vehicle_label,coreDeviceUid:b.core_device_uid};
  }
  async authorizeScope(scope: Scope): Promise<boolean> {
    const c=await this.context({userId:scope.userId,sessionId:scope.sessionId,deviceId:scope.multimediaDeviceId});
    return !!c&&c.session.monitoringRead&&scopeEqual(c.session.scope,scope);
  }
  async acknowledge(scope: Scope,version: number): Promise<void> {
    const c=await this.context({userId:scope.userId,sessionId:scope.sessionId,deviceId:scope.multimediaDeviceId});
    if(!c||!scopeEqual(c.session.scope,scope)||c.policy.version!==version||Date.parse(c.policy.issuedAt)>Date.now()||Date.parse(c.policy.expiresAt)<=Date.now())throw new AccessError(409,'policy_version_changed');
    await this.db.query(`insert into multimedia_v1.policy_acks(tenant_id,vehicle_id,device_id,user_id,session_id,policy_version)
      select tenant_id,vehicle_id,device_id,user_id,$5,$6 from multimedia_v1.bindings
      where tenant_id=$1 and vehicle_id=$2 and device_id=$3 and user_id=$4 and active and monitoring_read and policy_version=$6
        and expires_at>now() and policy_issued_at<=now() and policy_expires_at>now()
      on conflict do nothing`,[scope.tenantId,scope.vehicleId,scope.multimediaDeviceId,scope.userId,scope.sessionId,version]);
    const recorded=await this.db.query('select 1 from multimedia_v1.policy_acks where tenant_id=$1 and vehicle_id=$2 and device_id=$3 and user_id=$4 and session_id=$5 and policy_version=$6',
      [scope.tenantId,scope.vehicleId,scope.multimediaDeviceId,scope.userId,scope.sessionId,version]);
    if(recorded.length!==1)throw new AccessError(409,'policy_version_changed');
  }
  async operationKey(scope: Scope): Promise<string|null> {return (await this.operation(scope))?.legId??null;}
  async gps(scope: Scope): Promise<GpsData|null> {
    if(!await this.authorizeScope(scope))return null;
    const rows=await this.db.query<{lat:number;lon:number;ts:Date|string;source:string;provider:string|null;accuracy_m:number|null}>(`select g.lat,g.lon,g.ts,g.source,g.provider,g.accuracy_m
      from public.gps_latest g left join public.devices d on d.id=g.device_id and d.truck_id=g.truck_id and d.active
      where g.truck_id=$1 and g.source='ANDROID_PHONE' and (g.device_id is null or d.id is not null)`,[scope.vehicleId]);
    const r=rows[0];if(!r)return null;
    if(!Number.isFinite(r.lat)||!Number.isFinite(r.lon)||r.lat< -90||r.lat>90||r.lon< -180||r.lon>180||
       !Number.isFinite(new Date(r.ts).getTime())||new Date(r.ts).getTime()>Date.now()+60000||
       typeof r.source!=='string'||r.source.length<1||r.source.length>80||
       (r.provider!==null&&r.provider.length>80)||(r.accuracy_m!==null&&(!Number.isFinite(r.accuracy_m)||r.accuracy_m<0)))return null;
    return {latitude:r.lat,longitude:r.lon,sampledAt:iso(r.ts),source:r.source,provider:r.provider,accuracyM:r.accuracy_m,verification:'RECEIVED_NOT_PHYSICALLY_VERIFIED'};
  }
  private async operation(scope: Scope): Promise<{kind: 'LOADED'|'EMPTY';legId: string;trip: TripData}|null> {
    if(!await this.authorizeScope(scope))return null;
    const rows=await this.db.query<{mode: string;active_trip_id: string|null;empty_started_at: Date|string|null;id: string|null;origin: string|null;destination: string|null;weight_t: number|null;started_at: Date|string|null}>(`select o.mode,o.active_trip_id,o.empty_started_at,t.id,t.origin,t.destination,t.weight_t,t.started_at
      from public.operation_state o left join public.trips t on t.id=o.active_trip_id and t.truck_id=o.truck_id
      where o.truck_id=$1`,[scope.vehicleId]);
    const r=rows[0];if(!r)return null;
    if(r.mode==='EMPTY_MODE'&&r.empty_started_at) {
      const legId='empty:'+new Date(r.empty_started_at).getTime();
      return {kind:'EMPTY',legId,trip:{id:legId,legId,kind:'EMPTY',origin:null,destination:null,weightT:null,legStartedAt:iso(r.empty_started_at),consumption:null}};
    }
    if(r.mode!=='LOADED_TRIP'||!r.id)return null;
    const legId='loaded:'+r.id;
    return {kind:'LOADED',legId,trip:{id:r.id,legId,kind:'LOADED',origin:r.origin,destination:r.destination,weightT:r.weight_t,legStartedAt:r.started_at?iso(r.started_at):null,consumption:null}};
  }
  async consumption(scope: Scope): Promise<ConsumptionSource|null> {
    const op=await this.operation(scope);if(!op)return null;
    const rows=await this.db.query<{leg_id: string;kind:'LOADED'|'EMPTY';distance_km:number;consumed_liters:number}>(`select leg_id,kind,distance_km,consumed_liters from multimedia_v1.verified_consumption
      where tenant_id=$1 and vehicle_id=$2 and leg_id=$3 and kind=$4 and source_verified`,[scope.tenantId,scope.vehicleId,op.legId,op.kind]);
    if(!rows.length)return null;
    return {scope,legs:rows.map(x=>({legId:x.leg_id,kind:x.kind,distanceKm:x.distance_km,consumedLiters:x.consumed_liters}))};
  }
  async trip(scope: Scope): Promise<TripSource|null> {
    if(!await this.authorizeScope(scope))return null;
    const op=await this.operation(scope);
    if(!op)return {scope,trip:null};
    const c=await this.consumption(scope);
    return {scope,trip:{...op.trip,consumption:c?.legs.find(x=>x.legId===op.legId)??null}};
  }
  async financial(scope: Scope): Promise<FinancialSource|null> {
    const context=await this.context({userId:scope.userId,sessionId:scope.sessionId,deviceId:scope.multimediaDeviceId});
    if(!context||!scopeEqual(scope,context.session.scope)||!context.session.monitoringRead||!context.policy.financialRead||Date.parse(context.policy.issuedAt)>Date.now()||Date.parse(context.policy.expiresAt)<=Date.now())return null;
    const op=await this.operation(scope);if(!op)return null;
    const rows=await this.db.query<{kind:'LOADED'|'EMPTY';contracted_cents:string|number|null;incurred_cents:string|number;projected_cents:string|number}>(`select kind,contracted_cents,incurred_cents,projected_cents from multimedia_v1.financial_snapshots
      where tenant_id=$1 and vehicle_id=$2 and leg_id=$3 and kind=$4 and source_verified and owner_costs_confirmed
      and exists(select 1 from multimedia_v1.bindings b where b.tenant_id=$1 and b.vehicle_id=$2 and b.device_id=$5 and b.user_id=$6
        and b.active and b.monitoring_read and b.financial_read and b.expires_at>now() and b.policy_issued_at<=now() and b.policy_expires_at>now())`,[scope.tenantId,scope.vehicleId,op.legId,op.kind,scope.multimediaDeviceId,scope.userId]);
    const r=rows[0];return r?{scope,legKind:r.kind,contractedCents:r.contracted_cents===null?null:Number(r.contracted_cents),incurredCents:Number(r.incurred_cents),projectedCents:Number(r.projected_cents)}:null;
  }
  async journey(scope: Scope): Promise<JourneySource|null> {
    const context=await this.context({userId:scope.userId,sessionId:scope.sessionId,deviceId:scope.multimediaDeviceId});
    if(!context||!scopeEqual(scope,context.session.scope)||!context.session.monitoringRead||!context.policy.journeyRead||Date.parse(context.policy.issuedAt)>Date.now()||Date.parse(context.policy.expiresAt)<=Date.now())return null;
    const rows=await this.db.query<{reference:Date|string;driving_seconds:number;rest_confirmed_seconds:number;waiting_seconds:number;other_stop_seconds:number;unknown_seconds:number}>(`select reference,driving_seconds,rest_confirmed_seconds,waiting_seconds,other_stop_seconds,unknown_seconds from multimedia_v1.journey_windows
      where tenant_id=$1 and vehicle_id=$2 and user_id=$3 and source_verified and reference<=now() and reference>now()-interval '1 minute'
      and exists(select 1 from multimedia_v1.bindings b where b.tenant_id=$1 and b.vehicle_id=$2 and b.user_id=$3 and b.device_id=$4
        and b.active and b.monitoring_read and b.journey_read and b.expires_at>now() and b.policy_issued_at<=now() and b.policy_expires_at>now())
      order by reference desc limit 1`,[scope.tenantId,scope.vehicleId,scope.userId,scope.multimediaDeviceId]);
    const r=rows[0];return r?{scope,reference:iso(r.reference),drivingSeconds:r.driving_seconds,restConfirmedSeconds:r.rest_confirmed_seconds,waitingSeconds:r.waiting_seconds,otherStopSeconds:r.other_stop_seconds,unknownSeconds:r.unknown_seconds}:null;
  }
}
