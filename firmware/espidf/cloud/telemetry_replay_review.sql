-- Review with deployed truckbox-ingest v26 and its replay patch.
-- Existing history is deliberately outside this transaction.
begin;
set local lock_timeout = '3s';

create or replace function public.truckbox_guard_latest_capture_v1()
returns trigger
language plpgsql
security invoker
set search_path = ''
as $$
begin
  if old.ts is not null and (
    new.ts is null or new.ts < old.ts or
    (new.ts = old.ts and (
      new.device_id is distinct from old.device_id or
      new.seq is null or (old.seq is not null and new.seq <= old.seq)
    ))
  ) then
    -- Skip the update and its downstream trip/empty-mode triggers atomically.
    return null;
  end if;
  return new;
end;
$$;

drop trigger if exists aa_truckbox_guard_latest_capture_v1 on public.telemetry_latest;
create trigger aa_truckbox_guard_latest_capture_v1
before update of ts, seq, device_id on public.telemetry_latest
for each row execute function public.truckbox_guard_latest_capture_v1();

create or replace function public.truckbox_guard_operation_capture_v1()
returns trigger
language plpgsql
security invoker
set search_path = ''
as $$
begin
  if new.last_telemetry_ts < old.last_telemetry_ts or (
    new.last_telemetry_ts = old.last_telemetry_ts and
    new.last_telemetry_seq < old.last_telemetry_seq
  ) then
    new.last_telemetry_ts := old.last_telemetry_ts;
    new.last_telemetry_seq := old.last_telemetry_seq;
    new.runtime_counters := old.runtime_counters;
  end if;
  if new.last_health_ts < old.last_health_ts then
    new.last_health_ts := old.last_health_ts;
  end if;
  return new;
end;
$$;

drop trigger if exists aa_truckbox_guard_operation_capture_v1 on public.operation_state;
create trigger aa_truckbox_guard_operation_capture_v1
before update of last_telemetry_ts, last_health_ts on public.operation_state
for each row execute function public.truckbox_guard_operation_capture_v1();

commit;
