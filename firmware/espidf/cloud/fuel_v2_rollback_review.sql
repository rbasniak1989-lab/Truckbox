-- REVIEW ONLY: restore definitions observed before fuel_v2_review.sql.
BEGIN;
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

 SELECT NULLIF(s.raw->>'core_fuel_l','')::double precision,s.odometer_km
 INTO v_last_core,v_last_odo
 FROM public.telemetry_samples s
 WHERE s.trip_id=p_trip_id AND s.cloud_rate_applied
   AND s.ts<=v_last_ts
 ORDER BY s.ts DESC,s.seq DESC,s.id DESC
 LIMIT 1;

 FOR r IN
   SELECT s.id,s.ts,s.seq,s.trip_distance_km,
          s.fuel_rate_lph AS rate_lph,s.odometer_km,
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

   IF r.rate_lph IS NOT NULL THEN
     v_dt:=EXTRACT(EPOCH FROM r.ts-v_last_ts);
     IF v_last_rate IS NOT NULL AND v_dt BETWEEN 0.001 AND p_gap_limit_s THEN
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
   END IF;

   UPDATE public.telemetry_samples s
   SET cloud_rate_applied=true,
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
       fuel_source='SPN183_RATE_CLOUD_V2',
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
$function$
;
CREATE OR REPLACE FUNCTION public.truckbox_calibrate_telemetry_latest()
 RETURNS trigger
 LANGUAGE plpgsql
 SET search_path TO 'public'
AS $function$
declare
  v_factor double precision;
  v_started timestamptz;
  v_trip uuid;
  v_trip_started timestamptz;
  v_fuel double precision;
  v_dist double precision;
  v_avg double precision;
begin
  select fuel_calibration_factor::double precision, fuel_calibration_started_at
    into v_factor, v_started
  from public.trucks where id=new.truck_id;

  if v_factor is null or abs(v_factor-1.0)<0.0000001 or v_started is null or new.ts < v_started or new.data is null then
    return new;
  end if;

  begin
    v_trip := nullif(new.data->>'trip_id','')::uuid;
  exception when others then
    v_trip := null;
  end;

  v_fuel := nullif(new.data->>'trip_fuel_l','')::double precision;
  v_dist := nullif(new.data->>'trip_distance_km','')::double precision;

  if v_trip is not null then
    select started_at into v_trip_started from public.trips where id=v_trip;
    if v_trip_started is not null and v_trip_started >= v_started and v_fuel is not null then
      v_fuel := v_fuel * v_factor;
      new.data := jsonb_set(new.data,'{trip_fuel_l}',to_jsonb(v_fuel),true);
      if v_dist is not null and v_dist > 0 and v_fuel > 0 then
        v_avg := v_dist/v_fuel;
        new.data := jsonb_set(new.data,'{trip_average_kml}',to_jsonb(v_avg),true);
      end if;
    end if;
  elsif v_fuel is not null then
    v_fuel := public.truckbox_calibrate_fuel_value(new.truck_id,v_fuel,new.ts);
    new.data := jsonb_set(new.data,'{trip_fuel_l}',to_jsonb(v_fuel),true);
  end if;

  return new;
end;
$function$
;
CREATE OR REPLACE FUNCTION public.truckbox_calibrate_telemetry_sample()
 RETURNS trigger
 LANGUAGE plpgsql
 SET search_path TO 'public'
AS $function$
declare
  v_factor double precision;
  v_started timestamptz;
  v_trip_started timestamptz;
begin
  if current_setting('truckbox.skip_fuel_calibration',true)='on' then
    return new;
  end if;

  select fuel_calibration_factor::double precision, fuel_calibration_started_at
    into v_factor, v_started
  from public.trucks where id=new.truck_id;

  if v_factor is null or abs(v_factor-1.0)<0.0000001 or v_started is null or new.ts < v_started then
    return new;
  end if;

  if new.trip_id is not null then
    select started_at into v_trip_started from public.trips where id=new.trip_id;
    if v_trip_started is not null and v_trip_started >= v_started and new.trip_fuel_l is not null then
      new.trip_fuel_l := new.trip_fuel_l * v_factor;
      if new.trip_distance_km is not null and new.trip_distance_km > 0 and new.trip_fuel_l > 0 then
        new.trip_average_kml := new.trip_distance_km / new.trip_fuel_l;
      end if;
    end if;
  elsif new.trip_fuel_l is not null then
    new.trip_fuel_l := public.truckbox_calibrate_fuel_value(new.truck_id,new.trip_fuel_l,new.ts);
  end if;

  return new;
