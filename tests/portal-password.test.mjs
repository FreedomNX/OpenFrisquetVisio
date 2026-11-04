import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/settings.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const fields = { portalPassword: { value: '' }, portalPasswordConfirm: { value: '' } };
const context = vm.createContext({
  document: { querySelector: selector => fields[selector.slice(1)] }, TextEncoder,
});
vm.runInContext(script.slice(0, script.indexOf('    async function saveConfig')), context);

function passwordChange(password, confirmation) {
  fields.portalPassword.value = password;
  fields.portalPasswordConfirm.value = confirmation;
  const values = new Map();
  const changed = context.appendPortalPassword({ append: (key, value) => values.set(key, value) });
  return { changed, values };
}

test('empty fields preserve the password and omit it from the request', () => {
  const { changed, values } = passwordChange('', '');
  assert.equal(changed, false);
  assert.equal(values.size, 0);
});

test('new password needs matching confirmation and is never trimmed', () => {
  for (const pair of [['', 'admin'], ['admin', ''], ['abc', 'abcd']]) {
    assert.throws(() => passwordChange(...pair), /identiques/);
  }
  const { changed, values } = passwordChange(' avec espaces ', ' avec espaces ');
  assert.equal(changed, true);
  assert.equal(values.get('portalPassword'), ' avec espaces ');
  assert.equal(values.get('portalPasswordConfirm'), ' avec espaces ');
});

test('64-byte limit is checked in UTF-8, including multibyte passwords', () => {
  for (const password of ['a'.repeat(64), 'é'.repeat(32)]) {
    assert.equal(passwordChange(password, password).changed, true);
  }
  for (const password of ['a'.repeat(65), 'é'.repeat(33), 'nul\0byte']) {
    assert.throws(() => passwordChange(password, password), /invalide/);
  }
});

test('dashboard warns while the default password is in use', () => {
  const dashboard = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
  const dashboardScript = dashboard.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = new Map();
  const dashboardContext = vm.createContext({
    document: {
      querySelectorAll() { return []; },
      querySelector(selector) {
        if (!elements.has(selector)) elements.set(selector, { classList: { remove() {} } });
        return elements.get(selector);
      },
    },
  });
  vm.runInContext(dashboardScript.slice(0, dashboardScript.indexOf('    async function loadDashboard')), dashboardContext);
  for (const [defaultPortalPassword, hidden] of [[true, false], [false, true], [undefined, true]]) {
    dashboardContext.render({ defaultPortalPassword });
    assert.equal(elements.get('#defaultPasswordNotice').hidden, hidden);
  }
  assert.match(dashboard, /<div class="notice" id="defaultPasswordNotice" role="alert" hidden>.*href="\/settings#system"/);
  const login = readFileSync(new URL('../web/login.html', import.meta.url), 'utf8');
  assert.doesNotMatch(login, /defaultPasswordNotice|Mot de passe par défaut/);
});

test('login uses a password only and settings never prefill it from configuration', () => {
  const login = readFileSync(new URL('../web/login.html', import.meta.url), 'utf8');
  assert.match(login, /action="\/login" method="post"/);
  assert.match(login, /name="password" type="password"/);
  assert.doesNotMatch(login, /name="(?:user|username|login)"/);
  assert.doesNotMatch(html.match(/const fieldIds=([^;]+)/)[1], /portalPassword/);
  assert.match(html, /id="portalPassword"[^>]*autocomplete="new-password"/);
});

async function settingsForm(config = {}) {
  const elements = new Map();
  for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
    elements.set('#' + match[1], {
      value: '', checked: false, disabled: /\bdisabled\b/.test(match[0]),
      type: match[0].match(/\btype="([^"]+)"/)?.[1] || 'text',
      textContent: '', style: {}, listeners: {},
      addEventListener(event, callback) { this.listeners[event] = callback; },
    });
  }
  const requests = [];
  const settings = vm.createContext({
    document: { querySelector: selector => elements.get(selector), querySelectorAll: () => [] },
    TextEncoder, URLSearchParams, history: { replaceState() {} }, setTimeout() {},
    fetch: async (url, options) => {
      assert.equal(url, '/api/config');
      if (options.method === 'POST') {
        // Round-trip the actual form encoding, including spaces, quotes, +, & and %.
        requests.push(new URLSearchParams(options.body.toString()));
        return { ok: true, json: async () => ({ ok: true }) };
      }
      return { ok: true, json: async () => config };
    },
  });
  vm.runInContext(script.slice(0, script.indexOf('    const initial=')), settings);
  await settings.loadConfig();
  return {
    field: id => elements.get('#' + id), requests,
    async save() {
      await elements.get('#configForm').listeners.submit({ preventDefault() {} });
      return requests.at(-1);
    },
    clear(id, checked = true) {
      const checkbox = elements.get('#' + id + 'Clear');
      checkbox.checked = checked;
      checkbox.listeners.change();
    },
  };
}

