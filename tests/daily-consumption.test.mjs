import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

function dashboard() {
  const html = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = new Map(), requests = [];
  function element(key) {
    if (!elements.has(key)) elements.set(key, {
      options: [], listeners: {}, classList:{remove(){}},
      addEventListener(type, fn) { this.listeners[type] = fn; },
      set innerHTML(html) { this.html = html; this.options = []; },
      get innerHTML() { return this.html; },
      get selectedIndex() { return this.options.findIndex(opt => opt.value === this.value); },
      appendChild(opt) { this.options.push(opt); },
    });
    return elements.get(key);
  }
  const context = vm.createContext({
    document:{hidden:true,querySelector:element,createElement:()=>({}),addEventListener(){}},
    AbortController, setTimeout, clearTimeout,
    fetch:(url,{signal})=>new Promise(resolve=>requests.push({url,signal,resolve})),
  });
  vm.runInContext(script, context);
  return {element, requests, render:data=>context.renderDailyConsumption(data)};
}

const janvier = {month:'2027-01', firstMonth:'2026-11', lastMonth:'2027-01',
  lastDay:20819, days:[[17,0]]};

test('les lignes proviennent du mois sélectionné ; les dates, zéros et lacunes sont conservés',()=>{
  const page=dashboard(), el=page.element;
  page.render({month:null,days:[]});
  assert.equal(el('#dailyNav').hidden,true);
  page.render(janvier);
  assert.equal(el('#dailyMonth').options.map(o=>o.value).join(','),'2027-01,2026-12,2026-11');
  assert.match(el('#dailyMonth').options[1].textContent,/décembre 2026/);
  assert.equal(el('#dailyNext').disabled,true);
  assert.match(el('#dailyRows').innerHTML,/01\/01\/2027<\/th><td>17<\/td><td>0<\/td>/);
  page.render({...janvier,month:'2026-12',lastDay:20818,days:[null,[null,3]]});
  assert.match(el('#dailyRows').innerHTML,/31\/12\/2026<\/th><td>—<\/td><td>—<\/td>/);
  assert.match(el('#dailyRows').innerHTML,/30\/12\/2026<\/th><td>—<\/td><td>3<\/td>/);
  page.render({...janvier,month:'2026-11',days:Array(30).fill(null)});
  assert.equal(el('#dailyPrev').disabled,true);
  assert.equal(el('#dailyNext').disabled,false);
  assert.equal(el('#dailyTable').hidden,true);
  assert.match(el('#dailyStatus').textContent,/Aucun relevé pour ce mois/);
});

test('ouverture au dernier mois, navigation interannuelle et réponse tardive ignorée', async()=>{
  const page=dashboard(), el=page.element, panel=el('#dailyConsumption');
  panel.open=true;
  const ouverture=panel.listeners.toggle();
  assert.equal(page.requests[0].url,'/api/consommation/quotidienne');
  page.requests[0].resolve({ok:true,json:async()=>janvier}); await ouverture;
  const decembre=el('#dailyPrev').listeners.click();
  assert.equal(page.requests[1].url,'/api/consommation/quotidienne?mois=2026-12');
  const reouverture=panel.listeners.toggle();
  assert.ok(page.requests[1].signal.aborted);
  page.requests[2].resolve({ok:true,json:async()=>janvier}); await reouverture;
  page.requests[1].resolve({ok:true,json:async()=>({...janvier,month:'2026-12'})}); await decembre;
  assert.equal(el('#dailyMonth').value,'2027-01');
  el('#dailyMonth').value='2026-11';
  const saut=el('#dailyMonth').listeners.change();
  assert.match(page.requests[3].url,/mois=2026-11$/);
  page.requests[3].resolve({ok:false,status:503,text:async()=>"Partition non vierge illisible. Son contenu a été préservé."}); await saut;
  assert.match(el('#dailyStatus').textContent,/Partition non vierge illisible/);
});
