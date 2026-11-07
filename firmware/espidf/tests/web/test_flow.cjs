/* Local-only browser regression. All fixtures are mock data, not measurements.
 * Run: node tests/web/test_flow.cjs
 */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const {chromium} = require('playwright');
const ROOT = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(ROOT, 'components/tb_web/index.html'));
const ADMIN = 'mock_admin_token_32_characters_ok';
const requests = [], failures = [];
let deviceRole = 'core', otaFailure = false, otaDelay = 40, config = {};
function reset(role) {
  deviceRole = role;
  config = {role,wifi_ssid:'Mock Router',core_ssid:'Mock Core',can_baud:250000,
    gnss_baud:9600,sequence_floor:0,capture_enabled:true,gnss_enabled:true,
    fram_enabled:false,sd_enabled:false,modem_enabled:false,repaired_power_gates:false,
    uplink_url:'https://example.invalid/mock-ingest',gps_url:'https://example.invalid/mock-gps',
    cloud_device_uid:'MOCK-CLOUD-CORE',gps_device_uid:'MOCK-CLOUD-GPS',
    apn:'mock.apn',apn_user:'',wifi_password_configured:true};
}
function json(res,status,value) {
  res.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store'});
  res.end(JSON.stringify(value));
}
const server = http.createServer(async(req,res) => {
  const chunks = [];
  for await (const chunk of req) chunks.push(chunk);
  const body = Buffer.concat(chunks);
  requests.push({method:req.method,url:req.url,headers:req.headers,body});
  if(req.url === '/') {
    res.writeHead(200,{'Content-Type':'text/html; charset=utf-8',
      'Content-Security-Policy':"default-src 'none'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; base-uri 'none'; form-action 'self'; frame-ancestors 'none'"});
    return res.end(html);
  }
  if(req.url === '/favicon.ico') {res.writeHead(404);return res.end();}
  if(req.url === '/api/state') return json(res,200,{ok:true,role:deviceRole,version:'MOCK-TEST-ONLY',
    device_uid:'<img src=x onerror="window.__xss=true">',network:{station_connected:true,
    station_ip:'192.0.2.23',access_point_running:true,ap_ip:'192.168.4.1',ap_ssid:'Mock AP'},
    hardware_notice:'Mock diagnostics <script>window.__xss=true</script>'});
  if(req.headers.authorization !== 'Bearer ' + ADMIN) return json(res,401,{ok:false,error:'Mock unauthorized'});
  if(req.url === '/api/config' && req.method === 'GET') return json(res,200,config);
  if(req.url === '/api/config' && req.method === 'POST') {
    let update;
    try {update = JSON.parse(body.toString());}
    catch {return json(res,400,{ok:false,error:'Mock invalid JSON'});}
    if(update.uplink_url && !update.uplink_url.startsWith('https://'))
      return json(res,400,{ok:false,error:'Mock endpoint must use HTTPS'});
    Object.assign(config,update);
    return json(res,200,{ok:true,message:'Mock configuration accepted; no device restarted.'});
  }
  if(req.url === '/api/pairing') return json(res,200,{ssid:'Mock Core',
    password:'mock-fixture-password',pair_key:'a'.repeat(64),host:'192.168.4.2'});
  if(req.url === '/api/ota' || req.url === '/api/ota/link') {
    await new Promise(resolve=>setTimeout(resolve,otaDelay));
    return otaFailure ? json(res,400,{ok:false,error:'Mock image role rejected'})
      : json(res,200,{ok:true,message:'Mock OTA accepted; no flash was written.'});
  }
  json(res,404,{ok:false,error:'Mock route missing'});
});
async function check(name,fn) {
  try {await fn();console.log('PASS '+name);}
  catch(error){failures.push({name,error:error.stack});console.error('FAIL '+name+'\n'+error.stack);}
}
function recent(method,url){return requests.filter(r=>r.method===method&&r.url===url).at(-1);}
async function login(page){
  await page.locator('#admin-token').fill(ADMIN);
  await page.locator('#login').click();
  await page.locator('#auth-status').filter({hasText:'Administrador'}).waitFor();
}
async function fieldsetDisabled(page, selector) {
  // Playwright's isDisabled() omits FIELDSET itself; its native disabled
  // property and the effective state of enclosed controls are authoritative.
  return page.locator(selector).evaluate(element => element.disabled);
}
async function acceptedConfig(page,action){
  const before=requests.filter(r=>r.method==='POST'&&r.url==='/api/config').length;
  await action();await page.locator('#save-config').click();
  await page.waitForFunction(()=>document.querySelector('#config-message').textContent.includes('Mock configuration accepted'));
  assert.equal(requests.filter(r=>r.method==='POST'&&r.url==='/api/config').length,before+1);
  return JSON.parse(recent('POST','/api/config').body.toString());
}
async function file(page,name,size=512,magic=0xe9){
  const buffer=Buffer.alloc(size);buffer[0]=magic;
  await page.locator('#firmware-file').setInputFiles({name,mimeType:'application/octet-stream',buffer});
}
(async()=>{
  reset('core');
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin='http://127.0.0.1:'+server.address().port;
  const browser=await chromium.launch({executablePath:'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  try{
    const context=await browser.newContext({viewport:{width:1120,height:900}});
    await context.route('**/*',route=>route.request().url().startsWith(origin+'/')?route.continue():route.abort());
    const page=await context.newPage(), errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    page.on('dialog',dialog=>dialog.accept());
    await page.goto(origin);
    await page.locator('#connection').filter({hasText:'Online'}).waitFor();
    await check('public status stays read-only and renders untrusted JSON as text',async()=>{
      assert.equal(await fieldsetDisabled(page,'#config-fields'),true);
      assert.equal(await fieldsetDisabled(page,'#ota-fields'),true);
      assert.equal(await page.locator('#save-config').isDisabled(),true);
      assert.equal(await page.locator('#device-label').textContent(),'<img src=x onerror="window.__xss=true">');
      assert.equal(await page.evaluate(()=>window.__xss),undefined);
      assert.equal(await page.locator('#device-label img').count(),0);
    });
    await check('wrong administrator key leaves configuration locked',async()=>{
      await page.locator('#admin-token').fill('wrong-mock-key');
      await page.locator('#login').click();
      await page.locator('#auth-message').filter({hasText:'Mock unauthorized'}).waitFor();
      assert.equal(await fieldsetDisabled(page,'#config-fields'),true);
      assert.equal(await page.locator('#save-config').isDisabled(),true);
    });
    await check('authenticated login enables Core controls without persisting its key',async()=>{
      await login(page);
      assert.equal(await page.locator('#admin-token').inputValue(),'');
      assert.equal(await fieldsetDisabled(page,'#config-fields'),false);
      assert.equal(await page.locator('#core-config').isVisible(),true);
      assert.equal(await page.locator('#modem-config').isVisible(),false);
      assert.equal(await page.locator('#forward-target').isVisible(),true);
      assert.deepEqual(await page.evaluate(()=>({local:localStorage.length,session:sessionStorage.length,cookie:document.cookie})),
        {local:0,session:0,cookie:''});
    });
    await check('configuration submits only dirty fields and omits emptied secrets',async()=>{
      const payload=await acceptedConfig(page,async()=>{
        await page.locator('[name=wifi_ssid]').fill('Mock New Router');
        await page.locator('[name=wifi_password]').fill('mock-secret-to-clear');
        await page.locator('[name=wifi_password]').fill('');
        await page.locator('[name=capture_enabled]').uncheck();
      });
      assert.deepEqual(payload,{wifi_ssid:'Mock New Router',capture_enabled:false});
      assert.equal(recent('POST','/api/config').headers.authorization,'Bearer '+ADMIN);
    });
    await check('explicit open-network checkbox clears Wi-Fi password',async()=>{
      await page.locator('#reload-config').click();
      await page.locator('#config-message').filter({hasText:'recarregada'}).waitFor();
      const payload=await acceptedConfig(page,async()=>page.locator('#open-wifi').check());
      assert.deepEqual(payload,{wifi_password:''});
    });
    await check('existing-cloud sequence accepts the largest safe JSON integer',async()=>{
      const payload=await acceptedConfig(page,async()=>{
        await page.locator('summary').filter({hasText:'Sequências'}).click();
        await page.locator('[name=sequence_floor]').fill('9007199254740991');
      });
      assert.deepEqual(payload,{sequence_floor:9007199254740991});
    });
    await check('pairing data is authenticated and copyable',async()=>{
      await page.locator('#show-pair').click();await page.locator('#pair-data').waitFor();
      const pairing=JSON.parse(await page.locator('#pair-text').inputValue());
      assert.equal(pairing.ssid,'Mock Core');assert.equal(pairing.pair_key.length,64);
      await page.locator('#copy-pair').click();
      await page.locator('#pair-message').filter({hasText:/copiados|selecionados/}).waitFor();
      assert.match(await page.locator('#pair-message').textContent(),/copiados|selecionados/);
    });
    await check('OTA rejects extension, short file, oversized file and invalid magic before upload',async()=>{
      const before=requests.filter(r=>r.method==='POST'&&r.url.startsWith('/api/ota')).length;
      for(const fixture of [
        ['mock.txt',512,0xe9,/\.bin/],['mock.bin',287,0xe9,/288 bytes/],
        ['mock.bin',3*1024*1024+1,0xe9,/3 MiB/],['mock.bin',512,0x00,/cabeçalho/]
      ]){
        await file(page,...fixture.slice(0,3));await page.locator('#upload').click();
        await page.waitForFunction(()=>!!document.querySelector('#ota-message').textContent);
        assert.match(await page.locator('#ota-message').textContent(),fixture[3]);
      }
      assert.equal(requests.filter(r=>r.method==='POST'&&r.url.startsWith('/api/ota')).length,before);
    });
    await check('server role rejection is shown and local OTA sends authenticated raw binary',async()=>{
      otaFailure=true;await file(page,'mock-wrong-role.bin');await page.locator('#upload').click();
      await page.locator('#ota-message').filter({hasText:'Mock image role rejected'}).waitFor();
      const req=recent('POST','/api/ota');
      assert.equal(req.body.length,512);assert.equal(req.body[0],0xe9);
      assert.equal(req.headers['content-type'],'application/octet-stream');
      assert.equal(req.headers.authorization,'Bearer '+ADMIN);
      assert.equal(await fieldsetDisabled(page,'#ota-fields'),false);otaFailure=false;
    });
    await check('Core forwards Link OTA to its dedicated route and presents progress',async()=>{
      await page.locator('[name=ota_target][value=link]').check();await file(page,'mock-link.bin');
      otaDelay=500;await page.locator('#upload').click();
      await page.waitForFunction(()=>document.querySelector('#ota-fields').disabled);
      assert.equal(await page.locator('#upload-progress').isVisible(),true);
      await page.locator('#ota-message').filter({hasText:'Mock OTA accepted'}).waitFor();
      assert.equal(recent('POST','/api/ota/link').body.length,512);
      assert.equal(await page.locator('#upload-progress').getAttribute('value'),'100');otaDelay=40;
    });
    await page.setViewportSize({width:390,height:844});
    await page.screenshot({path:path.join(__dirname,'core-mobile.png'),fullPage:true});
    await check('mobile page has no horizontal overflow',async()=>{
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    });
    await check('logout clears pairing and removes authorization from future state requests',async()=>{
      await page.locator('#logout').click();assert.equal(await fieldsetDisabled(page,'#config-fields'),true);
      assert.equal(await page.locator('#save-config').isDisabled(),true);
      assert.equal(await page.locator('#pair-text').inputValue(),'');assert.equal(await page.locator('#pair-data').isVisible(),false);
      await page.locator('#refresh').click();await page.waitForTimeout(100);
      assert.equal(recent('GET','/api/state').headers.authorization,undefined);
    });
    assert.deepEqual(errors,[]);await context.close();
    reset('link');
    const linkContext=await browser.newContext({viewport:{width:1120,height:900}});
    await linkContext.route('**/*',route=>route.request().url().startsWith(origin+'/')?route.continue():route.abort());
    const lp=await linkContext.newPage();lp.on('pageerror',e=>errors.push(e.message));lp.on('dialog',d=>d.accept());
    await lp.goto(origin);await lp.locator('#stat-role').filter({hasText:'Link'}).waitFor();await login(lp);
    await check('Link profile shows pairing/GPS/modem and hides Core forwarding/capture',async()=>{
      assert.equal(await lp.locator('#core-config').isVisible(),false);assert.equal(await lp.locator('#link-config').isVisible(),true);
      assert.equal(await lp.locator('#modem-config').isVisible(),true);assert.equal(await lp.locator('#forward-target').isVisible(),false);
      assert.equal(await lp.locator('#target-local').isChecked(),true);assert.equal(await lp.locator('#show-pair').isDisabled(),true);
    });
    await check('Link submits separate GPS credentials and pairing then clears secret inputs',async()=>{
      const payload=await acceptedConfig(lp,async()=>{
        await lp.locator('[name=core_ssid]').fill('Mock New Core');
        await lp.locator('[name=core_password]').fill('mock-private-wifi');
        await lp.locator('[name=pair_key]').fill('b'.repeat(64));
        await lp.locator('[name=cloud_device_uid]').fill('MOCK-NEW-CORE');
        await lp.locator('[name=gps_device_uid]').fill('MOCK-NEW-GPS');
        await lp.locator('[name=gps_url]').fill('https://example.invalid/new-gps');
        await lp.locator('[name=gps_token]').fill('mock-gps-device-token');
        await lp.locator('[name=gnss_enabled]').uncheck();
      });
      assert.deepEqual(payload,{core_ssid:'Mock New Core',core_password:'mock-private-wifi',pair_key:'b'.repeat(64),
        cloud_device_uid:'MOCK-NEW-CORE',gps_device_uid:'MOCK-NEW-GPS',gps_url:'https://example.invalid/new-gps',
        gps_token:'mock-gps-device-token',gnss_enabled:false});
      for(const name of ['core_password','pair_key','gps_token'])assert.equal(await lp.locator('[name='+name+']').inputValue(),'');
    });
    await check('configuration server errors stay visible without claiming successful save',async()=>{
      await lp.locator('[name=uplink_url]').fill('http://example.invalid/insecure');
      await lp.locator('#save-config').click();
      await lp.locator('#config-message').filter({hasText:'Mock endpoint must use HTTPS'}).waitFor();
      assert.equal(await lp.locator('#config-message').getAttribute('class'),'banner message error');
    });
    await lp.screenshot({path:path.join(__dirname,'link-desktop.png'),fullPage:true});
    assert.deepEqual(errors,[]);await linkContext.close();
    fs.writeFileSync(path.join(__dirname,'result.json'),JSON.stringify({
      scope:'Browser against local mock only; not physical OTA or production Supabase.',
      failed:failures,requests:requests.map(({method,url,body})=>({method,url,bytes:body.length})),browserErrors:errors
    },null,2));
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
  if(failures.length)process.exitCode=1;
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
