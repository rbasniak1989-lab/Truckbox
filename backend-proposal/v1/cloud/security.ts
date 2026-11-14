export type UserIdentity = Readonly<{userId: string; sessionId: string}>;
export type DeviceIdentity = UserIdentity & Readonly<{deviceId: string}>;
export interface UserVerifier { verify(token: string): Promise<UserIdentity | null>; }
export interface DeviceRegistry {
  sessionActive(user: UserIdentity): Promise<boolean>;
  publicKey(deviceId: string): Promise<string | null>;
  enroll(user: UserIdentity,deviceId: string,spki: string,label: string): Promise<void>;
  claimNonce(deviceId: string,nonce: string,expiresAt: Date): Promise<boolean>;
}
export class AccessError extends Error {
  readonly status: number;
  readonly code: string;
  constructor(status: number,code: string) { super(code);this.status=status;this.code=code; }
}
export const uuid=(s: unknown): s is string => typeof s==='string'&&/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(s);
const decode64=(s: string): Uint8Array<ArrayBuffer> => {
  if(!/^[A-Za-z0-9+/]+={0,2}$/.test(s)) throw new AccessError(401,'invalid_device_proof');
  try {return Uint8Array.from(atob(s),c=>c.charCodeAt(0));} catch {throw new AccessError(401,'invalid_device_proof');}
};
export const sha256=async (bytes: Uint8Array<ArrayBuffer>) => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256',bytes)),b=>b.toString(16).padStart(2,'0')).join('');
const bytes=(s: string)=>new TextEncoder().encode(s);
export function canonicalTarget(url: URL): string {
  let path=url.pathname;
  if(path.startsWith('/functions/v1/multimedia-next-v1/v1/'))path=path.slice('/functions/v1'.length);
  else if(path.startsWith('/v1/'))path='/multimedia-next-v1'+path;
  return path+url.search;
}
export async function canonicalProof(req: Request,body: Uint8Array<ArrayBuffer>,token: string): Promise<string> {
  const u=new URL(req.url);
  return ['truckbox-next-v1',req.headers.get('x-truckbox-device')?.toLowerCase(),req.method,canonicalTarget(u),
    req.headers.get('x-truckbox-time'),req.headers.get('x-truckbox-nonce')?.toLowerCase(),await sha256(body),await sha256(bytes(token))].join('\n');
}

/** Supabase Auth verifica a assinatura; sessão revogada é verificada também no banco por requisição. */
export class SupabaseUserVerifier implements UserVerifier {
  private readonly baseUrl: string;
  private readonly publishableKey: string;
  private readonly fetcher: typeof fetch;
  constructor(baseUrl: string,publishableKey: string,fetcher: typeof fetch=fetch) {
    this.baseUrl=baseUrl.replace(/\/$/,'');this.publishableKey=publishableKey;this.fetcher=fetcher;
    const u=new URL(baseUrl);
    if(u.protocol!=='https:'||u.username||u.password||u.search||u.hash||u.pathname!=='/') throw new Error('invalid_auth_configuration');
  }
  async verify(token: string): Promise<UserIdentity | null> {
    if(token.length>16384) return null;
    const r=await this.fetcher(this.baseUrl+'/auth/v1/user',{headers:{apikey:this.publishableKey,Authorization:'Bearer '+token},redirect:'error'});
    if(!r.ok) return null;
    const user: unknown=await r.json();
    if(!user||typeof user!=='object'||!('id' in user)||!uuid(user.id)) return null;
    try {
      const part=token.split('.')[1];
      if(!part) return null;
      const claims=JSON.parse(atob(part.replace(/-/g,'+').replace(/_/g,'/'))) as Record<string,unknown>;
      if(claims.sub!==user.id||!uuid(claims.session_id)||claims.iss!==this.baseUrl+'/auth/v1'||typeof claims.exp!=='number'||claims.exp*1000<=Date.now()) return null;
      return {userId:user.id,sessionId:claims.session_id};
    } catch {return null;}
  }
}

