-- Review only. Captured production RPC definitions are pinned below.
begin;
set local lock_timeout='3s';
do $$
begin
  if (select md5(prosrc) from pg_proc where oid=to_regprocedure('public.truckbox_apply_trip_rate_tail(uuid,double precision)')) is distinct from 'a71304f0827c124c3d6909eb6831204f' then
    raise exception 'unexpected source of truckbox_apply_trip_rate_tail; review before applying';
  end if;
  if (select md5(prosrc) from pg_proc where oid=to_regprocedure('public.truckbox_rebuild_trip_fuel_rate(uuid,double precision)')) is distinct from '0001e96e6ec62f9fe6a136466768b8e9' then
    raise exception 'unexpected source of truckbox_rebuild_trip_fuel_rate; review before applying';
  end if;
end;
$$;

-- Pure helpers and the closing trigger for the Lite telemetry contract.
create function public.truckbox_lite_uint_v1(v jsonb) returns bigint
language sql immutable strict security invoker set search_path = ''
as $$
  select case when jsonb_typeof(v) = 'number' and v #>> '{}' ~ '^[0-9]{1,16}$'
              then case when (v #>> '{}')::bigint <= 9007199254740991 then (v #>> '{}')::bigint end
         end;
$$;

create function public.truckbox_lite_flow_window_v1(previous_device uuid, previous jsonb,
                                                   current_device uuid, current jsonb)
returns jsonb language plpgsql immutable security invoker set search_path = ''
as $$
declare
  a jsonb := previous->'f183';
  b jsonb := current->'f183';
  a_ms bigint;
  b_ms bigint;
  span bigint;
  covered bigint;
  ul bigint;
begin
  if previous_device is null or previous_device is distinct from current_device
     or previous->>'source' is distinct from 'REVB1_LINK_LITE'
     or current->>'source' is distinct from 'REVB1_LINK_LITE'
     or nullif(previous->>'core_uid','') is null or previous->>'core_uid' is distinct from current->>'core_uid'
     or coalesce(public.truckbox_lite_uint_v1(previous->'boot_id'),0) = 0
     or previous->>'boot_id' is distinct from current->>'boot_id'
     or jsonb_typeof(a) is distinct from 'array' or jsonb_typeof(b) is distinct from 'array' then
    return null;
  end if;
  if jsonb_array_length(a) <> 3 or jsonb_array_length(b) <> 3 then return null; end if;
  a_ms := public.truckbox_lite_uint_v1(a->0);
  b_ms := public.truckbox_lite_uint_v1(b->0);
  span := b_ms-a_ms;
  covered := public.truckbox_lite_uint_v1(b->1)-public.truckbox_lite_uint_v1(a->1);
  ul := public.truckbox_lite_uint_v1(b->2)-public.truckbox_lite_uint_v1(a->2);
  if a_ms <= 0 or span is null or covered is null or ul is null
     or span < 0 or covered < 0 or covered > span or ul < 0
     or ul > 1000.0 * covered / 3.6 + 1 or (covered = 0 and ul <> 0) then return null; end if;
  return jsonb_build_object('span_ms',span, 'covered_ms',covered, 'litres',ul/1000000.0,
         'average_lph',case when covered>0 then ul*3.6/covered end);
end;
$$;

-- The optional pair is a validated PID250 and odometer captured near the same time.
-- Both anchors carry physical capture timestamps, not Supabase receipt timestamps.
create function public.truckbox_lite_pid250_anchor_v1(raw jsonb, odo double precision)
returns table(fuel_at timestamptz, odo_at timestamptz, mid bigint, odo_mid bigint, fuel_l double precision)
language sql stable security invoker set search_path = ''
as $$
  with fields as (
    select public.truckbox_lite_uint_v1(raw #> '{a250,0}') as fmid,
           public.truckbox_lite_uint_v1(raw #> '{a250,1}') as omid,
           public.truckbox_lite_uint_v1(raw #> '{a250,2}') as fage,
           public.truckbox_lite_uint_v1(raw #> '{a250,3}') as oage,
           public.truckbox_lite_uint_v1(raw->'unix_ms') + public.truckbox_lite_uint_v1(raw->'sensor_age_ms') as snapshot_ms,
           public.truckbox_lite_uint_v1(raw->'valid_mask') as valid,
           public.truckbox_lite_uint_v1(raw->'estimated_mask') as estimated,
           case when jsonb_typeof(raw->'ecu_fuel_total_l')='number'
                then (raw->>'ecu_fuel_total_l')::numeric end as fuel
    where raw->>'source'='REVB1_LINK_LITE' and raw->'time_valid'='true'::jsonb
      and jsonb_typeof(raw->'a250')='array'
      and nullif(raw->>'core_uid','') is not null and odo>=0 and odo<1e9
  )
  select to_timestamp((snapshot_ms-fage)/1000.0),to_timestamp((snapshot_ms-oage)/1000.0),fmid,omid,fuel::double precision
  from fields
  where fmid between 128 and 255 and omid between 128 and 255
    and (valid & 96)=96 and (estimated & 96)=0 and fuel>=0 and fuel<1e10
    and fage between 0 and 30000 and oage between 0 and 30000 and abs(fage-oage)<=5000
    and snapshot_ms-greatest(fage,oage)>=1704067200000 and snapshot_ms<=4102444800000;
$$;

create function public.truckbox_lite_pid250_close_v1() returns trigger
language plpgsql security invoker set search_path = ''
as $$
declare
  pair record;
begin
  if old.status is distinct from 'ACTIVE' or new.status is distinct from 'COMPLETED'
     or new.started_at is null or new.ended_at is null or new.ended_at <= new.started_at
     or coalesce(new.fuel_source,'') like 'MANUAL%'
     or exists (select 1 from public.events e where e.type='MANUAL_FUEL_ADJUSTMENT'
                and e.data->>'trip_id'=new.id::text) then return new; end if;

  -- The closing client can send older locally calculated amounts.
  if old.fuel_source='J1587_PID183_WINDOW_CLOUD_V1' then
    new.distance_km := old.distance_km;
    new.fuel_liters := old.fuel_liters;
    new.average_kml := old.average_kml;
    new.fuel_source := old.fuel_source;
  end if;
  if new.start_odometer_km is null or new.end_odometer_km is null
     or new.end_odometer_km-new.start_odometer_km < 10 then return new; end if;

  with points as materialized (
    select s.device_id,s.raw->>'core_uid' as core_uid,s.odometer_km as odo, a.*
    from public.telemetry_samples s
    cross join lateral public.truckbox_lite_pid250_anchor_v1(s.raw,s.odometer_km) a
    where s.truck_id=new.truck_id and s.sample_kind='OPERATIONAL'
      and s.ts between new.started_at-interval '60 seconds' and new.ended_at+interval '60 seconds'
  ), choices as (
    select b.odo-a.odo as km, b.fuel_l-a.fuel_l as litres,
           abs(extract(epoch from (a.fuel_at-new.started_at)))+
           abs(extract(epoch from (b.fuel_at-new.ended_at))) as timing
    from points a join points b using(device_id,core_uid,mid,odo_mid)
    where a.fuel_at between new.started_at-interval '30 seconds' and new.started_at+interval '30 seconds'
      and a.odo_at between new.started_at-interval '30 seconds' and new.started_at+interval '30 seconds'
      and b.fuel_at between new.ended_at-interval '30 seconds' and new.ended_at
      and b.odo_at between new.ended_at-interval '30 seconds' and new.ended_at
      and abs(a.odo-new.start_odometer_km)<=0.30 and abs(b.odo-new.end_odometer_km)<=0.30
      and b.odo-a.odo>=10 and b.fuel_l>a.fuel_l and b.fuel_at>a.fuel_at
      and b.fuel_l-a.fuel_l<=1000.0*extract(epoch from (b.fuel_at-a.fuel_at))/3600.0+0.946352946
      and not exists (
        select 1 from (
          select p.fuel_l, lag(p.fuel_l) over (order by p.fuel_at,p.fuel_l) as previous_l
          from points p where p.device_id=a.device_id and p.core_uid=a.core_uid
            and p.mid=a.mid and p.odo_mid=a.odo_mid and p.fuel_at between a.fuel_at and b.fuel_at
        ) series where fuel_l<previous_l
      )
  )
  select km,litres into pair from choices order by timing limit 1;
  if found then
    new.distance_km := pair.km;
    new.fuel_liters := pair.litres;
    new.average_kml := pair.km/pair.litres;
    new.fuel_source := 'J1587_PID250_ECU_TOTAL_DELTA';
  end if;
  return new;
end;
$$;

-- Runs after the legacy BEFORE safeguards. Existing completed trips are outside its scope.
create trigger z_truckbox_lite_pid250_confirm_close_v1 before update of status on public.trips
for each row when (old.status='ACTIVE' and new.status='COMPLETED')
execute function public.truckbox_lite_pid250_close_v1();

CREATE OR REPLACE FUNCTION public.truckbox_apply_trip_rate_tail(p_trip_id uuid, p_gap_limit_s double precision DEFAULT 180)
 RETURNS TABLE(trip_id uuid, distance_km double precision, fuel_liters double precision, average_kml double precision, last_ts timestamp with time zone, last_seq bigint, last_rate_lph double precision, gap_count integer, gap_seconds double precision, processed integer, rebuilt boolean)
 LANGUAGE plpgsql
 SET search_path TO 'public'
AS $function$
DECLARE
 t public.trips%rowtype;
 r record;
 v_distance double precision;
 v_fuel double precision;
 v_last_ts timestamptz;
 v_last_seq bigint;
 v_last_rate double precision;
 v_last_core double precision;
 v_last_odo double precision;
 v_gap_count integer;
 v_gap_seconds double precision;
 v_recovered double precision;
 v_recovery_count integer;
 v_gap_liters double precision;
 v_dt double precision;
 v_avg double precision;
 v_processed integer:=0;
 v_rebuilt boolean:=false;
 v_late boolean:=false;
 v_latest_late_insert timestamptz;
 v_cal_factor double precision;
 v_last_device uuid;
 v_last_raw jsonb;
 v_window jsonb;
 v_current_window boolean;
 v_window_mode boolean;
BEGIN
 SELECT * INTO t FROM public.trips WHERE id=p_trip_id;
 IF NOT FOUND THEN RAISE EXCEPTION 'trip_not_found'; END IF;

 IF NOT pg_try_advisory_xact_lock(hashtextextended(p_trip_id::text,0)) THEN
   RETURN QUERY
   SELECT p_trip_id,t.distance_km,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,
          t.rate_last_lph,COALESCE(t.rate_gap_count,0),COALESCE(t.rate_gap_seconds,0),0,false;
   RETURN;
 END IF;

 SELECT * INTO t FROM public.trips WHERE id=p_trip_id FOR UPDATE;
 IF t.status='COMPLETED' AND t.fuel_source='J1587_PID250_ECU_TOTAL_DELTA' THEN
   RETURN QUERY SELECT p_trip_id,t.distance_km,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,
       t.rate_last_lph,COALESCE(t.rate_gap_count,0),COALESCE(t.rate_gap_seconds,0),0,false;
   RETURN;
 END IF;
 v_window_mode:=COALESCE(t.fuel_source='J1587_PID183_WINDOW_CLOUD_V1',false);

 SELECT fuel_calibration_factor::double precision INTO v_cal_factor
 FROM public.trucks WHERE id=t.truck_id;
 IF v_cal_factor IS NULL OR abs(v_cal_factor-1.0)<0.0000001 THEN
   PERFORM set_config('truckbox.skip_fuel_calibration','on',true);
 END IF;

 v_distance:=COALESCE(t.distance_km,0);
 v_fuel:=COALESCE(t.rate_fuel_liters,0);
 v_last_ts:=t.rate_last_ts;
 v_last_seq:=t.rate_last_seq;
 v_last_rate:=t.rate_last_lph;
 v_gap_count:=COALESCE(t.rate_gap_count,0);
 v_gap_seconds:=COALESCE(t.rate_gap_seconds,0);
 v_recovered:=COALESCE(t.rate_gap_recovered_liters,0);
 v_recovery_count:=COALESCE(t.rate_gap_recovery_count,0);

 IF v_last_ts IS NOT NULL THEN
   SELECT EXISTS(
      SELECT 1 FROM public.telemetry_samples s
      WHERE s.trip_id=p_trip_id AND NOT s.cloud_rate_applied
        AND (s.ts<v_last_ts OR
             (s.ts=v_last_ts AND COALESCE(s.seq,-1)<=COALESCE(v_last_seq,-1)))
   ) INTO v_late;
 END IF;

 IF v_last_ts IS NULL THEN
   PERFORM public.truckbox_rebuild_trip_fuel_rate(p_trip_id,p_gap_limit_s);
   v_rebuilt:=true;
   SELECT * INTO t FROM public.trips WHERE id=p_trip_id;
   RETURN QUERY
   SELECT p_trip_id,t.distance_km,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,
          t.rate_last_lph,COALESCE(t.rate_gap_count,0),COALESCE(t.rate_gap_seconds,0),0,v_rebuilt;
   RETURN;
 END IF;

 IF v_late THEN
   SELECT MAX(s.created_at) INTO v_latest_late_insert
   FROM public.telemetry_samples s
   WHERE s.trip_id=p_trip_id AND NOT s.cloud_rate_applied
     AND (s.ts<v_last_ts OR
          (s.ts=v_last_ts AND COALESCE(s.seq,-1)<=COALESCE(v_last_seq,-1)));

   IF v_latest_late_insert IS NOT NULL AND v_latest_late_insert>now()-interval '30 seconds' THEN
     RETURN QUERY
     SELECT p_trip_id,t.distance_km,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,
            t.rate_last_lph,COALESCE(t.rate_gap_count,0),COALESCE(t.rate_gap_seconds,0),0,false;
     RETURN;
   END IF;

   PERFORM public.truckbox_rebuild_trip_fuel_rate(p_trip_id,p_gap_limit_s);
   v_rebuilt:=true;
   SELECT * INTO t FROM public.trips WHERE id=p_trip_id;
   RETURN QUERY
   SELECT p_trip_id,t.distance_km,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,
          t.rate_last_lph,COALESCE(t.rate_gap_count,0),COALESCE(t.rate_gap_seconds,0),0,v_rebuilt;
   RETURN;
 END IF;

 SELECT NULLIF(s.raw->>'core_fuel_l','')::double precision,s.odometer_km,s.device_id,s.raw
 INTO v_last_core,v_last_odo,v_last_device,v_last_raw
 FROM public.telemetry_samples s
 WHERE s.trip_id=p_trip_id AND s.cloud_rate_applied
   AND s.ts=v_last_ts AND s.seq=v_last_seq
 ORDER BY s.ts DESC,s.seq DESC,s.id DESC
 LIMIT 1;

 FOR r IN
   SELECT s.id,s.ts,s.seq,s.trip_distance_km,
          s.fuel_rate_lph AS rate_lph,s.odometer_km,s.device_id,s.raw,
          NULLIF(s.raw->>'core_fuel_l','')::double precision AS core_l
   FROM public.telemetry_samples s
   WHERE s.trip_id=p_trip_id AND NOT s.cloud_rate_applied
     AND (s.ts>v_last_ts OR
          (s.ts=v_last_ts AND COALESCE(s.seq,-1)>COALESCE(v_last_seq,-1)))
   ORDER BY s.ts,s.seq,s.id
 LOOP
   IF r.trip_distance_km IS NOT NULL THEN
     v_distance:=GREATEST(v_distance,r.trip_distance_km);
   END IF;

   v_window:=NULL;
   v_current_window:=COALESCE(r.raw->>'source'='REVB1_LINK_LITE' AND r.raw ? 'f183',false);
   IF r.rate_lph IS NOT NULL OR v_current_window THEN
     v_dt:=EXTRACT(EPOCH FROM r.ts-v_last_ts);
     IF v_current_window OR COALESCE(v_last_raw->>'source'='REVB1_LINK_LITE' AND v_last_raw ? 'f183',false) THEN
       v_window_mode:=v_window_mode OR v_current_window;
       v_window:=public.truckbox_lite_flow_window_v1(v_last_device,v_last_raw,r.device_id,r.raw);
       IF v_window IS NOT NULL THEN
         v_fuel:=v_fuel+(v_window->>'litres')::double precision;
         v_dt:=((v_window->>'span_ms')::double precision-(v_window->>'covered_ms')::double precision)/1000.0;
       END IF;
       IF v_dt>0 THEN
         v_gap_count:=v_gap_count+1;
         v_gap_seconds:=v_gap_seconds+v_dt;
       END IF;
     ELSIF v_last_rate IS NOT NULL AND v_dt BETWEEN 0.001 AND p_gap_limit_s THEN
       v_fuel:=v_fuel+((v_last_rate+r.rate_lph)/2.0)*v_dt/3600.0;
     ELSIF v_dt>p_gap_limit_s THEN
       v_gap_count:=v_gap_count+1;
       v_gap_seconds:=v_gap_seconds+v_dt;
       v_gap_liters:=public.truckbox_core_gap_recovery(
           v_last_core,r.core_l,v_last_odo,r.odometer_km,v_dt,p_gap_limit_s
       );
       v_fuel:=v_fuel+v_gap_liters;
       v_recovered:=v_recovered+v_gap_liters;
       IF v_gap_liters>0 THEN
         v_recovery_count:=v_recovery_count+1;
       END IF;
     END IF;

     v_last_ts:=r.ts;
     v_last_seq:=r.seq;
     v_last_rate:=r.rate_lph;
     v_last_core:=r.core_l;
     v_last_odo:=r.odometer_km;
     v_last_device:=r.device_id;
     v_last_raw:=r.raw;
   END IF;

   UPDATE public.telemetry_samples s
   SET cloud_rate_applied=true,
       raw=CASE WHEN v_window IS NOT NULL AND (v_window->>'span_ms')::bigint>0
                THEN s.raw || jsonb_build_object('pid183_window',v_window)
                ELSE s.raw-'pid183_window' END,
       trip_fuel_l=v_fuel,
       trip_average_kml=CASE WHEN v_fuel>0.0001 AND s.trip_distance_km IS NOT NULL
           THEN s.trip_distance_km/v_fuel ELSE NULL END
   WHERE s.id=r.id;
   v_processed:=v_processed+1;
 END LOOP;

 v_avg:=CASE WHEN v_fuel>0.0001 THEN v_distance/v_fuel ELSE NULL END;

 IF v_processed>0 THEN
   UPDATE public.trips
   SET distance_km=v_distance,fuel_liters=v_fuel,average_kml=v_avg,
       fuel_source=CASE WHEN v_window_mode THEN 'J1587_PID183_WINDOW_CLOUD_V1' ELSE 'SPN183_RATE_CLOUD_V2' END,
       rate_fuel_liters=v_fuel,rate_last_ts=v_last_ts,rate_last_seq=v_last_seq,
       rate_last_lph=v_last_rate,rate_gap_count=v_gap_count,rate_gap_seconds=v_gap_seconds,
       rate_gap_recovered_liters=v_recovered,rate_gap_recovery_count=v_recovery_count,
       updated_at=now()
   WHERE id=p_trip_id;

   UPDATE public.telemetry_latest l
   SET data=jsonb_set(
        jsonb_set(COALESCE(l.data,'{}'::jsonb),'{trip_fuel_l}',to_jsonb(v_fuel),true),
        '{trip_average_kml}',to_jsonb(v_avg),true),
       updated_at=now()
   WHERE l.truck_id=t.truck_id
     AND l.data->>'trip_id'=p_trip_id::text
     AND l.ts<=v_last_ts;
 END IF;

 RETURN QUERY
 SELECT p_trip_id,v_distance,v_fuel,v_avg,v_last_ts,v_last_seq,v_last_rate,
        v_gap_count,v_gap_seconds,v_processed,v_rebuilt;
END;
$function$;

CREATE OR REPLACE FUNCTION public.truckbox_rebuild_trip_fuel_rate(p_trip_id uuid, p_gap_limit_s double precision DEFAULT 180)
 RETURNS TABLE(trip_id uuid, fuel_liters double precision, average_kml double precision, last_ts timestamp with time zone, last_seq bigint, last_rate_lph double precision, gap_count integer, gap_seconds double precision, samples bigint)
 LANGUAGE plpgsql
 SET search_path TO 'public'
AS $function$
DECLARE
 t public.trips%rowtype;
 v_distance double precision;
 v_fuel double precision;
 v_avg double precision;
 v_last_ts timestamptz;
 v_last_seq bigint;
 v_last_rate double precision;
 v_gap_count integer;
 v_gap_seconds double precision;
 v_gap_recovered double precision;
 v_gap_recovery_count integer;
 v_samples bigint;
 v_cal_factor double precision;
 v_window_mode boolean;
BEGIN
 SELECT * INTO t FROM public.trips WHERE id=p_trip_id FOR UPDATE;
 IF NOT FOUND THEN RAISE EXCEPTION 'trip_not_found'; END IF;
 IF t.status='COMPLETED' AND t.fuel_source='J1587_PID250_ECU_TOTAL_DELTA' THEN
   RETURN QUERY SELECT p_trip_id,t.fuel_liters,t.average_kml,t.rate_last_ts,t.rate_last_seq,t.rate_last_lph,
       COALESCE(t.rate_gap_count,0),COALESCE(t.rate_gap_seconds,0),0::bigint;
   RETURN;
 END IF;

 SELECT fuel_calibration_factor::double precision INTO v_cal_factor
 FROM public.trucks WHERE id=t.truck_id;
 IF v_cal_factor IS NULL OR abs(v_cal_factor-1.0)<0.0000001 THEN
   PERFORM set_config('truckbox.skip_fuel_calibration','on',true);
 END IF;

 WITH ordered AS (
   SELECT s.id,s.ts,s.seq,s.trip_distance_km,s.odometer_km,s.fuel_rate_lph AS rate_lph,
          NULLIF(s.raw->>'core_fuel_l','')::double precision AS core_l,
          LAG(s.ts) OVER w AS prev_ts,
          LAG(s.fuel_rate_lph) OVER w AS prev_rate,
          LAG(NULLIF(s.raw->>'core_fuel_l','')::double precision) OVER w AS prev_core,
          LAG(s.odometer_km) OVER w AS prev_odo,s.device_id,s.raw,
          LAG(s.device_id) OVER w AS prev_device,LAG(s.raw) OVER w AS prev_raw,
          COALESCE(s.raw->>'source'='REVB1_LINK_LITE' AND s.raw ? 'f183',false) AS has_window
   FROM public.telemetry_samples s
   WHERE s.trip_id=p_trip_id AND (s.fuel_rate_lph IS NOT NULL OR (s.raw->>'source'='REVB1_LINK_LITE' AND s.raw ? 'f183'))
   WINDOW w AS (ORDER BY s.ts,s.seq,s.id)
 ), anchored AS (
   SELECT o.*,
          COALESCE(o.prev_ts,t.started_at) AS anchor_ts,
          CASE WHEN o.prev_ts IS NULL THEN t.start_core_fuel_liters ELSE o.prev_core END AS anchor_core,
          CASE WHEN o.prev_ts IS NULL THEN t.start_odometer_km ELSE o.prev_odo END AS anchor_odo,
          EXTRACT(EPOCH FROM (o.ts-COALESCE(o.prev_ts,t.started_at)))::double precision AS dt_s
   FROM ordered o
 ), windows AS (
   SELECT a.*,
          a.has_window OR COALESCE(a.prev_raw->>'source'='REVB1_LINK_LITE' AND a.prev_raw ? 'f183',false) AS window_mode,
          public.truckbox_lite_flow_window_v1(a.prev_device,a.prev_raw,a.device_id,a.raw) AS measured
   FROM anchored a
 ), increments AS (
   SELECT w.*,
          CASE WHEN w.window_mode THEN COALESCE((w.measured->>'litres')::double precision,0)
               WHEN w.prev_ts IS NOT NULL AND w.prev_rate IS NOT NULL AND w.dt_s BETWEEN 0.001 AND p_gap_limit_s
               THEN ((w.prev_rate+w.rate_lph)/2.0)*w.dt_s/3600.0
               ELSE 0::double precision END AS rate_inc,
          CASE WHEN w.window_mode THEN 0::double precision ELSE public.truckbox_core_gap_recovery(
             w.anchor_core,w.core_l,w.anchor_odo,w.odometer_km,w.dt_s,p_gap_limit_s
          ) END AS gap_inc,
          CASE WHEN w.window_mode AND w.measured IS NOT NULL THEN
                 ((w.measured->>'span_ms')::double precision-(w.measured->>'covered_ms')::double precision)/1000.0
               WHEN w.window_mode OR w.dt_s>p_gap_limit_s THEN GREATEST(w.dt_s,0)
               ELSE 0::double precision END AS gap_duration
   FROM windows w
 ), cumulative AS (
   SELECT i.*,
          SUM(i.rate_inc+i.gap_inc) OVER (
              ORDER BY i.ts,i.seq,i.id ROWS UNBOUNDED PRECEDING
          )::double precision AS cloud_fuel_l
   FROM increments i
 ), updated AS (
   UPDATE public.telemetry_samples s
   SET trip_fuel_l=c.cloud_fuel_l,
       raw=CASE WHEN c.measured IS NOT NULL AND (c.measured->>'span_ms')::bigint>0
                THEN s.raw || jsonb_build_object('pid183_window',c.measured)
                ELSE s.raw-'pid183_window' END,
       trip_average_kml=CASE WHEN c.cloud_fuel_l>0.0001 AND s.trip_distance_km IS NOT NULL
          THEN s.trip_distance_km/c.cloud_fuel_l ELSE NULL END,
       cloud_rate_applied=true
   FROM cumulative c
   WHERE s.id=c.id AND (
       NOT s.cloud_rate_applied
       OR s.raw->'pid183_window' IS DISTINCT FROM
            CASE WHEN (c.measured->>'span_ms')::bigint>0 THEN c.measured END
       OR s.trip_fuel_l IS DISTINCT FROM c.cloud_fuel_l
       OR s.trip_average_kml IS DISTINCT FROM (
           CASE WHEN c.cloud_fuel_l>0.0001 AND s.trip_distance_km IS NOT NULL
             THEN s.trip_distance_km/c.cloud_fuel_l ELSE NULL END
       )
   )
   RETURNING s.id
 )
 SELECT
    GREATEST(COALESCE(t.distance_km,0),COALESCE(MAX(c.trip_distance_km),0)),
    COALESCE(MAX(c.cloud_fuel_l),0),
    (ARRAY_AGG(c.ts ORDER BY c.ts DESC,c.seq DESC,c.id DESC))[1],
    (ARRAY_AGG(c.seq ORDER BY c.ts DESC,c.seq DESC,c.id DESC))[1],
    (ARRAY_AGG(c.rate_lph ORDER BY c.ts DESC,c.seq DESC,c.id DESC))[1],
    COUNT(*) FILTER (WHERE c.gap_duration>0)::integer,
    COALESCE(SUM(c.gap_duration),0)::double precision,
    COALESCE(SUM(c.gap_inc),0)::double precision,
    COUNT(*) FILTER (WHERE c.gap_inc>0)::integer,
    COUNT(*)::bigint,COALESCE(BOOL_OR(c.has_window),false)
 INTO v_distance,v_fuel,v_last_ts,v_last_seq,v_last_rate,v_gap_count,
      v_gap_seconds,v_gap_recovered,v_gap_recovery_count,v_samples,v_window_mode
 FROM cumulative c;

 UPDATE public.telemetry_samples s SET cloud_rate_applied=true
 WHERE s.trip_id=p_trip_id AND NOT s.cloud_rate_applied AND s.fuel_rate_lph IS NULL;

 v_avg:=CASE WHEN v_fuel>0.0001 THEN v_distance/v_fuel ELSE NULL END;

 UPDATE public.trips
 SET distance_km=v_distance,fuel_liters=v_fuel,average_kml=v_avg,
     fuel_source=CASE WHEN v_window_mode THEN 'J1587_PID183_WINDOW_CLOUD_V1' ELSE 'SPN183_RATE_CLOUD_V2' END,
     rate_fuel_liters=v_fuel,rate_last_ts=v_last_ts,rate_last_seq=v_last_seq,
     rate_last_lph=v_last_rate,rate_gap_count=v_gap_count,rate_gap_seconds=v_gap_seconds,
     rate_gap_recovered_liters=v_gap_recovered,rate_gap_recovery_count=v_gap_recovery_count,
     updated_at=now()
 WHERE id=p_trip_id;

 UPDATE public.telemetry_latest l
 SET data=jsonb_set(
           jsonb_set(COALESCE(l.data,'{}'::jsonb),
                      '{trip_fuel_l}',to_jsonb(v_fuel),true),
           '{trip_average_kml}',to_jsonb(v_avg),true),
     updated_at=now()
 WHERE l.truck_id=t.truck_id
   AND l.data->>'trip_id'=p_trip_id::text
   AND l.ts<=v_last_ts;

 RETURN QUERY SELECT p_trip_id,v_fuel,v_avg,v_last_ts,v_last_seq,v_last_rate,
                     v_gap_count,v_gap_seconds,v_samples;
END;
$function$;

-- Keep the existing roles of the public aggregation RPCs. New primitives accept
-- only supplied data and perform no privileged access to other vehicles.
revoke execute on function public.truckbox_lite_pid250_close_v1() from public;

commit;
