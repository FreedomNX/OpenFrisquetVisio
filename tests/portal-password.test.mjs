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
