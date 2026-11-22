import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

function dashboard(){
  const html=readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
  const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements=new Map();
  const context=vm.createContext({document:{querySelectorAll:()=>[],querySelector(selector){
    if(!elements.has(selector))elements.set(selector,{classList:{remove(){}}});
    return elements.get(selector);
  }}});
  vm.runInContext(script.slice(0,script.indexOf('    async function loadDashboard')),context);
  return {render:(data,previous)=>context.render({boiler:{monthlyConsumption:data,previousYearConsumption:previous}}),
    releve:boiler=>{context.render({boiler});return elements.get('#gasPeriod').textContent;},
    previousTable:()=>elements.get('#previousYearConsumptionTable'),
    previousRows:()=>elements.get('#previousYearConsumptionRows').innerHTML,
    previousStatus:()=>elements.get('#previousYearConsumptionStatus').textContent,
    table:()=>elements.get('#monthlyConsumptionTable'),
    rows:()=>elements.get('#monthlyConsumptionRows').innerHTML,
    status:()=>elements.get('#monthlyConsumptionStatus').textContent};
}

test('les consommations absentes restent inconnues, les zéros sont affichés',()=>{
  const page=dashboard();
  for(const data of [undefined,null,{}, {heating:[],hotWater:[]}]){
    page.render(data);assert.equal(page.table().hidden,true);
    assert.equal(page.rows(),'');assert.match(page.status(),/En attente/);
  }
  page.render({heating:Array(12).fill(0),hotWater:Array(12).fill(0),year:null,month:null,receivedAt:null});
  assert.equal(page.table().hidden,false);
  assert.equal((page.rows().match(/<td>0<\/td>/g)||[]).length,24);
  assert.equal((page.rows().match(/class="partial"/g)||[]).length,12);
  assert.equal(page.status(),'Date de réception indisponible · période non confirmée');
});

test('un relevé de décembre conserve les douze mois, leur ordre et leur année',()=>{
  const page=dashboard();
  const data={heating:Array.from({length:12},(_,i)=>1000+i),hotWater:Array.from({length:12},(_,i)=>10+i),
    year:2025,month:12,receivedAt:'31/12/2025 23:50:00'};
  page.render(data);
  const rows=[...page.rows().matchAll(/<tr[^>]*><th scope="row">([^<]+)(?:<span[^>]*>Partiel<\/span>)?<\/th><td>(\d+)<\/td><td>(\d+)<\/td><\/tr>/g)];
  assert.equal(rows.length,12);
  assert.equal(rows[0][1],'Janvier');assert.equal(rows[11][1],'Décembre');
  rows.forEach((row,i)=>{assert.equal(Number(row[2]),1000+i);assert.equal(Number(row[3]),10+i);});
  assert.match(page.rows(),/<tr class="partial"><th scope="row">Décembre/);
  assert.equal(page.status(),'Année 2025 · Relevé du 31/12/2025 23:50:00 (heure chaudière)');
  // Le navigateur n'utilise pas son année courante pour réétiqueter le relevé.
  page.render(data);assert.match(page.status(),/^Année 2025/);
  page.render(null);assert.equal(page.table().hidden,true);assert.equal(page.rows(),'');
});

test('novembre reste partiel, décembre est omis et les vrais zéros restent affichés',()=>{
  const page=dashboard();
  const data={heating:[2100,1850,1200,700,250,80,0,0,120,720,950,0],
    hotWater:[110,105,100,95,90,85,80,82,90,100,65,0],
    year:2026,month:11,receivedAt:'16/11/2026 11:50:00'};
  page.render(data);
  assert.equal((page.rows().match(/<tr/g)||[]).length,11);
  assert.doesNotMatch(page.rows(),/Décembre/);
  assert.match(page.rows(),/Juillet<\/th><td>0<\/td><td>80<\/td>/);
  assert.match(page.rows(),/Août<\/th><td>0<\/td><td>82<\/td>/);
  assert.match(page.rows(),/<tr class="partial"><th scope="row">Novembre<span[^>]*>Partiel<\/span><\/th><td>950<\/td><td>65<\/td>/);
  assert.equal((page.rows().match(/class="partial"/g)||[]).length,1);
  // La date du navigateur ne doit pas transformer un ancien relevé en mois complets.
  page.render({...data,year:2024,receivedAt:'16/11/2024 11:50:00'});
  assert.match(page.status(),/^Année 2024/);
  assert.doesNotMatch(page.rows(),/Décembre/);
  assert.match(page.rows(),/<tr class="partial"><th scope="row">Novembre/);
});

test('la limite suit le mois reçu, y compris janvier et des cases futures non nulles',()=>{
  const page=dashboard();
  for(let month=1;month<=12;month++){
    page.render({heating:Array(12).fill(10),hotWater:Array(12).fill(20),
      year:2025,month,receivedAt:'Relevé de test'});
    assert.equal((page.rows().match(/<tr/g)||[]).length,month);
    assert.equal((page.rows().match(/class="partial"/g)||[]).length,1);
  }
});

test('l’année précédente affiche décembre et ne rend aucun mois partiel',()=>{
  const page=dashboard();
  const current={heating:Array(12).fill(10),hotWater:Array(12).fill(20),
    year:2026,month:11,complete:false,receivedAt:'16/11/2026 11:50:00'};
  const previous={heating:Array.from({length:12},(_,i)=>2000+i),hotWater:Array(12).fill(30),
    year:2025,month:12,complete:true,receivedAt:'16/11/2026 11:50:00'};
  page.render(current,previous);
  assert.equal((page.rows().match(/<tr/g)||[]).length,11);
  assert.match(page.rows(),/Novembre.*Partiel/);
  assert.equal(page.previousTable().hidden,false);
  assert.equal((page.previousRows().match(/<tr/g)||[]).length,12);
  assert.match(page.previousRows(),/Décembre<\/th><td>2011<\/td><td>30<\/td>/);
  assert.doesNotMatch(page.previousRows(),/partial|Partiel/);
  assert.match(page.previousStatus(),/^Année 2025/);
  page.render(current,null);
  assert.equal(page.table().hidden,false);
  assert.equal(page.previousTable().hidden,true);
  assert.equal(page.previousRows(),'');
  assert.match(page.previousStatus(),/En attente.*année précédente/);
  // Une archive seule reste visible, sans inventer les valeurs de l’année courante.
  page.render(null,previous);
  assert.equal(page.table().hidden,true);
  assert.equal(page.previousTable().hidden,false);
});

test('le navigateur ne redéfinit pas l’année archivée et ne date pas une archive inconnue',()=>{
  const page=dashboard();
  const previous={heating:Array(12).fill(0),hotWater:Array(12).fill(0),
    year:2020,month:12,complete:true,receivedAt:'01/01/2021 00:02:00'};
  page.render(null,previous);
  assert.match(page.previousStatus(),/^Année 2020/);
  assert.doesNotMatch(page.previousRows(),/Partiel/);
  page.render(null,{...previous,year:null,month:null,receivedAt:null});
  assert.equal(page.previousStatus(),'Date de réception indisponible · période non confirmée');
  assert.equal((page.previousRows().match(/class="partial"/g)||[]).length,12);
});


test('le jour de consommation vient de la lecture chaudière, y compris au nouvel an',()=>{
  const page=dashboard();
  assert.match(page.releve({estimatedConsumptionDay:20818}),/31\/12\/2026/);
  assert.match(page.releve({estimatedConsumptionDay:null}),/date indéterminée/);
});