test('opening and saving settings never reads back or submits stored network passwords', async () => {
  const form = await settingsForm({
    wifiHostname: 'device', wifiPassSet: true, mqttPassSet: true,
    // Even an older response containing credentials must not fill the inputs.
    wifiPass: 'saved wifi secret', mqttPass: 'saved mqtt secret',
  });
  for (const id of ['wifiPass', 'mqttPass']) {
    assert.equal(form.field(id).value, '');
    assert.equal(form.field(id + 'Clear').checked, false);
    assert.equal(form.field(id + 'Status').textContent, 'Un mot de passe est enregistré.');
    assert.match(html, new RegExp(`id="${id}"[^>]*autocomplete="new-password"`));
  }
  const untouched = await form.save();
  assert.equal(untouched.has('wifiPass'), false);
  assert.equal(untouched.has('mqttPass'), false);
  form.field('wifiHostname').value = 'renamed';
  const unrelated = await form.save();
  assert.equal(unrelated.get('wifiHostname'), 'renamed');
  assert.equal(unrelated.has('wifiPass'), false);
  assert.equal(unrelated.has('mqttPass'), false);
});

for (const [id, other] of [['wifiPass', 'mqttPass'], ['mqttPass', 'wifiPass']]) {
  test(`${id} replacements preserve exact characters and leave ${other} unchanged`, async () => {
    const form = await settingsForm({ wifiPassSet: true, mqttPassSet: true });
    for (const password of [' new "quoted" \'secret\' +&%=\\é ', '   ', '********']) {
      form.field(id).value = password;
      const data = await form.save();
      assert.equal(data.get(id), password);
      assert.equal(data.has(other), false);
    }
    // Erasing a draft replacement returns to retaining the saved credential.
    form.field(id).value = '';
    assert.equal((await form.save()).has(id), false);
  });

  test(`${id} clears only on explicit selection and can be cancelled`, async () => {
    const form = await settingsForm({ wifiPassSet: true, mqttPassSet: true });
    form.clear(id);
    assert.equal(form.field(id).disabled, true);
    let data = await form.save();
    assert.equal(data.get(id), '');
    assert.equal(data.has(other), false);
    form.clear(id, false);
    assert.equal(form.field(id).disabled, false);
    assert.equal((await form.save()).has(id), false);
    form.field(id).value = 'pending replacement';
    form.clear(id);
    assert.equal((await form.save()).get(id), '');
    form.clear(id, false);
    assert.equal((await form.save()).get(id), 'pending replacement');
    form.clear(id);
    form.field(other).value = ' other "replacement" ';
    data = await form.save();
    assert.equal(data.get(id), '');
    assert.equal(data.get(other), ' other "replacement" ');
  });
}

test('empty credentials stay empty and both credentials can be changed together', async () => {
  const form = await settingsForm({ wifiPassSet: false, mqttPassSet: false });
  for (const id of ['wifiPass', 'mqttPass']) {
    assert.equal(form.field(id + 'Status').textContent, 'Aucun mot de passe enregistré.');
    assert.equal(form.field(id).value, '');
  }
  let data = await form.save();
  assert.equal(data.has('wifiPass'), false);
  assert.equal(data.has('mqttPass'), false);
  form.field('wifiPass').value = 'new wifi password';
  form.field('mqttPass').value = 'new mqtt password';
  data = await form.save();
  assert.equal(data.get('wifiPass'), 'new wifi password');
  assert.equal(data.get('mqttPass'), 'new mqtt password');
  form.clear('wifiPass');
  form.clear('mqttPass');
  data = await form.save();
  assert.equal(data.get('wifiPass'), '');
  assert.equal(data.get('mqttPass'), '');
});
