import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const html = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];

test('boiler activity and anomaly stay independent on the dashboard', () => {
  const elements = new Map();
  const context = vm.createContext({
    document: {
      querySelectorAll() { return []; },
      querySelector(selector) {
        if (!elements.has(selector)) elements.set(selector, { classList: { remove() {} } });
        return elements.get(selector);
      },
    },
  });
  vm.runInContext(script.slice(0, script.indexOf('    async function loadDashboard')), context);
  for (const state of ['Fonctionnement', 'Chauffage', 'Chauffage ECS', 'Veille', 'Inconnu']) {
    for (const anomaly of [true, false, null]) {
      context.render({ boiler: { enabled: true, state, anomaly, hotWaterMode: 'Eco' } });
      assert.equal(elements.get('#boilerState').textContent, state);
      assert.equal(elements.get('#boilerAnomaly').textContent,
        anomaly == null ? 'Anomalie · inconnue' : anomaly ? 'Anomalie détectée' : 'Aucune anomalie');
      assert.equal(elements.get('#boilerAnomaly').className,
        'status ' + (anomaly == null ? 'warn' : anomaly ? 'bad' : 'ok'));
      assert.equal(elements.get('#hotWaterMode').textContent, 'ECS · Eco');
    }
  }
  assert.doesNotMatch(html, /id="heatingMode"/);
});
