/* Real embedded page, local HTTP fixtures only. No ESP32 or Cloud access. */
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),http=require('node:http'),path=require('node:path');
const {chromium}=require('playwright');
const html=fs.readFileSync(path.join(__dirname,'../../components/tb_web/index.html'));
const ADMIN='synthetic_admin_only_for_tests';
let role='core',formatFailure=false,expires=120,acceptDialog=false;
const operations=[],errors=[];
const server=http.createServer(async(req,res)=>{
  const parts=[];for await(const chunk of req)parts.push(chunk);
  const body=Buffer.concat(parts).toString();
  const json=(status,data)=>{res.writeHead(status,{'Content-Type':'application/json'});res.end(JSON.stringify(data));};
  if(req.url==='/'){res.writeHead(200,{'Content-Type':'text/html'});return res.end(html);}
  if(req.url.startsWith('/api/state'))return json(200,{ok:true,role,boot_id:77,version:'0.2.21-MOCK',device_uid:'MOCK-CORE',
    network:{},speedKph:0,storage:{maintenance:{available:true}},ota_busy:false});
  if(req.headers.authorization!=='Bearer '+ADMIN)return json(401,{ok:false,error:'Mock unauthorized'});
  if(req.url==='/api/config')return json(200,{role,capture_profile:'J1708_ONLY'});
  if(req.url.startsWith('/api/j1708/requests'))return json(200,{ok:true,stats:[]});
  if(req.url==='/api/storage/backup'){
    operations.push('backup');res.writeHead(200,{'Content-Type':'application/octet-stream'});return res.end(Buffer.alloc(0x1e0000));
  }
  if(req.url==='/api/storage/maintenance'){
    const value=JSON.parse(body);operations.push(value);
    assert.equal(value.authorized,true);assert.equal(value.stationary,true);
    if(value.action==='prepare')return json(200,{ok:true,challenge:'6'.repeat(32),expires_in_s:expires});
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
  const browser=await chromium.launch({executablePath:process.env.TB_CHROMIUM_PATH || '/usr/bin/chromium',headless:true,args:['--no-sandbox']});
  try{
    const context=await browser.newContext({acceptDownloads:true});
    await context.route('**/*',route=>route.request().url().startsWith(origin+'/')?route.continue():route.abort());
    const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
    page.on('dialog',d=>acceptDialog?d.accept():d.dismiss());
    await page.goto(origin);await page.locator('#connection').filter({hasText:'Online'}).waitFor();
    assert.equal(await page.locator('#flash-maint-fields').evaluate(e=>e.disabled),true);
    await page.locator('#admin-token').fill(ADMIN);await page.locator('#login').click();
    await page.locator('#auth-status').filter({hasText:'Administrador'}).waitFor();
    await page.locator('#flash-maintenance > summary').click();
    assert.equal(operations.length,0);assert(await page.locator('#flash-prepare').isDisabled());
    await page.locator('#flash-parked').check();
    const download=page.waitForEvent('download');await page.locator('#flash-backup').click();
    await download;await page.locator('#flash-message').filter({hasText:'baixada'}).waitFor();
    assert.deepEqual(operations,['backup']);
    await page.locator('#flash-prepare').click();await page.locator('#flash-message').filter({hasText:'Nada foi apagado'}).waitFor();
    assert.equal(operations.filter(o=>o.action==='format').length,0);
    await page.locator('#flash-loss').check();await page.locator('#flash-phrase').fill('INCORRETO');
    assert(await page.locator('#flash-format').isDisabled());
    await page.locator('#flash-phrase').fill('FORMATAR TBFLASH');
    await page.locator('#flash-format').click(); /* dialog dismissed */
    assert.equal(operations.filter(o=>o.action==='format').length,0);
    acceptDialog=true;await page.locator('#flash-format').click();
    await page.locator('#flash-message').filter({hasText:'Mock format completed'}).waitFor();
    assert.equal(operations.filter(o=>o.action==='format').length,1);
    assert(await page.locator('#flash-format').isDisabled());
    formatFailure=true;await page.locator('#flash-prepare').click();
    await page.locator('#flash-message').filter({hasText:'Nada foi apagado'}).waitFor();
    await page.locator('#flash-loss').check();await page.locator('#flash-phrase').fill('FORMATAR TBFLASH');
    await page.locator('#flash-format').click();await page.locator('#flash-message').filter({hasText:'Não repita automaticamente'}).waitFor();
    await page.waitForTimeout(300);assert.equal(operations.filter(o=>o.action==='format').length,2);
    assert(await page.locator('#flash-format').isDisabled());
    expires=0;await page.locator('#flash-prepare').click();
    await page.locator('#flash-message').filter({hasText:'Nada foi apagado'}).waitFor();
    await page.locator('#flash-loss').check();await page.locator('#flash-phrase').fill('FORMATAR TBFLASH');
    assert(await page.locator('#flash-format').isDisabled());
    await page.locator('#logout').click();assert(await page.locator('#flash-format').isDisabled());
    assert.equal(await page.locator('#flash-phrase').inputValue(),'');
    role='link';await page.reload();await page.locator('#connection').filter({hasText:'Online'}).waitFor();
    assert(await page.locator('#storage-maintenance').isHidden());
    assert.deepEqual(errors,[]);
    console.log('Browser maintenance PASS: no auto-format on load/login, backup, parked/loss/phrase/nonce gates, cancel, one confirmed POST, media error no retry, expiry/logout, Core/Link isolation; fixtures only.');
    await context.close();
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);process.exitCode=1;server.close();});
