import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/settings.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const fields = {};
for (const id of ['wifiPass', 'mqttPass']) {
  fields[id] = { value: '', placeholder: '' };
  fields[id + 'Clear'] = { checked: false };
  fields[id + 'ClearField'] = { hidden: true };
}
const context = vm.createContext({
  document: { querySelector: selector => fields[selector.slice(1)] }, TextEncoder,
});
vm.runInContext(script.slice(0, script.indexOf('    async function saveConfig')), context);

function secrets(values, clear = {}) {
  for (const id of ['wifiPass', 'mqttPass']) {
    fields[id].value = values[id] ?? '';
    fields[id + 'Clear'].checked = clear[id] ?? false;
  }
  const sent = new Map();
  context.appendNetworkSecrets({ append: (key, value) => sent.set(key, value) });
  return sent;
}

test('stored passwords are never shown, only reported as configured', () => {
  fields.wifiPass.value = 'saisie précédente';
  context.showNetworkSecrets({ wifiPassConfigured: true, mqttPassConfigured: false });
  assert.equal(fields.wifiPass.value, '');
  assert.match(fields.wifiPass.placeholder, /Enregistré/);
  assert.equal(fields.wifiPassClearField.hidden, false);
  assert.equal(fields.mqttPass.placeholder, '');
  assert.equal(fields.mqttPassClearField.hidden, true);
  // An older firmware returning the password itself does not fill the field.
  context.showNetworkSecrets({ wifiPass: 'secret', mqttPass: 'secret' });
  assert.equal(fields.wifiPass.value, '');
  assert.equal(fields.mqttPass.value, '');
});

test('empty fields keep the stored password, a new value replaces it', () => {
  assert.equal(secrets({}).size, 0);
  const sent = secrets({ wifiPass: ' avec espaces ', mqttPass: 'broker' });
  assert.equal(sent.get('wifiPass'), ' avec espaces ');
  assert.equal(sent.get('mqttPass'), 'broker');
  assert.equal(sent.size, 2);
});

test('clearing is explicit and a new value takes precedence', () => {
  const cleared = secrets({}, { wifiPass: true, mqttPass: true });
  assert.deepEqual([...cleared], [['wifiPassClear', 'true'], ['mqttPassClear', 'true']]);
  const replaced = secrets({ mqttPass: 'nouveau' }, { mqttPass: true });
  assert.deepEqual([...replaced], [['mqttPass', 'nouveau']]);
});

test('settings wire the helpers and never prefill the passwords', () => {
  const ids = html.match(/const fieldIds=([^;]+)/)[1];
  assert.doesNotMatch(ids, /wifiPass|mqttPass/);
  assert.match(script, /async function loadConfig\(\)[^\n]*showNetworkSecrets\(data\)/);
  assert.match(script, /async function saveConfig\(event\)[^\n]*appendNetworkSecrets\(data\)/);
  for (const id of ['wifiPass', 'mqttPass']) {
    assert.match(html, new RegExp(`id="${id}" type="password" autocomplete="new-password"`));
    assert.match(html, new RegExp(`id="${id}ClearField" hidden><input id="${id}Clear" type="checkbox">`));
  }
});
