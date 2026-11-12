import { createClient } from 'https://esm.sh/@supabase/supabase-js@2.49.8';
import { diagnosticsHandler } from '../../../cloud/diagnostics/handler.mjs';
import catalog from '../../../cloud/diagnostics/volvo-fh-e5-0.1.0.json' with { type: 'json' };

const client = createClient(Deno.env.get('SUPABASE_URL')!, Deno.env.get('SUPABASE_SERVICE_ROLE_KEY')!,
  { auth: { persistSession: false, autoRefreshToken: false } });
async function result(query: any) {
  const { data, error } = await query;
  if (error) throw new Error('Database query failed');
  return data;
}
const kinds = ['J1708_FAST', 'J1708_REQUEST_EVENT', 'HEALTH', 'DECODED'];
Deno.serve(diagnosticsHandler({
  device: (uid: string) => result(client.from('devices').select('id,truck_id,token_hash,active').eq('device_uid', uid).maybeSingle()),
  trip: (truck: string, uid: string) => result(client.from('trips').select('id,client_uid,started_at,ended_at').eq('truck_id', truck).eq('client_uid', uid).maybeSingle()),
  page: (truck: string, cursor: string | null, trip: any) => {
    let q = client.from('telemetry_samples').select('id,seq,ts,sample_kind,raw').eq('truck_id', truck).in('sample_kind', kinds).order('id', { ascending: false }).limit(200);
    if (cursor) q = q.lt('id', cursor);
    // Boundaries are asynchronous captures: include nearby samples, expose
    // uncertainty, and never update/close financial trip records here.
    if (trip?.started_at) q = q.gte('ts', new Date(Date.parse(trip.started_at) - 1800000).toISOString());
    if (trip?.ended_at) q = q.lte('ts', new Date(Date.parse(trip.ended_at) + 1800000).toISOString());
    return result(q);
  },
  fragments: (truck: string, raw: any) => result(client.from('telemetry_samples').select('id,seq,ts,sample_kind,raw')
    .eq('truck_id', truck).eq('raw->>event', 'DIAGNOSTIC_RAW').eq('raw->>boot_id', String(raw.boot_id))
    .eq('raw->>capture_ms', String(raw.capture_ms)).eq('raw->>mid', String(raw.mid)).limit(8)),
  boundaries: (truck: string, uid: string) => result(client.from('telemetry_samples').select('id,seq,ts,sample_kind,raw')
    .eq('truck_id', truck).eq('raw->>event', 'COUNTER_CAPTURE').eq('raw->>trip_client_uid', uid)
    .in('raw->>reason', ['4', '5']).order('id', { ascending: true }).limit(12)),
}, catalog));
