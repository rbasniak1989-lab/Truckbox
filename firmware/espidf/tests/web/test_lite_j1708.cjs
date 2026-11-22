'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, '../../components/tb_web/index.html'), 'utf8');
function panelCode(start, next) {
  const begin = html.indexOf('  function ' + start + '(');
  const end = html.indexOf(next, begin);
  assert(begin >= 0 && end > begin);
  return html.slice(begin, end);
}

const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {dataset: {}, value: '', textContent: ''});
  return elements.get(id);
}

const requests = {
  sent_total: 3, answered_total: 2, timeout_total: 1,
  echo_ok: 3, collisions: 0, echo_timeouts: 0,
  hardware_tx_verified: false, armed: false,
  automatic: {state: 'DISABLED', reason: 'PERSISTENT_OPERATOR_DISABLE',
    technical_hold_mid: 172, technical_hold_persisted: true}
};
const lastState = {
  core_lite: true, live: {speedKph: 0}, ota_busy: false,
  j1708: {ready: true, observed_mids: [128, 130], requests}
};
const context = vm.createContext({lastState, token: 'test', role: 'core', uploadBusy: false,
  j1708Busy: false, stateBusy: false, $: element, text: (id, value) => {element(id).textContent = value;}});
vm.runInContext(panelCode('errorText', '  async function request') +
  panelCode('updateJ1708Panel', '  async function j1708Command'), context);

element('j1708-recover-ack').checked = true;
context.updateJ1708Panel();
assert.equal(element('j1708-sent').textContent, '3');
assert.equal(element('j1708-responses').textContent, '2');
assert.equal(element('j1708-errors').textContent, '0 / 0 / 1');
assert.match(element('j1708-automatic-status').textContent, /confirmação de transmissão pendente/);
assert.equal(element('j1708-automatic-enable').disabled, true);
assert.equal(element('j1708-recover-hold').disabled, false);
assert.match(context.errorText({reason: 'physical_tx_validation_required'}, 'HTTP 409'), /Coleta e armazenamento/);

requests.hardware_tx_verified = true;
const canRecover = () => !element('j1708-recover-hold').disabled;
lastState.live.speedKph = 75.6;
context.updateJ1708Panel();
assert.equal(canRecover(), false);
assert.equal(element('j1708-automatic-enable').disabled, false);

lastState.live.speedKph = 0;
lastState.j1708.observed_mids.push(172);
context.updateJ1708Panel();
assert.equal(canRecover(), false);
console.log('Core Lite panel: compact counters, TX confirmation reason and parked/latest MID interlocks PASS');
