import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {canonicalProof} from './security.ts';
const vector=JSON.parse(await readFile(new URL('./proof-vector.json',import.meta.url),'utf8')) as Record<string,string>;
const headers={'x-truckbox-device':vector.deviceId,'x-truckbox-time':vector.time,'x-truckbox-nonce':vector.nonce};
test('canonical proof matches the independent SHA256 vector shared with Android',async()=>{
  const req=new Request(vector.url,{method:vector.method,headers});
  assert.equal(await canonicalProof(req,new TextEncoder().encode(vector.body),vector.token),vector.canonical);
});
test('gateway path stripping preserves proof while route and query substitutions do not',async()=>{
  for(const url of [vector.url,vector.url.replace('/functions/v1/','/'),vector.url.replace('/functions/v1/multimedia-next-v1','')]) {
    assert.equal(await canonicalProof(new Request(url,{method:vector.method,headers}),new TextEncoder().encode(vector.body),vector.token),vector.canonical);
  }
  const changed=await canonicalProof(new Request(vector.url+'&vehicle_id=other',{method:vector.method,headers}),new TextEncoder().encode(vector.body),vector.token);
  assert.notEqual(changed,vector.canonical);
});
