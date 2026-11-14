-- Canal temporário autorizado pelo proprietário em 14/11/2026.
-- Adições privadas; não altera dados, APIs ou permissões dos apps existentes.
begin;
create schema multimedia_field_test_v1;
revoke all on schema multimedia_field_test_v1 from public,anon,authenticated;
create table multimedia_field_test_v1.access (
  id uuid primary key default gen_random_uuid(),
  tenant_id uuid not null default gen_random_uuid(),
  principal_id uuid not null default gen_random_uuid(),
  session_id uuid not null default gen_random_uuid(),
  vehicle_id uuid not null references public.trucks(id),
  token_hash text not null unique check(token_hash~'^[0-9a-f]{64}$'),
  created_at timestamptz not null default now(),
  expires_at timestamptz not null check(expires_at>created_at and expires_at<=created_at+interval '72 hours'),
  active boolean not null default false,
  financial_read boolean not null default false,
  journey_read boolean not null default false,
  policy_version bigint not null default 1 check(policy_version between 1 and 9007199254740991),
  device_id uuid,
  public_key_spki text,
  core_device_uid text,
  activated_at timestamptz,
  ack_version bigint,
  ack_at timestamptz,
  check((device_id is null and public_key_spki is null and core_device_uid is null) or
    (device_id is not null and length(public_key_spki) between 256 and 2048 and length(core_device_uid) between 1 and 128))
);
create table multimedia_field_test_v1.nonces (
  access_id uuid not null references multimedia_field_test_v1.access(id),nonce uuid not null,expires_at timestamptz not null,
  primary key(access_id,nonce)
);
create index field_nonces_expiry on multimedia_field_test_v1.nonces(access_id,expires_at);
alter table multimedia_field_test_v1.access enable row level security;
alter table multimedia_field_test_v1.nonces enable row level security;
revoke all on all tables in schema multimedia_field_test_v1 from public,anon,authenticated;

create function multimedia_field_test_v1.guard_access() returns trigger language plpgsql set search_path=pg_catalog as $$
begin
  if row(new.id,new.tenant_id,new.principal_id,new.session_id,new.vehicle_id,new.token_hash,new.created_at,new.expires_at)
    is distinct from row(old.id,old.tenant_id,old.principal_id,old.session_id,old.vehicle_id,old.token_hash,old.created_at,old.expires_at)
    then raise exception 'field_scope_immutable'; end if;
  if old.device_id is not null and row(new.device_id,new.public_key_spki,new.core_device_uid)
    is distinct from row(old.device_id,old.public_key_spki,old.core_device_uid) then raise exception 'field_device_pinned'; end if;
  if row(new.active,new.financial_read,new.journey_read) is distinct from row(old.active,old.financial_read,old.journey_read)
    and new.policy_version<>old.policy_version+1 then raise exception 'field_policy_version_required'; end if;
  if new.policy_version<old.policy_version then raise exception 'field_policy_rollback'; end if;
  return new;
end $$;
revoke all on function multimedia_field_test_v1.guard_access() from public,anon,authenticated;
create trigger field_guard before update on multimedia_field_test_v1.access for each row execute function multimedia_field_test_v1.guard_access();

-- Ponte exclusivamente servidor: sem EXECUTE anon/authenticated/PUBLIC.
-- A Edge verifica RSA antes de solicitar uma ação mutável. O hash nunca sai para Android.
create function public.truckbox_multimedia_field_test_v1(
  p_action text,p_token_hash text,p_device_id uuid,p_nonce uuid default null,
  p_key text default null,p_core_uid text default null,p_version bigint default null
) returns jsonb language plpgsql security definer set search_path=pg_catalog as $$
declare
  a multimedia_field_test_v1.access%rowtype;
  v_now timestamptz=clock_timestamp();
  v_scope jsonb;v_policy jsonb;v_trip jsonb;v_gps jsonb;v_label text;v_nonce uuid;
  op record;g record;
