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
let simulatedJ1708=null,diagnosticOOM=false;
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
  if(req.url === '/api/health') return json(res,200,{ok:true,diagnostic_partial:true,role:deviceRole,version:'MOCK-TEST-ONLY',uptime_ms:60000,heap_free:5000,heap_largest_block:256});
  if(req.url === '/api/state?compact=1' && diagnosticOOM) return json(res,503,{ok:false,error:'Sem memoria RAM para diagnostico completo'});
  if(req.url === '/api/state?compact=1') return json(res,200,{ok:true,role:deviceRole,version:'MOCK-TEST-ONLY',
    device_uid:'<img src=x onerror="window.__xss=true">',network:{station_connected:true,
    station_ip:'192.0.2.23',access_point_running:true,ap_ip:'192.168.4.1',ap_ssid:'Mock AP'},
    hardware_notice:'Mock diagnostics <script>window.__xss=true</script>',...(simulatedJ1708 || {})});
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
  if(req.url.startsWith('/api/j1708/requests?') && req.method==='GET'){
    const q=new URL(req.url,'http://127.0.0.1').searchParams;
    const offset=Number(q.get('offset')||0),limit=Number(q.get('limit')||8);
    return json(res,200,{ok:true,total:17,stats:(simulatedJ1708?.j1708?.requests?.stats || []).slice(offset,offset+limit)});
  }
  if(req.url==='/api/j1708/requests' && req.method==='POST'){
    const update=JSON.parse(body.toString()), status=simulatedJ1708.j1708.requests;
    if(update.action==='arm'){assert.equal(update.authorized,true);assert.equal(update.stationary,true);status.armed=true;status.single_shot=true;}
    if(update.action==='start_field'){
      assert.equal(update.authorized,true);assert.equal(update.stationary,true);assert.equal(update.topology_confirmed,true);
      assert.equal(update.external_capture_verified,undefined);assert.equal(update.tx_observed_hex,undefined);
      status.physical_validated=false;status.work_evidence_ready=false;status.single_shot=false;status.armed=true;
      status.field_validation={phase:'PASSIVE_BEFORE',running:true,confirmed:false,session_id:'42:2000000',completed_trials:0,total_trials:51,contrasting_pids:0};
    }
    if(update.action==='confirm_field'){
      assert.equal(update.authorized,true);assert.equal(update.stationary,true);assert.equal(update.indirect_validation_acknowledged,true);
      assert.equal(update.session_id,'42:2000000');assert.equal(update.external_capture_verified,undefined);
      assert.equal(update.tx_observed_hex,undefined);assert.equal(update.response_observed_hex,undefined);
      assert.equal(status.field_validation.phase,'SATISFACTORY_INDIRECT');
      status.field_validation.confirmed=true;status.field_validation.phase='OPERATOR_CONFIRMED_INDIRECT';status.work_evidence_ready=true;
    }
    if(update.action==='confirm_physical'){
      assert.equal(update.external_capture_verified,true);assert.equal(update.local_mid_free_verified,true);assert.equal(update.trial_id,'42:1005000');
      assert.equal(update.tx_observed_hex,'F2 80 1A 80 F4');assert.equal(update.response_observed_hex,'80 1A 01 65');status.physical_validated=true;status.work_evidence_ready=true;
    }
    if(update.action==='enable_work'){assert(status.physical_validated || status.field_validation?.confirmed);status.work_mode=true;status.armed=true;}
    if(update.action==='stop'){status.work_mode=false;status.armed=false;}
    return json(res,200,{ok:true});
  }
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
  const browser=await chromium.launch({executablePath:process.env.TB_CHROMIUM_PATH || (fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
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
    await check('J1708 timeout and validated source MIDs submit numeric configuration',async()=>{
      const sent=await acceptedConfig(page,async()=>{
        await page.locator('[name=j1708_timeout_ms]').fill('700');
        await page.locator('[name=j1587_odometer_mid]').fill('128');
        await page.locator('[name=j1587_fuel250_mid]').fill('128');
      });
      assert.equal(sent.j1708_timeout_ms,700);assert.equal(sent.j1587_odometer_mid,128);assert.equal(sent.j1587_fuel250_mid,128);
    });
    await check('J1708 one-shot, external evidence gate, explicit travel activation and stop',async()=>{
      simulatedJ1708={speedKph:0,uptime_ms:65000,boot_id:42,reset_reason:1,capture_profile:'J1708_ONLY',can:{enabled:false},
        storage:{ready:true,phase:'NORMAL',pending_known:true,pending_records:150,space_total_bytes:1966080,space_available_bytes:1000000,offline_estimate_seconds:7200,last_ack_seq:7,recovered_records:3,isolated_files:1,corrupt_records:1},
        bridge:{last_cloud_ack_age_ms:3000},telemetry_v2:{latest_readings:65},
        j1708:{ready:true,frames_per_minute:3600,observed_mids:[128,130,140,144],requests:{sent:0,echo_ok:0,responses:0,single_shot:false,armed:false,physical_validated:false,stats:[]}}};
      await page.locator('#refresh').click();await page.waitForTimeout(150);
      await page.locator('#j1708-start-pid').selectOption('26');await page.locator('#j1708-local-mid').fill('242');await page.locator('#j1708-topology').check();assert.equal(await page.locator('#j1708-work').isDisabled(),true);
      await page.locator('#j1708-test').click();
      await page.waitForFunction(()=>document.querySelector('#j1708-message').textContent.includes('Uma requisição autorizada'));
      Object.assign(simulatedJ1708.j1708.requests,{armed:false,sent:1,echo_ok:1,responses:1,trial_id:'42:1005000'});
      await page.locator('#refresh').click();await page.waitForTimeout(150);assert.equal(await page.locator('#j1708-work').isDisabled(),true);
      await page.locator('#j1708-tx-proof').fill('F2 80 1A 80 F4');await page.locator('#j1708-rx-proof').fill('80 1A 01 65');await page.locator('#j1708-external-proof').check();
      await page.locator('#j1708-confirm').click();await page.waitForFunction(()=>document.querySelector('#j1708-message').textContent.includes('Validação externa'));
      await page.locator('#j1708-work').click();await page.waitForFunction(()=>document.querySelector('#j1708-message').textContent.includes('Modo de viagem ativado'));
      assert.match(await page.locator('#diag-can').textContent(),/Desativada no perfil J1708_ONLY/);
      await page.locator('#j1708-stop').click();await page.waitForFunction(()=>document.querySelector('#j1708-message').textContent.includes('Parada solicitada'));
    });
    await check('supervised field path requires parked authorization and separate indirect confirmation; external fields remain empty',async()=>{
      await page.locator('#j1708-tx-proof').fill('');await page.locator('#j1708-rx-proof').fill('');await page.locator('#j1708-external-proof').uncheck();
      await page.locator('#refresh').click();await page.waitForTimeout(150);
      assert.equal(await page.locator('#j1708-field-start').isDisabled(),true);
      await page.locator('#j1708-field-authorize').check();
      await page.locator('#j1708-field-start').click();await page.locator('#j1708-message').filter({hasText:'supervisionada iniciada'}).waitFor();
      // Command acknowledgement precedes its diagnostic refresh. Wait for
      // the actual new phase, not a 150 ms guess or the earlier external proof.
      await page.locator('#j1708-field-status').filter({hasText:'Medindo tráfego passivo anterior'}).waitFor();
      assert.equal(await page.locator('#j1708-work').isDisabled(),true);assert.equal(await page.locator('#j1708-field-confirm').isDisabled(),true);
      Object.assign(simulatedJ1708.j1708.requests,{armed:false});
      Object.assign(simulatedJ1708.j1708.requests.field_validation,{phase:'INCONCLUSIVE',running:false,completed_trials:51});
      await page.locator('#refresh').click();await page.waitForTimeout(150);await page.locator('#j1708-field-ack').check();
      assert.equal(await page.locator('#j1708-field-confirm').isDisabled(),true);assert.equal(await page.locator('#j1708-work').isDisabled(),true);
      Object.assign(simulatedJ1708.j1708.requests.field_validation,{phase:'SATISFACTORY_INDIRECT',contrasting_pids:17});
      await page.locator('#refresh').click();await page.waitForTimeout(150);
      await page.locator('#j1708-field-confirm').click();await page.locator('#j1708-message').filter({hasText:'operacional indireto confirmado'}).waitFor();
      assert.equal(simulatedJ1708.j1708.requests.physical_validated,false);
      assert.equal(await page.locator('#j1708-tx-proof').inputValue(),'');assert.equal(await page.locator('#j1708-rx-proof').inputValue(),'');
      assert.equal(await page.locator('#j1708-external-proof').isChecked(),false);
      await page.locator('#j1708-work').click();await page.locator('#j1708-message').filter({hasText:'Modo de viagem ativado'}).waitFor();
      await page.locator('#j1708-stop').click();await page.locator('#j1708-message').filter({hasText:'Parada solicitada'}).waitFor();
    });
    await check('compact diagnostics paginate all 17 PID statistics without the large full-state response',async()=>{
      simulatedJ1708.j1708.requests.stats=Array.from({length:17},(_,i)=>({pid:i+1,target_mid:128,interval_ms:60000,sent:i,response_candidates:0,timeouts:0}));
      await page.waitForFunction(()=>!document.querySelector('#refresh').disabled);
      const before=requests.length;
      const lastPage=page.waitForResponse(r=>r.url().endsWith('/api/j1708/requests?offset=16&limit=4'));
      await page.locator('#refresh').click();await lastPage;await page.locator('#j1708-cadences').filter({hasText:'PID 17 / MID 128'}).waitFor();
      assert(recent('GET','/api/state?compact=1'));
      for(const offset of [0,4,8,12,16])assert(requests.slice(before).some(r=>r.url==='/api/j1708/requests?offset='+offset+'&limit=4'));
      assert(!requests.slice(before).some(r=>r.url==='/api/state'));
    });
    await check('latched RX fault explains the stop and blocks new TX controls while manual stop stays available',async()=>{
      simulatedJ1708.j1708.requests.rx_guard={fault_latched:true,last_cause:'RX_ERROR_RATE_PERSISTENT',decision_uptime_ms:60000};
      await page.locator('#refresh').click();await page.locator('#j1708-block').filter({hasText:'reconfiguração e nova autorização'}).waitFor();
      for(const id of ['j1708-test','j1708-field-start','j1708-confirm','j1708-work'])assert.equal(await page.locator('#'+id).isDisabled(),true);
      assert.equal(await page.locator('#j1708-stop').isDisabled(),false);
    });
    await check('RAM failure shows partial health, preserves OTA access and stop, and never enables a stale active test',async()=>{
      diagnosticOOM=true;await page.locator('#refresh').click();
      await page.locator('#connection').filter({hasText:'Online · diagnóstico parcial'}).waitFor();
      assert.match(await page.locator('#state-message').textContent(),/RAM/);
      assert.match(await page.locator('#diag-store').textContent(),/temporariamente indisponível/);
      assert.equal(await page.locator('#j1708-test').isDisabled(),true);
      assert.equal(await page.locator('#j1708-work').isDisabled(),true);
      assert.equal(await page.locator('#j1708-stop').isDisabled(),false);
      assert.equal(await fieldsetDisabled(page,'#ota-fields'),false);
      await page.locator('#j1708-stop').click();
      await page.locator('#j1708-message').filter({hasText:'Parada solicitada'}).waitFor();
      diagnosticOOM=false;await page.locator('#refresh').click();
      await page.locator('#connection').filter({hasText:/^Online$/}).waitFor();
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
      assert.equal(recent('GET','/api/state?compact=1').headers.authorization,undefined);
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
