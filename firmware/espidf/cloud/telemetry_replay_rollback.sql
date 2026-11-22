begin;
set local lock_timeout = '3s';
drop trigger if exists aa_truckbox_guard_latest_capture_v1 on public.telemetry_latest;
drop trigger if exists aa_truckbox_guard_operation_capture_v1 on public.operation_state;
drop function if exists public.truckbox_guard_latest_capture_v1();
drop function if exists public.truckbox_guard_operation_capture_v1();
commit;
