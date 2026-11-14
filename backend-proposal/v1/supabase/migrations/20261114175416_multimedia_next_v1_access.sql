-- Proposta aditiva para revisão. NÃO aplicada ao projeto hospedado.
-- Só cria objetos privados; não altera tabelas/RPCs/credenciais legadas.
begin;
create schema multimedia_v1;
revoke all on schema multimedia_v1 from public, anon, authenticated;

create table multimedia_v1.tenants (
  id uuid primary key default gen_random_uuid(), label text not null check (length(label) between 1 and 100)
);
create table multimedia_v1.memberships (
  tenant_id uuid not null references multimedia_v1.tenants(id),
  user_id uuid not null references auth.users(id),
  active boolean not null default false,
  primary key (tenant_id,user_id)
);
create index memberships_user_idx on multimedia_v1.memberships(user_id,tenant_id) where active;
create table multimedia_v1.vehicle_owners (
  vehicle_id uuid primary key references public.trucks(id),
  tenant_id uuid not null references multimedia_v1.tenants(id),
  unique (tenant_id,vehicle_id)
);
create table multimedia_v1.devices (
  id uuid primary key,
  public_key_spki text not null check (length(public_key_spki) between 256 and 2048),
  label text not null check (length(label) between 1 and 80),
  created_at timestamptz not null default now()
);
create table multimedia_v1.device_requests (
  device_id uuid not null references multimedia_v1.devices(id),
  user_id uuid not null references auth.users(id),
  requested_at timestamptz not null default now(),
  primary key (device_id,user_id)
);
create index device_requests_user_idx on multimedia_v1.device_requests(user_id,requested_at);
create table multimedia_v1.bindings (
  tenant_id uuid not null,
  vehicle_id uuid not null,
  device_id uuid not null references multimedia_v1.devices(id),
  user_id uuid not null,
  active boolean not null default false,
  monitoring_read boolean not null default false,
  expires_at timestamptz not null,
  core_device_uid text,
  policy_version bigint not null default 1 check (policy_version between 1 and 9007199254740991),
  financial_read boolean not null default false,
  journey_read boolean not null default false,
  policy_issued_at timestamptz not null default now(),
  policy_expires_at timestamptz not null,
  primary key (tenant_id,vehicle_id,device_id,user_id),
  foreign key (tenant_id,vehicle_id) references multimedia_v1.vehicle_owners(tenant_id,vehicle_id),
  foreign key (tenant_id,user_id) references multimedia_v1.memberships(tenant_id,user_id),
  foreign key (device_id,user_id) references multimedia_v1.device_requests(device_id,user_id),
  check (policy_expires_at>policy_issued_at and policy_expires_at<=expires_at),
  check (not monitoring_read or (core_device_uid is not null and length(core_device_uid) between 1 and 128))
);
create unique index one_active_binding_per_user_device on multimedia_v1.bindings(user_id,device_id) where active;
create index bindings_vehicle_idx on multimedia_v1.bindings(tenant_id,vehicle_id);
create table multimedia_v1.request_nonces (
  device_id uuid not null references multimedia_v1.devices(id),
  nonce uuid not null,
  expires_at timestamptz not null,
  primary key (device_id,nonce)
);
create index request_nonces_expiry_idx on multimedia_v1.request_nonces(expires_at);
create index request_nonces_device_expiry_idx on multimedia_v1.request_nonces(device_id,expires_at);
create table multimedia_v1.policy_acks (
  tenant_id uuid not null,vehicle_id uuid not null,device_id uuid not null,user_id uuid not null,session_id uuid not null,
  policy_version bigint not null check (policy_version>0),applied_at timestamptz not null default now(),
  primary key(tenant_id,vehicle_id,device_id,user_id,session_id,policy_version),
  foreign key(tenant_id,vehicle_id,device_id,user_id) references multimedia_v1.bindings(tenant_id,vehicle_id,device_id,user_id)
);
create table multimedia_v1.policy_audit (
  id bigint generated always as identity primary key,
  tenant_id uuid not null, vehicle_id uuid not null, device_id uuid not null, user_id uuid not null,
  old_version bigint, new_version bigint not null,
  financial_read boolean not null, journey_read boolean not null, active boolean not null,
  changed_at timestamptz not null default now(), changed_by text not null default current_user
);

