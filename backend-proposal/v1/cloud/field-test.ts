import {AccessError,canonicalProof,sha256,uuid} from './security.ts';
export type FieldAction='context'|'activate'|'enroll'|'state'|'ack'|'logout';
export interface FieldTestPort { call(action: FieldAction,hash: string,device: string,nonce?: string,key?: string,core?: string,version?: number): Promise<Record<string,unknown>>; }
const json=(status: number,v: unknown)=>new Response(JSON.stringify(v),{status,headers:{'content-type':'application/json','cache-control':'no-store','vary':'Authorization, X-TruckBox-Device'}});

/** Acesso de campo temporário aprovado pelo proprietário; sem cadastro e sem credencial embutida no APK. */
export function createFieldTestApp(db: FieldTestPort,now: ()=>Date=()=>new Date()) {
  return async(req: Request): Promise<Response>=>{
    try {
      const path=new URL(req.url).pathname;
      const routes: Record<string,FieldAction>={'activate-field-test':'activate','enroll':'enroll','state':'state','ack':'ack','logout':'logout'};
      const action=routes[path.split('/').at(-1)??''];
      if(!action||(!path.endsWith('/v1/'+(action==='activate'?'activate-field-test':action))&&!(action==='ack'&&path.endsWith('/v1/policy/ack'))))return json(404,{error:'unknown_contract'});
      if(req.method!==(action==='state'?'GET':'POST'))return json(405,{error:'method_not_allowed'});
      const token=/^Bearer (tbxft_[0-9a-f]{64})$/.exec(req.headers.get('authorization')??'')?.[1];
      const device=req.headers.get('x-truckbox-device');const nonce=req.headers.get('x-truckbox-nonce');const time=req.headers.get('x-truckbox-time')??'';
      if(!token||!uuid(device)||!uuid(nonce)||!/^\d{13}$/.test(time)||Math.abs(now().getTime()-Number(time))>60000)return json(401,{error:'invalid_test_access'});
      const chunks: Uint8Array<ArrayBuffer>[]=[];let size=0;const reader=req.body?.getReader();
      if(reader)try {while(true){const v=await reader.read();if(v.done)break;size+=v.value.length;if(size>4096){await reader.cancel();throw new AccessError(413,'body_too_large');}chunks.push(new Uint8Array(v.value));}} finally {reader.releaseLock();}
      const body=new Uint8Array(size);let at=0;for(const b of chunks){body.set(b,at);at+=b.length;}
      let value: Record<string,unknown>={};if(size)try {value=JSON.parse(new TextDecoder().decode(body));if(!value||typeof value!=='object'||Array.isArray(value))throw Error();}catch {throw new AccessError(400,'invalid_body');}
      if(action==='state'&&size)throw new AccessError(400,'unexpected_body');
      const hash=await sha256(new TextEncoder().encode(token));const context=await db.call('context',hash,device);
      const proposed=action==='activate'?value.publicKeySpki:null;
      const spki=proposed??context.publicKeySpki;
      if(typeof spki!=='string'||spki.length>2048||!/^[A-Za-z0-9+/]+={0,2}$/.test(spki))throw new AccessError(401,'invalid_device_proof');
      if(context.publicKeySpki&&context.publicKeySpki!==spki)throw new AccessError(403,'device_key_conflict');
      const signature=req.headers.get('x-truckbox-signature')??'';
      if(!/^[A-Za-z0-9+/]+={0,2}$/.test(signature)||signature.length>1024)throw new AccessError(401,'invalid_device_proof');
      let key: CryptoKey;try {key=await crypto.subtle.importKey('spki',Uint8Array.from(atob(spki),c=>c.charCodeAt(0)),{name:'RSASSA-PKCS1-v1_5',hash:'SHA-256'},false,['verify']);}catch {throw new AccessError(401,'invalid_device_proof');}
      const bits=(key.algorithm as RsaHashedKeyAlgorithm).modulusLength;
      if(bits<2048||bits>4096||!await crypto.subtle.verify('RSASSA-PKCS1-v1_5',key,Uint8Array.from(atob(signature),c=>c.charCodeAt(0)),new TextEncoder().encode(await canonicalProof(req,body,token))))throw new AccessError(401,'invalid_device_proof');
      if(action==='activate'&&(typeof value.coreDeviceUid!=='string'||!/^[-A-Za-z0-9_]{1,128}$/.test(value.coreDeviceUid)))throw new AccessError(400,'invalid_core_identity');
      const version=action==='ack'?value.version:undefined;
      if(action==='ack'&&(typeof version!=='number'||!Number.isSafeInteger(version)||version<1))throw new AccessError(400,'invalid_ack');
      const out=await db.call(action,hash,device,nonce,action==='activate'?spki:undefined,action==='activate'?value.coreDeviceUid as string:undefined,version as number|undefined);
      return json(action==='enroll'?202:200,out);
    } catch(e) {return e instanceof AccessError?json(e.status,{error:e.code}):json(503,{error:'test_service_unavailable'});}
  };
}

/** service_role existe apenas no ambiente Edge; RPC sem grants para clientes comuns. */
export class SupabaseFieldTestPort implements FieldTestPort {
  private readonly url: string;
  private readonly serviceKey: string;
  private readonly fetcher: typeof fetch;
  constructor(url: string,serviceKey: string,fetcher: typeof fetch=fetch) {this.url=url;this.serviceKey=serviceKey;this.fetcher=fetcher;}
  async call(action: FieldAction,hash: string,device: string,nonce?: string,key?: string,core?: string,version?: number): Promise<Record<string,unknown>> {
    const r=await this.fetcher(this.url+'/rest/v1/rpc/truckbox_multimedia_field_test_v1',{method:'POST',redirect:'error',
      headers:{apikey:this.serviceKey,authorization:'Bearer '+this.serviceKey,'content-type':'application/json'},
      body:JSON.stringify({p_action:action,p_token_hash:hash,p_device_id:device,p_nonce:nonce??null,p_key:key??null,p_core_uid:core??null,p_version:version??null})});
    if(!r.ok) {
      let message='';try {message=(await r.json()).message??'';}catch {}
      if(message==='field_request_replayed'||message==='field_policy_changed')throw new AccessError(409,'test_request_conflict');
      if(message==='field_access_expired')throw new AccessError(401,'test_access_expired');
      if(message.startsWith('field_'))throw new AccessError(403,'test_access_denied');
      throw new AccessError(503,'test_service_unavailable');
    }
    return await r.json();
  }
}
