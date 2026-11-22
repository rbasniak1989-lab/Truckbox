-- Homologation first. Install before enabling the new producer contract.
begin;
set local lock_timeout = '5s';

do $$
begin
  if (select md5(prosrc) from pg_proc where oid=to_regprocedure('public.truckbox_apply_trip_rate_tail(uuid,double precision)'))
        is distinct from 'a71304f0827c124c3d6909eb6831204f'
     or (select md5(prosrc) from pg_proc where oid=to_regprocedure('public.truckbox_rebuild_trip_fuel_rate(uuid,double precision)'))
        is distinct from '0001e96e6ec62f9fe6a136466768b8e9' then
    raise exception 'Expected the reviewed production fuel functions';
  end if;
end $$;

create schema truckbox_fuel;
revoke all on schema truckbox_fuel from public;
grant usage on schema truckbox_fuel to service_role;

create table truckbox_fuel.trip_checkpoint (
  trip_id uuid primary key references public.trips(id) on delete cascade,
  base_at timestamptz not null,
  base_l float8 not null check (base_l>=0),
  base_km float8 not null check (base_km>=0),
  legacy_rate float8,
  manual_at timestamptz,
  manual_km float8,
  manual_l float8,
  locked_at timestamptz not null default now()
);

create table truckbox_fuel.window (
  device_id uuid not null, core_uid text not null, boot bigint not null, slot bigint not null,
  trip_id uuid not null references public.trips(id) on delete cascade,
  end_at timestamptz not null, ul bigint not null, covered_ms integer not null,
  readings bigint not null, min_mlph integer not null, max_mlph integer not null, total_ul bigint not null,
  counter_at timestamptz, ticks bigint, mid integer, flags integer, counter_ul bigint, counter_cover_ms bigint,
  primary key (device_id,core_uid,boot,slot)
);
create index truckbox_fuel_window_trip_time on truckbox_fuel.window(trip_id,end_at);
alter table truckbox_fuel.trip_checkpoint enable row level security;
alter table truckbox_fuel.window enable row level security;
grant select,insert on truckbox_fuel.trip_checkpoint,truckbox_fuel.window to service_role;

alter table public.trips add column fuel_window_summary jsonb;

-- Register the verified manual reference and freeze everything the running
-- system has already booked through its latest watermark. Existing trip
-- totals, samples, odometer-anchor columns and fueling records are untouched.
do $$
declare t public.trips%rowtype;
begin
  select * into t from public.trips where id='5a63747c-daa7-4945-9b56-390b9c6d972a' and status='ACTIVE' for update;
  if not found then return; end if;
  if t.truck_id is distinct from '637e3c7a-2431-43c9-ac10-3d0c4e6f8de0'::uuid
     or t.live_base_distance_km is distinct from 742::float8
     or t.live_baseline_at is distinct from timestamptz '2026-11-22 21:22:39.609642+00'
     or t.rate_last_ts is null or t.rate_last_ts<t.live_baseline_at
     or not coalesce(t.fuel_liters>=542 and t.distance_km>=742,false) then
    raise exception 'The active TruckBox trip no longer matches its reviewed manual anchor';
  end if;
  insert into truckbox_fuel.trip_checkpoint(trip_id,base_at,base_l,base_km,legacy_rate,manual_at,manual_km,manual_l)
  values(t.id,t.rate_last_ts,t.fuel_liters,t.distance_km,t.rate_last_lph,t.live_baseline_at,742,542);
end $$;

