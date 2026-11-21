import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { test } from 'node:test';
import assert from 'node:assert/strict';

const html = readFileSync(new URL('../web/settings.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
function fixture(config = {}) {
  const elements = {};
  const field = id => elements[id] ??= {value: '', checked: false, disabled: false, style: {}, hidden: true,
    type: ['wifiStatic', 'developerMode'].includes(id) ? 'checkbox' : 'text'};
  let sent;
  const context = vm.createContext({
    document: {querySelector: selector => field(selector.slice(1)), querySelectorAll: () => []},
    URLSearchParams, TextEncoder, setTimeout() {},
    fetch: async (_url, options) => {
      if (options?.method === 'POST') {
        sent = new URLSearchParams(options.body);
        return {ok: true, json: async () => ({ok: true})};
      }
      return {ok: true, json: async () => config};
    },
  });
  vm.runInContext(script.slice(0, script.indexOf('    async function action(')), context);
  return {context, field, payload: () => sent};
}

test('all four outdoor probe choices survive the actual page save/load functions', async () => {
  for (const mode of ['0', '1', '2', '3']) {
    const saved = fixture();
    saved.field('useSondeMode').value = mode;
    await saved.context.saveConfig({preventDefault() {}});
    const params = saved.payload();
    assert.ok(params, 'configuration posted');
    const config = Object.fromEntries(['useSondeExt', 'useDS18B20', 'useSondeExistante']
      .map(key => [key, params.get(key) === 'true']));
    assert.equal(config.useSondeExistante, mode === '1');
    if (mode === '1') {
      assert.equal(config.useSondeExt, false); // A real probe must not enable a radio emulator.
      assert.equal(config.useDS18B20, false);
    }
    const reloaded = fixture(config);
    await reloaded.context.loadConfig();
    assert.equal(reloaded.field('useSondeMode').value, mode);
    assert.equal(reloaded.field('btnPairSondeExt').style.display, mode === '2' || mode === '3' ? 'inline-flex' : 'none');
  }
});
test('older configuration responses still restore disabled, virtual and DS18B20 probes', async () => {
  for (const [config, expected] of [[{}, '0'], [{useSondeExt: true}, '2'], [{useSondeExt: true, useDS18B20: true}, '3']]) {
    const page = fixture(config);
    await page.context.loadConfig();
    assert.equal(page.field('useSondeMode').value, expected);
  }
});