end;
$function$
;
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
BEGIN
 SELECT * INTO t FROM public.trips WHERE id=p_trip_id FOR UPDATE;
 IF NOT FOUND THEN RAISE EXCEPTION 'trip_not_found'; END IF;

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
          LAG(s.odometer_km) OVER w AS prev_odo
   FROM public.telemetry_samples s
   WHERE s.trip_id=p_trip_id AND s.fuel_rate_lph IS NOT NULL
   WINDOW w AS (ORDER BY s.ts,s.seq,s.id)
 ), anchored AS (
   SELECT o.*,
          COALESCE(o.prev_ts,t.started_at) AS anchor_ts,
          CASE WHEN o.prev_ts IS NULL THEN t.start_core_fuel_liters ELSE o.prev_core END AS anchor_core,
          CASE WHEN o.prev_ts IS NULL THEN t.start_odometer_km ELSE o.prev_odo END AS anchor_odo,
          EXTRACT(EPOCH FROM (o.ts-COALESCE(o.prev_ts,t.started_at)))::double precision AS dt_s
   FROM ordered o
 ), increments AS (
   SELECT a.*,
          CASE WHEN a.prev_ts IS NOT NULL AND a.prev_rate IS NOT NULL
                 AND a.dt_s BETWEEN 0.001 AND p_gap_limit_s
                 THEN ((a.prev_rate+a.rate_lph)/2.0)*a.dt_s/3600.0
               ELSE 0::double precision END AS rate_inc,
          public.truckbox_core_gap_recovery(
             a.anchor_core,a.core_l,a.anchor_odo,a.odometer_km,a.dt_s,p_gap_limit_s
          ) AS gap_inc,
          CASE WHEN a.dt_s>p_gap_limit_s THEN 1 ELSE 0 END AS is_gap
   FROM anchored a
 ), cumulative AS (
   SELECT i.*,
          SUM(i.rate_inc+i.gap_inc) OVER (
              ORDER BY i.ts,i.seq,i.id ROWS UNBOUNDED PRECEDING
          )::double precision AS cloud_fuel_l
   FROM increments i
 ), updated AS (
   UPDATE public.telemetry_samples s
   SET trip_fuel_l=c.cloud_fuel_l,
       trip_average_kml=CASE WHEN c.cloud_fuel_l>0.0001 AND s.trip_distance_km IS NOT NULL
          THEN s.trip_distance_km/c.cloud_fuel_l ELSE NULL END,
       cloud_rate_applied=true
   FROM cumulative c
   WHERE s.id=c.id AND (
       NOT s.cloud_rate_applied
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
    COALESCE(SUM(c.is_gap),0)::integer,
    COALESCE(SUM(CASE WHEN c.is_gap=1 THEN c.dt_s ELSE 0 END),0)::double precision,
    COALESCE(SUM(c.gap_inc),0)::double precision,
    COUNT(*) FILTER (WHERE c.gap_inc>0)::integer,
    COUNT(*)::bigint
 INTO v_distance,v_fuel,v_last_ts,v_last_seq,v_last_rate,v_gap_count,
      v_gap_seconds,v_gap_recovered,v_gap_recovery_count,v_samples
 FROM cumulative c;

 UPDATE public.telemetry_samples s SET cloud_rate_applied=true
 WHERE s.trip_id=p_trip_id AND NOT s.cloud_rate_applied AND s.fuel_rate_lph IS NULL;

 v_avg:=CASE WHEN v_fuel>0.0001 THEN v_distance/v_fuel ELSE NULL END;

 UPDATE public.trips
 SET distance_km=v_distance,fuel_liters=v_fuel,average_kml=v_avg,
     fuel_source='SPN183_RATE_CLOUD_V2',
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
$function$
;
COMMIT;
