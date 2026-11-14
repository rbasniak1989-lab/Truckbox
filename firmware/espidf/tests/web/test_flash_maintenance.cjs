/* Real embedded page; loopback fixtures only, never a vehicle or Cloud. */
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),http=require('node:http'),path=require('node:path');
const {chromium}=require('playwright');
const html=fs.readFileSync(path.join(__dirname,'../../components/tb_web/index.html'));
const ADMIN='synthetic_admin_only_for_tests';
let role='core',formatFailure=false,prepareFailure=false,expires=120,wrongIdentity=false;
const operations=[],errors=[],dialogs=[];
const server=http.createServer(async(req,res)=>{
  const parts=[];for await(const chunk of req)parts.push(chunk);
  const body=Buffer.concat(parts).toString();
  const json=(status,data)=>{res.writeHead(status,{'Content-Type':'application/json'});res.end(JSON.stringify(data));};
  if(req.url==='/'){res.writeHead(200,{'Content-Type':'text/html'});return res.end(html);}
  if(req.url==='/api/health')return json(200,{ok:true,role:wrongIdentity?'link':role,boot_id:77,version:'0.2.22-MOCK',device_uid:'MOCK-CORE'});
  // A partial/OOM diagnosis must not permanently disable storage maintenance.
  if(req.url.startsWith('/api/state'))return json(503,{ok:false,error:'Mock full diagnosis unavailable'});
  if(req.headers.authorization!=='Bearer '+ADMIN)return json(401,{ok:false,error:'Mock unauthorized'});
  if(req.url==='/api/config')return json(200,{role,capture_profile:'J1708_ONLY'});
  if(req.url.startsWith('/api/j1708/requests'))return json(200,{ok:true,stats:[]});
  if(req.url==='/api/storage/backup'){
    operations.push('backup');res.writeHead(200,{'Content-Type':'application/octet-stream'});return res.end(Buffer.alloc(0x1e0000));
  }
  if(req.url==='/api/storage/maintenance'){
    const value=JSON.parse(body);operations.push(value);
    assert.equal(value.authorized,true);assert.equal(value.stationary,undefined);
    if(value.action==='prepare'){
      if(prepareFailure)return json(400,{ok:false,error:'Mock maintenance busy'});
      await new Promise(r=>setTimeout(r,50));
      return json(200,{ok:true,challenge:'6'.repeat(32),expires_in_s:expires});
    }
    assert.equal(value.action,'format');assert.equal(value.accept_data_loss,true);
    assert.equal(value.confirmation,'FORMATAR TBFLASH');assert.equal(value.challenge,'6'.repeat(32));
    assert.equal(value.boot_id,77);assert.equal(value.device_uid,'MOCK-CORE');
    return formatFailure?json(503,{ok:false,error:'Mock media failure'}):json(200,{ok:true,message:'Mock format completed; no device changed.'});
  }
  res.writeHead(404);res.end();
});
(async()=>{
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const origin='http://127.0.0.1:'+server.address().port;
  const browser=await chromium.launch({executablePath:process.env.TB_CHROMIUM_PATH || (fs.existsSync('/usr/bin/chromium')?'/usr/bin/chromium':undefined),headless:true,args:['--no-sandbox']});
  try{
    const context=await browser.newContext({acceptDownloads:true});
    await context.route('**/*',route=>route.request().url().startsWith(origin+'/')?route.continue():route.abort());
    const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
    page.on('dialog',d=>{dialogs.push(d.message());d.dismiss();});
    await page.goto(origin);await page.locator('#connection').filter({hasText:'Online'}).waitFor();
    assert(await page.locator('#flash-format').isDisabled());
    await page.locator('#admin-token').fill(ADMIN);await page.locator('#login').click();
    await page.locator('#auth-status').filter({hasText:'Administrador'}).waitFor();
    assert.equal(operations.length,0);assert(!(await page.locator('#flash-format').isDisabled()));
    assert.equal(await page.locator('#flash-phrase,#flash-loss,#flash-parked,#flash-prepare').count(),0);
    // Even immediate repeated clicks only authorize one request pair.
    await page.locator('#flash-format').evaluate(el=>{el.click();el.click();});
    await page.locator('#flash-message').filter({hasText:'Mock format completed'}).waitFor();
    assert.equal(operations.filter(o=>o.action==='prepare').length,1);
    assert.equal(operations.filter(o=>o.action==='format').length,1);assert.deepEqual(dialogs,[]);
    prepareFailure=true;await page.locator('#flash-format').click();
    await page.locator('#flash-message').filter({hasText:'Mock maintenance busy'}).waitFor();
    assert.equal(operations.filter(o=>o.action==='format').length,1);prepareFailure=false;
    expires=0;await page.locator('#flash-format').click();
    await page.locator('#flash-message').filter({hasText:'não liberou a manutenção'}).waitFor();
    assert.equal(operations.filter(o=>o.action==='format').length,1);expires=120;
    await page.waitForFunction(()=>!document.querySelector('#refresh').disabled);
    wrongIdentity=true;const before=operations.length;await page.locator('#flash-format').click();
    await page.waitForFunction(()=>document.querySelector('#flash-message').textContent.includes('identidade do Core'));
    assert.equal(operations.length,before);wrongIdentity=false;
    await page.waitForFunction(()=>!document.querySelector('#refresh').disabled);
    await page.locator('#refresh').click();
    await page.locator('#stat-role').filter({hasText:'Core'}).waitFor();
    await page.locator('#flash-format').waitFor({state:'visible'});
    formatFailure=true;await page.locator('#flash-format').click();
    await page.locator('#flash-message').filter({hasText:'não será repetida automaticamente'}).waitFor();
    await page.waitForTimeout(300);assert.equal(operations.filter(o=>o.action==='format').length,2);
    const download=page.waitForEvent('download');await page.locator('#flash-backup').click();
    await download;await page.locator('#flash-message').filter({hasText:'baixada'}).waitFor();
    assert.equal(operations.at(-1),'backup');assert.deepEqual(dialogs,[]);
    await page.locator('#logout').click();assert(await page.locator('#flash-format').isDisabled());
    role='link';await page.reload();await page.locator('#connection').filter({hasText:'Online'}).waitFor();
    assert(await page.locator('#storage-maintenance').isHidden());assert.deepEqual(errors,[]);
    console.log('Browser maintenance PASS: single click, no phrase/checkbox/dialog, partial diagnostic works, internal nonce/identity, duplicate suppression, no auto-format on load/login, failures without retry, backup/logout/Core-Link guards; loopback fixtures only.');
    await context.close();
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);process.exitCode=1;server.close();});