create function truckbox_fuel.uint(v jsonb) returns bigint
language sql immutable strict set search_path=''
return case when jsonb_typeof(v)='number' and v #>> '{}' ~ '^[0-9]{1,16}$'
            then (v #>> '{}')::bigint end;

create function truckbox_fuel.receive_window() returns trigger
language plpgsql security invoker set search_path=''
as $$
declare
  w jsonb;
  v bigint[];
  b bigint;
  u text;
  wall_end timestamptz;
  counter_time timestamptz;
  width integer;
begin
  if current_user not in ('postgres','service_role') or new.trip_id is null or new.sample_kind<>'OPERATIONAL'
     or new.raw->>'source' is distinct from 'REVB1_LINK_LITE' or new.raw->'time_valid' is distinct from 'true'::jsonb
     or jsonb_typeof(new.raw->'w183') is distinct from 'array' then return new; end if;
  if jsonb_array_length(new.raw->'w183')>3 then return new; end if;
  u:=new.raw->>'core_uid';
  b:=truckbox_fuel.uint(new.raw->'boot_id');
  if b is null or b not between 1 and 4294967295 or u is null or length(u)>32
     or not exists (select 1 from public.devices d where d.id=new.device_id and d.truck_id=new.truck_id and d.device_uid=u)
     or not exists (select 1 from public.trips t where t.id=new.trip_id and t.truck_id=new.truck_id) then return new; end if;
  for w in select value from jsonb_array_elements(new.raw->'w183') loop
    if jsonb_typeof(w) is distinct from 'array' then continue; end if;
    width:=jsonb_array_length(w);
    if width not in (8,14) then continue; end if;
    select array_agg(truckbox_fuel.uint(value) order by n) into v from jsonb_array_elements(w) with ordinality a(value,n);
    if array_position(v,null::bigint) is not null or v[1] not between 1 and 4294967295
       or v[2] not between 1704067200000 and 4102444800000
       or v[4]>10000 or v[5]>4294967295
       or v[6]>v[7] or v[7]>1000000 or (v[5]=0 and v[7]<>0) or v[8]<v[3] then continue; end if;
    if v[3]>(v[4]*1000000+3599)/3600 then continue; end if;
    wall_end:=to_timestamp(v[2]/1000.0);
    if wall_end<new.ts-interval '180 seconds' or wall_end>new.ts+interval '5 seconds' then continue; end if;
    counter_time:=null;
    if width=14 and v[9]>=(v[1]-1)*10000 and v[9]>0 and v[9]<v[1]*10000
       and v[10]<4294967295 and v[11] between 128 and 255 and v[12] between 0 and 3
       and v[13]<=v[8] and v[14]<=v[9] then
      counter_time:=to_timestamp((v[2]+v[9]-v[1]*10000)/1000.0);
    end if;
    insert into truckbox_fuel.window
      (device_id,core_uid,boot,slot,trip_id,end_at,ul,covered_ms,readings,min_mlph,max_mlph,total_ul,
       counter_at,ticks,mid,flags,counter_ul,counter_cover_ms)
    values (new.device_id,u,b,v[1],new.trip_id,wall_end,v[3],v[4],v[5],v[6],v[7],v[8],
       counter_time,case when counter_time is not null then v[10] end,
       case when counter_time is not null then v[11] end,case when counter_time is not null then v[12] end,
       case when counter_time is not null then v[13] end,case when counter_time is not null then v[14] end)
    on conflict (device_id,core_uid,boot,slot) do nothing;
  end loop;
  return new;
end $$;

create trigger truckbox_fuel_receive_v2 after insert on public.telemetry_samples
for each row when (new.raw ? 'w183') execute function truckbox_fuel.receive_window();

-- The existing trigger applies manual additions and global fuel calibration.
-- Keep it for legacy paths. The guarded publisher below owns the completed
-- baseline-plus-delta calculation for this contract.
do $$
declare sig regprocedure:='public.truckbox_protect_manual_trip_fuel_adjustments()'::regprocedure;
declare body text;
declare guard text:=$guard$begin
  -- TBX_FUEL_WINDOW_V2 guarded update
  if tg_op='UPDATE' and new.fuel_window_summary is distinct from old.fuel_window_summary
     and new.fuel_source='J1587_PID183_WINDOW_CORE_V2' and current_user in ('postgres','service_role') then
    return new;
  end if;
$guard$;
begin
  if (select md5(prosrc) from pg_proc where oid=sig) is distinct from '498ef9a5cf1abc59dcb1eb859861a23e' then
    raise exception 'Unexpected manual adjustment trigger source';
  end if;
  body:=pg_get_functiondef(sig);
  execute replace(body,E'\nbegin\n',E'\n'||guard);
end $$;

create function truckbox_fuel.consolidate(p_trip uuid,p_gap_limit_s float8) returns integer
language plpgsql security invoker set search_path=''
as $$
declare
  t public.trips%rowtype;
  base truckbox_fuel.trip_checkpoint%rowtype;
  meter record;
  publication jsonb;
  latest_ts timestamptz;
  latest_seq bigint;
  latest_rate float8;
  distance float8;
  litres float8;
  avg_kml float8;
  legacy_l float8;
  legacy_gaps integer;
  legacy_gap_s float8;
  reported integer;
  calibration float8:=1;
  quality text;
begin
  if not pg_try_advisory_xact_lock(hashtextextended(p_trip::text,0)) then return 0; end if;
  select * into t from public.trips where id=p_trip for update;
  if not found or t.status<>'ACTIVE' or t.started_at is null then return 0; end if;
  insert into truckbox_fuel.trip_checkpoint(trip_id,base_at,base_l,base_km,legacy_rate)
  values (t.id,greatest(t.started_at,t.live_baseline_at,t.rate_last_ts),
          greatest(0,coalesce(t.fuel_liters,t.rate_fuel_liters,0)),greatest(0,coalesce(t.distance_km,0)),
          case when t.rate_last_ts>=greatest(t.started_at,t.live_baseline_at) then t.rate_last_lph end)
  on conflict (trip_id) do nothing;
  select * into strict base from truckbox_fuel.trip_checkpoint where trip_id=t.id;

  -- A newer manual adjustment keeps priority. It must be made the new explicit
  -- checkpoint before automatic fuel publishing resumes.
  if t.live_baseline_at>coalesce(base.manual_at,base.base_at) or
     exists (select 1 from public.events e where e.type='MANUAL_FUEL_ADJUSTMENT'
             and e.data->>'trip_id'=t.id::text and e.ts>=base.locked_at) then return 0; end if;

  select s.ts,s.seq,s.fuel_rate_lph into latest_ts,latest_seq,latest_rate
  from public.telemetry_samples s where s.trip_id=t.id and s.sample_kind='OPERATIONAL' and s.ts>base.base_at
  order by s.ts desc,s.seq desc,s.id desc limit 1;
  if not found then return 0; end if;

  select greatest(base.base_km,t.distance_km,max(s.trip_distance_km)) into distance
  from public.telemetry_samples s where s.trip_id=t.id and s.sample_kind='OPERATIONAL' and s.ts>base.base_at;
  select case when tr.fuel_calibration_started_at is not null and t.started_at>=tr.fuel_calibration_started_at
                  and tr.fuel_calibration_factor between 0.1 and 10 then tr.fuel_calibration_factor::float8 else 1 end
    into calibration from public.trucks tr where tr.id=t.truck_id;

  with ordered as (
    select w.*,max(w.end_at) over (order by w.end_at,w.device_id,w.boot,w.slot
      rows between unbounded preceding and 1 preceding) as previous_end
    from truckbox_fuel.window w where w.trip_id=t.id and w.end_at>=base.base_at+interval '10 seconds'
      and w.end_at<=coalesce(t.ended_at,latest_ts+interval '5 seconds')
  ), native as materialized (
    select *,row_number() over (partition by device_id,core_uid,boot order by slot) as position
    from ordered where previous_end is null or end_at>=previous_end+interval '10 seconds'
  ), missing as (
    select slot-lag(slot) over part-1 as slots,
           total_ul-ul-lag(total_ul) over part as observed_ul,
           end_at-lag(end_at) over part as elapsed,
           slot-lag(slot) over part as elapsed_slots
    from native window part as (partition by device_id,core_uid,boot order by slot)
  ), anchors as (
    select w.*,lag(ticks) over part as previous_ticks,lag(mid) over part as previous_mid
    from native w where (flags & 3)=3
    window part as (partition by device_id,core_uid,boot order by counter_at,slot)
  ), runs as (
    select a.*,sum(case when ticks<previous_ticks or mid<>previous_mid then 1 else 0 end)
           over (partition by device_id,core_uid,boot order by counter_at,slot) as run
    from anchors a
  ), pairs as (
    select device_id,core_uid,boot,run,counter_at,
      slot-first_value(slot) over part=position-first_value(position) over part as contiguous,
      extract(epoch from counter_at-first_value(counter_at) over part)::float8 as seconds,
      (ticks-first_value(ticks) over part)*0.473176473 as ecu_l,
      (counter_ul-first_value(counter_ul) over part)/1000000.0 as observed_l,
      (counter_cover_ms-first_value(counter_cover_ms) over part)/1000.0 as observed_seconds
    from runs window part as (partition by device_id,core_uid,boot,run order by counter_at,slot)
  ), plausible as (
    select distinct on (device_id,core_uid,boot,run) p.*,
      sign(ecu_l-observed_l)*least(greatest(abs(ecu_l-observed_l)-0.473176473,0),0.05*observed_l) as correction
    from pairs p where contiguous and seconds>=300 and ecu_l>=10*0.473176473
      and observed_seconds>=0.95*seconds and ecu_l between observed_l*0.75 and observed_l*1.25
    order by device_id,core_uid,boot,run,counter_at desc
  )
  select count(*) as n,coalesce(sum(n.ul),0)/1000000.0 as raw_l,
     coalesce(sum(n.covered_ms),0)/1000.0 as covered_s,
     extract(epoch from max(n.end_at)-min(n.end_at)+interval '10 seconds') as span_s,
     max(n.end_at) as sampled_at,
     (array_agg(n.boot order by n.end_at desc,n.slot desc))[1] as latest_boot,
     (select coalesce(sum(observed_ul),0)/1000000.0 from missing where slots>0
       and elapsed=elapsed_slots*interval '10 seconds' and observed_ul between 0 and slots*2777778) as recovered_l,
     (select coalesce(sum(p.correction),0) from plausible p) as correction,
     (select count(*) from plausible) as validated_spans,
     (select max(p.counter_at) from plausible p where p.boot=(select w.boot from native w order by w.end_at desc,w.slot desc limit 1)
         and p.counter_at=(select max(r.counter_at) from runs r where r.boot=p.boot)) as matched_at
  into meter from native n;

  -- Legacy compatibility before cutover and on rollback. Both endpoints
  -- must be legacy, and the entire interval must be outside all Core windows.
  with timeline as (
    select base.base_at as ts,0::bigint as seq,base.legacy_rate as rate,false as native_protocol
    union all
    select s.ts,s.seq,s.fuel_rate_lph,
      coalesce(s.raw->>'source'='REVB1_LINK_LITE' and s.raw ? 'w183',false)
    from public.telemetry_samples s where s.trip_id=t.id and s.sample_kind='OPERATIONAL' and s.ts>base.base_at
  ), intervals as (
    select *,lag(ts) over w as previous_ts,lag(rate) over w as previous_rate,lag(native_protocol) over w as previous_native
    from timeline window w as (order by ts,seq)
  ), legacy as (
    select i.*,extract(epoch from ts-previous_ts)::float8 as seconds
    from intervals i where not native_protocol and not previous_native and rate between 0 and 1000
      and previous_rate between 0 and 1000
      and not exists (select 1 from truckbox_fuel.window q where q.trip_id=t.id
        and q.end_at>=base.base_at+interval '10 seconds' and q.end_at>i.previous_ts and q.end_at<i.ts+interval '10 seconds')
  )
  select coalesce(sum((rate+previous_rate)*seconds/7200) filter (where seconds between 0.001 and p_gap_limit_s),0),
      count(*) filter (where seconds>p_gap_limit_s),coalesce(sum(seconds) filter (where seconds>p_gap_limit_s),0)
  into legacy_l,legacy_gaps,legacy_gap_s from legacy;

  litres:=base.base_l+legacy_l*coalesce(calibration,1)+meter.raw_l+meter.recovered_l+meter.correction;
  avg_kml:=case when litres>0 then distance/litres end;
  quality:=case when meter.n=0 then 'INSUFFICIENT'
                when meter.covered_s=0 then 'INSUFFICIENT'
                when meter.covered_s<meter.span_s or legacy_gap_s>0 or
                     meter.sampled_at<latest_ts-interval '30 seconds' then 'ESTIMATED_GAPS'
                when meter.matched_at>=meter.sampled_at-interval '30 seconds' then 'RECONCILED_PID250'
                else 'INTEGRATED_PID183' end;
  publication:=jsonb_build_object('source','J1587_PID183_WINDOW_CORE_V2','quality',quality,
    'sampledAt',meter.sampled_at,'coveredSeconds',meter.covered_s,
    'uncoveredSeconds',greatest(0,coalesce(meter.span_s,0)-meter.covered_s),
    'windows',meter.n,'integratedLitres',meter.raw_l,'recoveredLitres',meter.recovered_l,'reconciliationLitres',meter.correction,
    'validatedSpans',meter.validated_spans,'matchedAt',meter.matched_at,
    'legacyLitres',legacy_l,'baseAt',base.base_at,'baseLitres',base.base_l,
    'manualAt',base.manual_at,'manualKm',base.manual_km,'manualLitres',base.manual_l);

  update public.trips set distance_km=distance,fuel_liters=litres,average_kml=avg_kml,
      rate_fuel_liters=litres,rate_last_ts=latest_ts,rate_last_seq=latest_seq,rate_last_lph=latest_rate,
      rate_gap_count=legacy_gaps+(case when meter.covered_s<meter.span_s then 1 else 0 end),
      rate_gap_seconds=legacy_gap_s+greatest(0,coalesce(meter.span_s,0)-meter.covered_s),
      fuel_source='J1587_PID183_WINDOW_CORE_V2',fuel_window_summary=publication,updated_at=now()
    where id=t.id and (fuel_window_summary,distance_km,rate_last_ts,rate_last_seq) is distinct from (publication,distance,latest_ts,latest_seq);
  update public.telemetry_samples s set cloud_rate_applied=true
    where s.trip_id=t.id and not s.cloud_rate_applied and s.ts>base.base_at and s.ts<=latest_ts;
  get diagnostics reported=row_count;
  update public.telemetry_latest l set data=coalesce(l.data,'{}'::jsonb)||jsonb_build_object(
        'trip_fuel_l',litres,'trip_distance_km',distance,'trip_average_kml',avg_kml,
        'trip_fuel_summary',publication),updated_at=now()
    where l.truck_id=t.truck_id and l.data->>'trip_id'=t.id::text and l.ts<=latest_ts;
  return reported;
end $$;

create function truckbox_fuel.keep_latest_time() returns trigger
language plpgsql security invoker set search_path=''
as $$
begin
  if new.ts<old.ts and new.data#>>'{raw,source}'='REVB1_LINK_LITE' then return null; end if;
  return new;
end $$;
create trigger truckbox_fuel_keep_latest_time before update of ts on public.telemetry_latest
for each row execute function truckbox_fuel.keep_latest_time();

alter function public.truckbox_apply_trip_rate_tail(uuid,float8) rename to truckbox_apply_trip_rate_tail_legacy;
alter function public.truckbox_rebuild_trip_fuel_rate(uuid,float8) rename to truckbox_rebuild_trip_fuel_rate_legacy;

create function public.truckbox_apply_trip_rate_tail(p_trip_id uuid,p_gap_limit_s float8 default 180)
returns table(trip_id uuid,distance_km float8,fuel_liters float8,average_kml float8,last_ts timestamptz,
  last_seq bigint,last_rate_lph float8,gap_count integer,gap_seconds float8,processed integer,rebuilt boolean)
language plpgsql security invoker set search_path=''
as $$
declare t public.trips%rowtype;
declare n integer:=0;
begin
  select * into t from public.trips where id=p_trip_id;
  if not found then raise exception 'trip_not_found'; end if;
  if current_user in ('postgres','service_role') and t.live_baseline_at is null and t.fuel_window_summary is null and not exists (
      select 1 from public.telemetry_samples s where s.trip_id=t.id and s.sample_kind='OPERATIONAL' and s.raw ? 'w183') then
    return query select * from public.truckbox_apply_trip_rate_tail_legacy(p_trip_id,p_gap_limit_s); return;
  end if;
  if current_user in ('postgres','service_role') and t.status='ACTIVE' then
    n:=truckbox_fuel.consolidate(p_trip_id,p_gap_limit_s);
    select * into t from public.trips where id=p_trip_id;
  end if;
  return query select t.id,t.distance_km,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,
      t.rate_last_lph,coalesce(t.rate_gap_count,0),coalesce(t.rate_gap_seconds,0),n,false;
end $$;

create function public.truckbox_rebuild_trip_fuel_rate(p_trip_id uuid,p_gap_limit_s float8 default 180)
returns table(trip_id uuid,fuel_liters float8,average_kml float8,last_ts timestamptz,last_seq bigint,
  last_rate_lph float8,gap_count integer,gap_seconds float8,samples_used bigint)
language plpgsql security invoker set search_path=''
as $$
declare t public.trips%rowtype;
begin
  select * into t from public.trips where id=p_trip_id;
  if not found then raise exception 'trip_not_found'; end if;
  if current_user in ('postgres','service_role') and t.live_baseline_at is null and t.fuel_window_summary is null and not exists (
      select 1 from public.telemetry_samples s where s.trip_id=t.id and s.sample_kind='OPERATIONAL' and s.raw ? 'w183') then
    return query select * from public.truckbox_rebuild_trip_fuel_rate_legacy(p_trip_id,p_gap_limit_s); return;
  end if;
  return query select r.trip_id,r.fuel_liters,r.average_kml,r.last_ts,r.last_seq,r.last_rate_lph,r.gap_count,
    r.gap_seconds,coalesce((x.fuel_window_summary->>'windows')::bigint,0)
    from public.truckbox_apply_trip_rate_tail(p_trip_id,p_gap_limit_s) r join public.trips x on x.id=r.trip_id;
end $$;

create function truckbox_fuel.protect_close() returns trigger
language plpgsql security invoker set search_path=''
as $$
begin
  if (old.fuel_window_summary is not null or old.live_baseline_at is not null)
     and coalesce(new.fuel_source,'') not like 'MANUAL%' then
    new.fuel_liters:=old.fuel_liters;
    new.distance_km:=old.distance_km;
    new.average_kml:=old.average_kml;
  end if;
  return new;
end $$;
create trigger z_truckbox_fuel_protect_close before update of status on public.trips
for each row when (old.status='ACTIVE' and new.status='COMPLETED') execute function truckbox_fuel.protect_close();

-- Public callers may read the scoped result through the wrappers. Only the
-- service role and database owner run write-side aggregation.
revoke execute on function public.truckbox_apply_trip_rate_tail_legacy(uuid,float8) from public,anon,authenticated;
revoke execute on function public.truckbox_rebuild_trip_fuel_rate_legacy(uuid,float8) from public,anon,authenticated;

commit;