begin
  select * into a from multimedia_field_test_v1.access where token_hash=p_token_hash for update;
  if not found or not a.active then raise exception 'field_access_denied'; end if;
  if a.expires_at<=v_now then raise exception 'field_access_expired'; end if;
  if a.device_id is not null and a.device_id<>p_device_id then raise exception 'field_access_denied'; end if;
  if a.core_device_uid is not null and not exists(select 1 from public.devices d where d.truck_id=a.vehicle_id
    and d.device_uid=a.core_device_uid and d.device_type='CORE' and d.active) then raise exception 'field_access_denied'; end if;
  if p_action='context' then
    return jsonb_build_object('publicKeySpki',a.public_key_spki,'expiresAt',a.expires_at,
      'userId',a.principal_id,'sessionId',a.session_id);
  end if;
  if p_action not in ('activate','enroll','state','ack','logout') or p_nonce is null then raise exception 'field_action_denied'; end if;
  if p_action='activate' then
    if p_key is null or length(p_key) not between 256 and 2048 or not exists(select 1 from public.devices d
      where d.truck_id=a.vehicle_id and d.device_uid=p_core_uid and d.device_type='CORE' and d.active)
      then raise exception 'field_access_denied'; end if;
    if a.device_id is null then
      update multimedia_field_test_v1.access set device_id=p_device_id,public_key_spki=p_key,core_device_uid=p_core_uid,activated_at=v_now where id=a.id;
      a.device_id=p_device_id;a.public_key_spki=p_key;a.core_device_uid=p_core_uid;
    elsif a.public_key_spki<>p_key or a.core_device_uid<>p_core_uid then raise exception 'field_access_denied'; end if;
  elsif a.device_id is null then raise exception 'field_access_denied'; end if;
  delete from multimedia_field_test_v1.nonces where access_id=a.id and expires_at<v_now;
  insert into multimedia_field_test_v1.nonces values(a.id,p_nonce,v_now+interval '2 minutes') on conflict do nothing returning nonce into v_nonce;
  if v_nonce is null then raise exception 'field_request_replayed'; end if;
  if p_action='activate' then return jsonb_build_object('contractVersion',1,'userId',a.principal_id,'sessionId',a.session_id,
    'expiresAt',a.expires_at,'requestNonce',p_nonce); end if;
  if p_action='enroll' then return jsonb_build_object('contractVersion',1,'status','test_device_bound','requestNonce',p_nonce); end if;
  if p_action='ack' then
    if p_version is distinct from a.policy_version then raise exception 'field_policy_changed'; end if;
    update multimedia_field_test_v1.access set ack_version=p_version,ack_at=v_now where id=a.id;
    return jsonb_build_object('contractVersion',1,'version',p_version,'status','applied','requestNonce',p_nonce);
  end if;
  if p_action='logout' then
    update multimedia_field_test_v1.access set active=false,policy_version=policy_version+1 where id=a.id;
    return jsonb_build_object('contractVersion',1,'status','revoked','requestNonce',p_nonce);
  end if;
  select name into v_label from public.trucks where id=a.vehicle_id;
  select o.mode,o.empty_started_at,t.id,t.origin,t.destination,t.weight_t,t.started_at into op from public.operation_state o
    left join public.trips t on t.id=o.active_trip_id and t.truck_id=o.truck_id where o.truck_id=a.vehicle_id;
  if found and op.mode='LOADED_TRIP' and op.id is not null then
    v_trip=jsonb_build_object('id',op.id,'legId','loaded:'||op.id,'kind','LOADED','origin',op.origin,'destination',op.destination,
      'weightT',op.weight_t,'legStartedAt',op.started_at,'consumption',null);
  elsif found and op.mode='EMPTY_MODE' and op.empty_started_at is not null then
    v_trip=jsonb_build_object('id','empty:'||floor(extract(epoch from op.empty_started_at)*1000)::text,
      'legId','empty:'||floor(extract(epoch from op.empty_started_at)*1000)::text,'kind','EMPTY','origin',null,'destination',null,
      'weightT',null,'legStartedAt',op.empty_started_at,'consumption',null);
  end if;
  -- GPS exclusivamente TruckBox Motorista; nunca substituir por Core/Link.
  select x.* into g from (
    select l.lat,l.lon,l.ts,l.source,l.provider,l.accuracy_m,l.device_id from public.gps_latest l
      where l.truck_id=a.vehicle_id and l.source='ANDROID_PHONE'
    union all
    select s.lat,s.lon,s.ts,s.source,s.provider,s.accuracy_m,s.device_id from public.gps_samples s
      where s.truck_id=a.vehicle_id and s.source='ANDROID_PHONE'
  ) x where x.lat between -90 and 90 and x.lon between -180 and 180 and x.ts<=v_now+interval '1 minute'
    and (x.accuracy_m is null or x.accuracy_m between 0 and 100000)
    and (x.device_id is null or exists(select 1 from public.devices d where d.id=x.device_id and d.truck_id=a.vehicle_id and d.active))
    order by x.ts desc limit 1;
  if found then v_gps=jsonb_build_object('latitude',g.lat,'longitude',g.lon,'sampledAt',g.ts,'source',g.source,'provider',g.provider,
    'accuracyM',g.accuracy_m,'verification','RECEIVED_NOT_PHYSICALLY_VERIFIED'); end if;
  v_scope=jsonb_build_object('tenantId',a.tenant_id,'vehicleId',a.vehicle_id,'multimediaDeviceId',a.device_id,'userId',a.principal_id,'sessionId',a.session_id);
  v_policy=jsonb_build_object('scope',v_scope,'version',a.policy_version,'issuedAt',a.created_at,'expiresAt',a.expires_at,
    'financialRead',a.financial_read,'journeyRead',a.journey_read);
  -- Permissão não valida dado: modelos financeiros/jornada/consumo ainda sem fonte verificada neste canal.
  v_now=clock_timestamp();
  return jsonb_build_object('contractVersion',1,'scope',v_scope,'vehicleLabel',v_label,'coreDeviceUid',a.core_device_uid,
    'policy',v_policy,'trip',v_trip,'legs','[]'::jsonb,'gps',v_gps,'financial',null,'journey',null,'requestNonce',p_nonce,
    'receivedAt',v_now,'leaseExpiresAt',v_now+interval '20 seconds',
    'availability',jsonb_build_object('trip',case when v_trip is null then 'data_unavailable' else 'available' end,
      'gps',case when v_gps is null then 'data_unavailable' else 'received_not_physically_verified' end,
      'consumption','source_not_verified','financial',case when a.financial_read then 'source_not_verified' else 'not_authorized' end,
      'journey',case when a.journey_read then 'source_not_verified' else 'not_authorized' end));
end $$;
revoke all on function public.truckbox_multimedia_field_test_v1(text,text,uuid,uuid,text,text,bigint) from public,anon,authenticated;
grant execute on function public.truckbox_multimedia_field_test_v1(text,text,uuid,uuid,text,text,bigint) to service_role;
commit;
