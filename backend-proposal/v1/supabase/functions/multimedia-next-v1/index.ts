import postgres from 'postgres';
import {createCloudApp} from '../../../cloud/app.ts';
import {postgresDatabase} from '../../../cloud/postgres-db.ts';
import {CloudRepository} from '../../../cloud/repository.ts';
import {SupabaseUserVerifier} from '../../../cloud/security.ts';
import {createFieldTestApp,SupabaseFieldTestPort} from '../../../cloud/field-test.ts';

// Credenciais exclusivamente do ambiente Edge. Nunca exportar esses valores ao Android ou em logs.
const required=(name: string)=>{
  const value=Deno.env.get(name);if(!value)throw new Error('missing_server_configuration');return value;
};
const testApp=createFieldTestApp(new SupabaseFieldTestPort(required('SUPABASE_URL'),required('SUPABASE_SERVICE_ROLE_KEY')));
let fullApp: ReturnType<typeof createCloudApp>|null=null;
Deno.serve(req=>{
  if(!req.headers.get('authorization')?.startsWith('Bearer '))return new Response('{"error":"authentication_required"}',{status:401,headers:{'content-type':'application/json','cache-control':'no-store'}});
  if(req.headers.get('authorization')?.startsWith('Bearer tbxft_'))return testApp(req);
  const dbUrl=Deno.env.get('MULTIMEDIA_DATABASE_URL')??Deno.env.get('SUPABASE_DB_URL');
  if(!dbUrl)return new Response('{"error":"authenticated_channel_pending"}',{status:503,headers:{'content-type':'application/json','cache-control':'no-store'}});
  if(!fullApp) {
    const sql=postgres(dbUrl,{max:1,prepare:false,ssl:{rejectUnauthorized:true},idle_timeout:20,connect_timeout:10});
    const verifier=new SupabaseUserVerifier(required('SUPABASE_URL'),Deno.env.get('MULTIMEDIA_PUBLISHABLE_KEY')??required('SUPABASE_ANON_KEY'));
    fullApp=createCloudApp(verifier,new CloudRepository(postgresDatabase(sql)));
  }
  return fullApp(req);
});
