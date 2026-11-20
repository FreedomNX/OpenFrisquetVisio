import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/settings.html', import.meta.url), 'utf8');
const start = html.indexOf('    const displayFieldIds=');
const end = html.indexOf('    async function loadConfig()', start);
function fixture() {
  const fields = {displaySettings: {hidden: true}};
  for (const id of ['displayEnabled', 'displayBrightness', 'displayTimeout', 'displayFlip', 'displayAutoCycle']) {
    fields[id] = {disabled: true, type: ['displayBrightness', 'displayTimeout'].includes(id) ? 'number' : 'checkbox'};
  }
  const context = vm.createContext({$: selector => fields[selector.slice(1)]});
  vm.runInContext(html.slice(start, end), context);
  return {fields, context};
}
test('screen settings are hidden and never submitted for other boards or older firmware', () => {
  for (const data of [{}, {displaySupported: false}]) {
    const {fields, context} = fixture();
    context.loadDisplaySettings(data);
    assert.equal(fields.displaySettings.hidden, true);
    const sent = new URLSearchParams();
    context.appendDisplaySettings(sent);
    assert.equal(sent.toString(), '');
  }
});
test('Heltec V3 screen settings retain false and zero and are submitted on save', () => {
  const {fields, context} = fixture();
  context.loadDisplaySettings({displaySupported: true, displayEnabled: false, displayBrightness: 96,
    displayTimeout: 0, displayFlip: true, displayAutoCycle: false});
  assert.equal(fields.displaySettings.hidden, false);
  const sent = new URLSearchParams();
  context.appendDisplaySettings(sent);
  assert.equal(sent.get('displayEnabled'), 'false');
  assert.equal(sent.get('displayTimeout'), '0');
  assert.equal(sent.get('displayFlip'), 'true');
  assert.equal(sent.get('displayBrightness'), '96');
  assert.match(html, /loadDisplaySettings\(data\);/);
  assert.match(html, /appendDisplaySettings\(data\);/);
});
