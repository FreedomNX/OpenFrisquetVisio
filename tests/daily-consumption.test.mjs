import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

function dashboard(fetchImpl) {
  const html = readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements = new Map(), requests = [], downloads = [], blobs = new Map();
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
    document:{
      hidden:true, querySelector:element, addEventListener(){}, body:{appendChild(){}},
      createElement:tag=>tag !== 'a' ? {} : {
        click(){ downloads.push({name:this.download, blob:blobs.get(this.href)}); }, remove(){},
      },
    },
    AbortController, Blob, setTimeout, clearTimeout,
    URL:{createObjectURL(blob){blobs.set('blob:essai',blob); return 'blob:essai';}, revokeObjectURL(url){blobs.delete(url);}},
    fetch:fetchImpl ?? ((url,{signal})=>new Promise(resolve=>requests.push({url,signal,resolve}))),
  });
  vm.runInContext(script, context);
  return {element, requests, downloads, render:data=>context.renderDailyConsumption(data)};
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

function archive(mois, version='test-128-17') {
  const debut = '2016-03', fin = '2026-02';
  const date = new Date(mois+'-01T00:00:00Z');
  date.setUTCMonth(date.getUTCMonth()+1,0);
  const days = Array(date.getUTCDate()).fill(null);
  if (mois===debut) days[days.length-1]=[0,null];
  if (mois===fin) days[0]=[null,17];
  return {version,firstMonth:debut,lastMonth:fin,month:mois,lastDay:date.getTime()/86400000,days};
}

test('le CSV contient dix ans par date croissante, avec en-têtes français, zéros et lacunes', async()=>{
  const urls = [];
  const page=dashboard(async url=>{
    urls.push(url);
    const month = url.split('=')[1] || '2026-02';
    return {ok:true,json:async()=>archive(month)};
  });
  await page.element('#dailyExport').listeners.click();
  assert.equal(urls.length,120);
  assert.equal(new Set(urls).size,120);
  assert.equal(page.downloads.length,1);
  const {name,blob}=page.downloads[0];
  assert.equal(name,'consommation-quotidienne_2016-03_2026-02.csv');
  assert.equal(blob.type,'text/csv;charset=utf-8');
  const bytes=Buffer.from(await blob.arrayBuffer());
  assert.deepEqual([...bytes.subarray(0,3)],[0xef,0xbb,0xbf]);
  const rows=bytes.subarray(3).toString('utf8').split('\r\n');
  assert.equal(rows.length,3652+2);
  assert.equal(rows[0],'Date;Chauffage (kWh);Eau chaude (kWh)');
  assert.equal(rows[1],'2016-03-01;0;');
  assert.ok(rows.includes('2024-02-29;;'));
  assert.equal(rows.at(-2),'2026-02-28;;17');
  assert.equal(rows.at(-1),'');
});

test('aucun CSV partiel si le stockage est illisible ou si la collecte change en cours de lecture',async()=>{
  for (const cause of ['stockage','version','vide']) {
    const page=dashboard(async url=>{
      if(cause==='vide')return {ok:true,json:async()=>({month:null})};
      if(!url.includes('?'))return {ok:true,json:async()=>archive('2026-02')};
      if(cause==='stockage')return {ok:false,status:503,text:async()=>'Partition illisible.'};
      return {ok:true,json:async()=>archive('2016-03','autre-version')};
    });
    const bouton=page.element('#dailyExport');
    await bouton.listeners.click();
    assert.equal(page.downloads.length,0);
    assert.equal(bouton.disabled,false);
    assert.match(page.element('#dailyExportStatus').textContent,/Partition illisible|historique a changé|Aucun relevé/);
  }
});