create function multimedia_v1.audit_policy_change() returns trigger language plpgsql as $$
begin
  if tg_op='UPDATE' then
    if row(new.tenant_id,new.vehicle_id,new.device_id,new.user_id) is distinct from row(old.tenant_id,old.vehicle_id,old.device_id,old.user_id) then
      raise exception 'scope_is_immutable';
    end if;
    if row(new.active,new.monitoring_read,new.expires_at,new.core_device_uid,new.financial_read,new.journey_read,new.policy_expires_at,new.policy_issued_at)
       is distinct from row(old.active,old.monitoring_read,old.expires_at,old.core_device_uid,old.financial_read,old.journey_read,old.policy_expires_at,old.policy_issued_at)
       and new.policy_version<>old.policy_version+1 then raise exception 'policy_version_must_advance'; end if;
    if new.policy_version<old.policy_version then raise exception 'policy_version_rollback'; end if;
  end if;
  insert into multimedia_v1.policy_audit(tenant_id,vehicle_id,device_id,user_id,old_version,new_version,financial_read,journey_read,active)
  values(new.tenant_id,new.vehicle_id,new.device_id,new.user_id,case when tg_op='UPDATE' then old.policy_version else null end,new.policy_version,new.financial_read,new.journey_read,new.active);
  return new;
end $$;
revoke all on function multimedia_v1.audit_policy_change() from public,anon,authenticated;
create trigger audit_policy before insert or update on multimedia_v1.bindings for each row execute function multimedia_v1.audit_policy_change();

-- Read models verificados, sem backfill presumido de contadores legados.
-- Ingestão/reconciliação desses modelos é uma etapa própria, não executada por GET.
create table multimedia_v1.verified_consumption (
  tenant_id uuid not null, vehicle_id uuid not null,
  leg_id text not null, kind text not null check (kind in ('LOADED','EMPTY')),
  distance_km double precision not null check (distance_km between 0 and 1000000000),
  consumed_liters double precision not null check (consumed_liters between 0 and 1000000000),
  source text not null check (source in ('CORE_ACCOUNTING_V2','ECU_COUNTER_RECONCILED')),
  source_boot_id bigint not null check (source_boot_id>=0),
  source_seq bigint not null check (source_seq>=0),
  source_verified boolean not null default false,
  updated_at timestamptz not null default now(),
  primary key (tenant_id,vehicle_id,leg_id),
  foreign key (tenant_id,vehicle_id) references multimedia_v1.vehicle_owners(tenant_id,vehicle_id)
);
create table multimedia_v1.financial_snapshots (
  tenant_id uuid not null, vehicle_id uuid not null, leg_id text not null,
  kind text not null check (kind in ('LOADED','EMPTY')),
  contracted_cents bigint check (contracted_cents between 0 and 9007199254740991),
  incurred_cents bigint not null check (incurred_cents between 0 and 9007199254740991),
  projected_cents bigint not null check (projected_cents between 0 and 9007199254740991),
  owner_costs_confirmed boolean not null default false,
  source_verified boolean not null default false,
  updated_at timestamptz not null default now(),
  primary key (tenant_id,vehicle_id,leg_id),
  foreign key (tenant_id,vehicle_id) references multimedia_v1.vehicle_owners(tenant_id,vehicle_id),
  check (kind<>'EMPTY' or contracted_cents is null)
);
create table multimedia_v1.journey_windows (
  tenant_id uuid not null, vehicle_id uuid not null, user_id uuid not null,
  reference timestamptz not null,
  driving_seconds integer not null check (driving_seconds>=0),
  rest_confirmed_seconds integer not null check (rest_confirmed_seconds>=0),
  waiting_seconds integer not null check (waiting_seconds>=0),
  other_stop_seconds integer not null check (other_stop_seconds>=0),
  unknown_seconds integer not null check (unknown_seconds>=0),
  source_verified boolean not null default false,
  primary key (tenant_id,vehicle_id,user_id,reference),
  foreign key (tenant_id,vehicle_id) references multimedia_v1.vehicle_owners(tenant_id,vehicle_id),
  foreign key (tenant_id,user_id) references multimedia_v1.memberships(tenant_id,user_id),
  check (driving_seconds+rest_confirmed_seconds+waiting_seconds+other_stop_seconds+unknown_seconds=86400)
);
do $$ declare t text; begin
  foreach t in array array['tenants','memberships','vehicle_owners','devices','device_requests','bindings','request_nonces','policy_acks','policy_audit','verified_consumption','financial_snapshots','journey_windows'] loop
    execute format('alter table multimedia_v1.%I enable row level security',t);
    execute format('revoke all on multimedia_v1.%I from public,anon,authenticated',t);
  end loop;
end $$;
revoke all on all sequences in schema multimedia_v1 from public,anon,authenticated;
alter default privileges in schema multimedia_v1 revoke all on tables from public,anon,authenticated;
alter default privileges in schema multimedia_v1 revoke all on functions from public,anon,authenticated;
commit;
