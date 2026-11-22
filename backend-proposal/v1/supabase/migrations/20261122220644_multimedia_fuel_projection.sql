-- Extend the field channel through its already authenticated, nonce-bound RPC.
-- This wrapper has no new device/client privileges and works before the fuel
-- migration by returning the original response until a projection exists.
begin;
do $$
declare digest text;
begin
  select md5(prosrc) into digest from pg_proc
    where oid=to_regprocedure('public.truckbox_multimedia_field_test_v1(text,text,uuid,uuid,text,text,bigint)');
  if digest is null or digest not in ('abeb189918b48ad2e218f70bf69e037a','9f9c71dc8a3607f5589b305c4529f7a3') then
    raise exception 'Field test RPC changed since review';
  end if;
end $$;
alter function public.truckbox_multimedia_field_test_v1(text,text,uuid,uuid,text,text,bigint)
  rename to truckbox_multimedia_field_test_v1_base;

create function public.truckbox_multimedia_field_test_v1(
  p_action text,p_token_hash text,p_device_id uuid,p_nonce uuid default null,
  p_key text default null,p_core_uid text default null,p_version bigint default null
) returns jsonb language plpgsql security invoker set search_path=''
as $$
declare
  reply jsonb;
  t jsonb;
  quality text;
  km numeric;
  litres numeric;
  leg jsonb;
begin
  reply:=public.truckbox_multimedia_field_test_v1_base(p_action,p_token_hash,p_device_id,p_nonce,p_key,p_core_uid,p_version);
  if p_action is distinct from 'state' or reply#>>'{trip,kind}' is distinct from 'LOADED' then return reply; end if;

  select to_jsonb(p) into t from public.trips p
    where p.id=(reply#>>'{trip,id}')::uuid and p.truck_id=(reply#>>'{scope,vehicleId}')::uuid;
  quality:=t#>>'{fuel_window_summary,quality}';
  if t->>'status' is distinct from 'ACTIVE' or t->>'fuel_source' is distinct from 'J1587_PID183_WINDOW_CORE_V2'
      or quality is null or quality not in ('INTEGRATED_PID183','RECONCILED_PID250','ESTIMATED_GAPS','INSUFFICIENT')
      or jsonb_typeof(t->'distance_km') is distinct from 'number'
      or jsonb_typeof(t->'fuel_liters') is distinct from 'number' then return reply; end if;
  km:=(t->>'distance_km')::numeric;
  litres:=(t->>'fuel_liters')::numeric;
  if km not between 0 and 1e9 or litres not between 0 and 1e9 then return reply; end if;
  leg:=jsonb_build_object('legId',reply#>>'{trip,legId}','kind','LOADED','distanceKm',km,
      'consumedLiters',litres,'averageKml',km/nullif(litres,0),
      'fuel',jsonb_build_object('quality',quality,'sampledAt',t#>'{fuel_window_summary,sampledAt}'));
  reply:=jsonb_set(reply,'{trip,consumption}',leg,true);
  reply:=jsonb_set(reply,'{legs}',jsonb_build_array(leg),true);
  return jsonb_set(reply,'{availability,consumption}','"available"'::jsonb,true);
end $$;
revoke all on function public.truckbox_multimedia_field_test_v1(text,text,uuid,uuid,text,text,bigint) from public,anon,authenticated;
grant execute on function public.truckbox_multimedia_field_test_v1(text,text,uuid,uuid,text,text,bigint) to service_role;
commit;