/** Prova RSA/SHA256 compatível com Android Keystore; token, corpo, rota, nonce e relógio são vinculados. */
export async function authenticateDevice(req: Request,users: UserVerifier,registry: DeviceRegistry,now: Date): Promise<{identity: DeviceIdentity; enrolled: boolean;body: Uint8Array<ArrayBuffer>}> {
  const auth=/^Bearer ([A-Za-z0-9._-]+)$/.exec(req.headers.get('authorization')??'');
  if(!auth) throw new AccessError(401,'authentication_required');
  const user=await users.verify(auth[1]);
  if(!user||!await registry.sessionActive(user)) throw new AccessError(401,'authentication_required');
  const deviceId=req.headers.get('x-truckbox-device')?.toLowerCase();
  const nonce=req.headers.get('x-truckbox-nonce')?.toLowerCase();
  const time=req.headers.get('x-truckbox-time')??'';
  const signature=req.headers.get('x-truckbox-signature')??'';
  if(!uuid(deviceId)||!uuid(nonce)||!/^\d{13}$/.test(time)||Math.abs(now.getTime()-Number(time))>60000||signature.length>1024) throw new AccessError(401,'invalid_device_proof');
  const length=Number(req.headers.get('content-length')??0);
  if(!Number.isSafeInteger(length)||length<0||length>4096) throw new AccessError(413,'body_too_large');
  const reader=req.body?.getReader();const chunks: Uint8Array<ArrayBuffer>[]=[];let size=0;
  if(reader) {
    try {while(true) {const item=await reader.read();if(item.done)break;size+=item.value.length;if(size>4096){await reader.cancel();throw new AccessError(413,'body_too_large');}chunks.push(new Uint8Array(item.value));}} finally {reader.releaseLock();}
  }
  const body=new Uint8Array(size);let at=0;for(const c of chunks){body.set(c,at);at+=c.length;}
  const enrollment=req.method==='POST'&&new URL(req.url).pathname.endsWith('/v1/enroll');
  let proposed: {publicKeySpki: string; label: string} | null=null;
  if(enrollment) {
    try {
      const v: unknown=JSON.parse(new TextDecoder().decode(body));
      if(!v||typeof v!=='object'||!('publicKeySpki' in v)||typeof v.publicKeySpki!=='string'||v.publicKeySpki.length>2048||!('label' in v)||typeof v.label!=='string'||v.label.length<1||v.label.length>80) throw new Error();
      proposed={publicKeySpki:v.publicKeySpki,label:v.label};
    } catch {throw new AccessError(400,'invalid_enrollment');}
  } else if(size!==0&&!(req.method==='POST'&&new URL(req.url).pathname.endsWith('/v1/policy/ack'))) throw new AccessError(400,'unexpected_body');
  const spki=proposed?.publicKeySpki??await registry.publicKey(deviceId);
  if(!spki) throw new AccessError(403,'device_not_enrolled');
  let key: CryptoKey;
  try {key=await crypto.subtle.importKey('spki',decode64(spki),{name:'RSASSA-PKCS1-v1_5',hash:'SHA-256'},false,['verify']);}
  catch {throw new AccessError(401,'invalid_device_proof');}
  const bits=(key.algorithm as RsaHashedKeyAlgorithm).modulusLength;
  if(bits<2048||bits>4096||!await crypto.subtle.verify('RSASSA-PKCS1-v1_5',key,decode64(signature),bytes(await canonicalProof(req,body,auth[1])))) throw new AccessError(401,'invalid_device_proof');
  if(proposed) await registry.enroll(user,deviceId,proposed.publicKeySpki,proposed.label);
  if(!await registry.claimNonce(deviceId,nonce,new Date(now.getTime()+120000))) throw new AccessError(409,'replayed_request');
  return {identity:{...user,deviceId},enrolled:enrollment,body};
}
