CREATE OR REPLACE FUNCTION public.truckbox_protect_manual_trip_fuel_adjustments()
 RETURNS trigger
 LANGUAGE plpgsql
 SET search_path TO 'public'
AS $function$
declare
  v_manual_adjustment double precision := 0;
  v_measured_base double precision;
  v_factor double precision := 1.0;
  v_started timestamptz;
  v_fuel_changed boolean := true;
begin
  select coalesce(fuel_calibration_factor::double precision,1.0), fuel_calibration_started_at
    into v_factor, v_started
  from public.trucks where id=new.truck_id;

  if tg_op='UPDATE' then
    v_fuel_changed := new.fuel_liters is distinct from old.fuel_liters
                      or new.start_core_fuel_liters is distinct from old.start_core_fuel_liters
                      or new.end_core_fuel_liters is distinct from old.end_core_fuel_liters;
  end if;

  select coalesce(sum(nullif(e.data->>'liters','')::double precision),0)
    into v_manual_adjustment
  from public.events e
  where e.type='MANUAL_FUEL_ADJUSTMENT'
    and e.data->>'trip_id'=new.id::text;

  if v_manual_adjustment > 0
     and new.start_core_fuel_liters is not null
     and new.end_core_fuel_liters is not null
     and new.end_core_fuel_liters >= new.start_core_fuel_liters then
    v_measured_base := new.end_core_fuel_liters-new.start_core_fuel_liters;
    new.fuel_liters := v_measured_base+v_manual_adjustment;
    if v_started is not null and new.started_at is not null and new.started_at >= v_started then
      new.fuel_liters := new.fuel_liters*v_factor;
    end if;
  elsif v_fuel_changed and v_started is not null and new.started_at is not null and new.started_at >= v_started and new.fuel_liters is not null then
    new.fuel_liters := new.fuel_liters*v_factor;
  end if;

  if new.distance_km is not null and new.distance_km > 0 and new.fuel_liters is not null and new.fuel_liters > 0 then
    new.average_kml := new.distance_km/new.fuel_liters;
  end if;

  return new;
end;
$function$;
