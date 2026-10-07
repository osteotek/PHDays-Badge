const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {test} = require('node:test');

const html = fs.readFileSync(new URL('../main/ui/wifi.html', `file://${__filename}`), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

async function page(state) {
  const elements = Object.fromEntries(['status', 'message', 'save', 'settings'].map(id => [id, {
    textContent: '', disabled: true, children: [], appendChild(child) { this.children.push(child); }
  }]));
  const form = elements.settings;
  form.elements = {ssid: {value: ''}, password: {value: ''}};
  form.addEventListener = (_, handler) => { form.submit = handler; };
  const requests = [];
  const context = vm.createContext({
    document: {getElementById: id => elements[id], createElement: () => ({})},
    TextEncoder, setInterval() {},
    async fetch(path, options) {
      requests.push({path, options});
      return {ok: true, json: async () => state};
    }
  });
  vm.runInContext(script, context);
  await new Promise(resolve => setImmediate(resolve));
  return {elements, form, requests, context};
}

test('home-network access shows the assigned IP and disables credential changes', async () => {
  const {elements, requests} = await page({connected: true, ssid: 'Test network', ip: '192.168.1.42', can_configure: false});
  assert.equal(elements.status.children[0].href, 'http://192.168.1.42/');
  assert.equal(elements.save.disabled, true);
  assert.match(elements.message.textContent, /hotspot/);
  assert.equal(requests[0].path, '/api/v1/wifi');
});

test('hotspot form saves through the current host, clears the password and explains restart', async () => {
  const {elements, form, context} = await page({connected: false, ssid: '', can_configure: true});
  assert.equal(elements.save.disabled, false);
  form.elements.ssid.value = 'Test network';
  form.elements.password.value = 'example-password';
  context.fetch = async (path, options) => {
    assert.equal(path, '/api/v1/wifi');
    assert.equal(options.method, 'POST');
    assert.deepEqual(JSON.parse(options.body), {ssid: 'Test network', password: 'example-password'});
    return {ok: true};
  };
  await form.submit({preventDefault() {}});
  assert.equal(form.elements.password.value, '');
  assert.equal(elements.save.disabled, true);
  assert.match(elements.message.textContent, /restarting/);
});

test('multibyte SSIDs over the ESP32 byte limit are rejected before sending', async () => {
  const {elements, form, requests} = await page({connected: false, ssid: '', can_configure: true});
  form.elements.ssid.value = 'я'.repeat(17);
  await form.submit({preventDefault() {}});
  assert.equal(requests.length, 1);
  assert.match(elements.message.textContent, /32 bytes/);
});

test('a save error preserves the entered password and allows retry', async () => {
  const {elements, form, context} = await page({connected: false, ssid: '', can_configure: true});
  form.elements.ssid.value = 'Test network';
  form.elements.password.value = 'example-password';
  context.fetch = async () => ({ok: false, text: async () => 'Could not save Wi-Fi settings'});
  await form.submit({preventDefault() {}});
  assert.equal(form.elements.password.value, 'example-password');
  assert.equal(elements.save.disabled, false);
  assert.equal(elements.message.textContent, 'Could not save Wi-Fi settings');
});
