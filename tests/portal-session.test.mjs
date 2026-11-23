import {readFileSync,readdirSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const web = name => readFileSync(new URL('../web/'+name,import.meta.url),'utf8');
const flush = async () => { for (let n=0;n<20;n++) await Promise.resolve(); };
// Réponse du serveur à un appel /api/ sans session valide (Portal::requireAuthentication).
const unauthorized = () => {
  const body={ok:false,err:'Authentification requise'};
  return {ok:false,status:401,json:async()=>body,text:async()=>JSON.stringify(body)};
};
// Toutes les pages du portail sauf la page de connexion, qui n’appelle pas l’API.
const pages = readdirSync(new URL('../web/',import.meta.url))
  .filter(file=>file.endsWith('.html') && file!=='login.html').map(file=>file.slice(0,-5)).sort();

function browser() {
  const visits=[],requests=[],window=new EventTarget();
  const context=vm.createContext({window,Event,location:{replace:url=>visits.push(url)},
    fetch:(...args)=>new Promise((resolve,reject)=>requests.push({args,resolve,reject}))});
  vm.runInContext(web('session.js'),context);
  return {visits,requests,window,fetch:(...args)=>vm.runInContext('fetch',context)(...args)};
}
async function outcome(promise) {
  let result={state:'pending'};
  promise.then(value=>result={state:'fulfilled',value},error=>result={state:'rejected',error});
  await flush();return result;
}

test('session : les autres réponses et les erreurs réseau parviennent telles quelles à la page',async()=>{
  const b=browser();
  for(const status of [200,304,400,403,409,500,503]) {
    const options={method:'POST'},response={ok:status<300,status};
    const pending=b.fetch('/api/config',options);
    assert.deepEqual(b.requests.at(-1).args,['/api/config',options]);
    b.requests.at(-1).resolve(response);
    assert.deepEqual(await outcome(pending),{state:'fulfilled',value:response});
  }
  const error=new TypeError('Failed to fetch'),pending=b.fetch('/api/dashboard');
  b.requests.at(-1).reject(error);
  assert.deepEqual(await outcome(pending),{state:'rejected',error});
  assert.deepEqual(b.visits,[],'aucune de ces réponses ne renvoie à la connexion');
});

test('session : une réponse 401 renvoie à la page de connexion sans parvenir à la page',async()=>{
  const b=browser(),pending=b.fetch('/api/dashboard');
  assert.deepEqual(b.visits,[],'pas de redirection avant la réponse');
  b.requests[0].resolve(unauthorized());
  assert.deepEqual(await outcome(pending),{state:'pending'});
  assert.deepEqual(b.visits,['/login']);
});

test('session : un éditeur peut garder la page ouverte et libérer chaque requête, y compris la première',async()=>{
  const b=browser(),keepEditing=event=>event.preventDefault();
  b.window.addEventListener('ofv-session-expired',keepEditing);
  const first=b.fetch('/api/programming'),second=b.fetch('/api/programming',{method:'POST'});
  const firstResponse=unauthorized();b.requests[0].resolve(firstResponse);await flush();
  const response=unauthorized();b.requests[1].resolve(response);
  assert.deepEqual(await outcome(second),{state:'fulfilled',value:response});
  assert.deepEqual(await outcome(first),{state:'fulfilled',value:firstResponse});
  assert.deepEqual(b.visits,[]);
  const saved={ok:true,status:200},third=b.fetch('/api/programming',{method:'POST'});
  b.requests[2].resolve(saved);
  assert.deepEqual(await outcome(third),{state:'fulfilled',value:saved});
  b.window.removeEventListener('ofv-session-expired',keepEditing);
  const fourth=b.fetch('/api/programming');b.requests[3].resolve(unauthorized());
  assert.deepEqual(await outcome(fourth),{state:'pending'});
  assert.deepEqual(b.visits,['/login'],'une nouvelle expiration renvoie à la connexion après sauvegarde');
});

test('session : chaque page du portail charge le script avant ses propres appels',()=>{
  assert.deepEqual(pages,['index','logs','memory','programming','radio-logs','settings']);
  for(const name of pages) {
    const html=web(name+'.html'),tag=html.indexOf('<script src="/session.js"></script>');
    assert.ok(tag>=0 && tag<html.indexOf('</head>'),name);
    assert.ok(tag<html.indexOf('<script>'),name);
  }
});

// Exécute les scripts de la page, dans l’ordre où elle les déclare, sur un DOM minimal.
function page(name) {
  const html=web(name+'.html');
  const visits=[],requests=[],elements=new Map(),listeners=[],timers=new Map();
  let created=0,nextTimer=0,now=0;
  function element(selector) {
    if(!elements.has(selector))elements.set(selector,{
      textContent:'',value:selector==='#refresh'?'2000':'',className:'',disabled:false,hidden:false,checked:false,
      style:{},dataset:{},files:[],options:[],handlers:new Map(),classList:{add(){},remove(){},toggle(){}},
      setAttribute(){},getAttribute:()=>null,remove(){},
      append(...items){this.options.push(...items);},appendChild(item){this.append(item);},
      replaceChildren(...items){this.options=items;},
      cloneNode(){return element(selector+'/copie'+(++created));},querySelectorAll:()=>[],
      addEventListener(type,callback){this.handlers.set(type,callback);},
    });
    return elements.get(selector);
  }
  const document={hidden:false,documentElement:{dataset:{}},querySelector:element,querySelectorAll:()=>[],
    getElementById:()=>null,createElement:tag=>element(tag+(++created)),
    addEventListener(type,callback){listeners.push([type,callback]);}};
  const timer=(callback,delay,interval=false)=>{timers.set(++nextTimer,{callback,at:now+delay,delay,interval});return nextTimer;};
  const window=new EventTarget();
  const context=vm.createContext({document,console,Date,AbortController,FormData,URLSearchParams,TextEncoder,Event,
    window,history:{replaceState(){}},confirm:()=>true,
    location:{search:'',hash:'',replace:url=>visits.push(url)},
    HTMLElement:class {},customElements:{define(){}},
    setTimeout:timer,setInterval:(callback,delay)=>timer(callback,delay,true),clearTimeout:id=>timers.delete(id),clearInterval:id=>timers.delete(id),
    fetch:(url,options={})=>new Promise((resolve,reject)=>requests.push({url,options,resolve,reject})),
  });
  for(const [,source,inline] of html.matchAll(/<script(?: src="\/([^"]+)")?>([\s\S]*?)<\/script>/g))
    vm.runInContext(source?web(source):inline,context);
  for(const [type,callback] of listeners)if(type==='DOMContentLoaded')callback();
  return {visits,requests,element,window,evaluate:code=>vm.runInContext(code,context),
    displayed:()=>JSON.stringify([...elements].map(([selector,{textContent,value,className,disabled,hidden,checked}])=>
      [selector,textContent,value,className,disabled,hidden,checked])),
    async advance(ms){
      const end=now+ms;
      for(;;){
        const entry=[...timers].filter(([,t])=>t.at<=end).sort((a,b)=>a[1].at-b[1].at)[0];
        if(!entry)break;
        const [id,timer]=entry;now=timer.at;
        if(timer.interval)timer.at+=timer.delay;else timers.delete(id);
        timer.callback();await flush();
      }
      now=end;await flush();
    },
  };
}

for(const name of pages)test(name+' : une session perdue renvoie à la connexion sans rien afficher d’autre',async()=>{
  const p=page(name);
  // Seule la page mémoire n’appelle l’API qu’à la demande.
  if(name==='memory')p.element('#btnRead').handlers.get('click')();
  await flush();
  assert.equal(p.requests.length,1,'premier appel de la page');
  assert.match(p.requests[0].url,/^\/api\//);
  const before=p.displayed();
  p.requests[0].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,['/login']);
  assert.equal(p.displayed(),before,'ni message d’erreur ni valeur vide avant le départ');
  await p.advance(15000);
  assert.equal(p.requests.length,1,'plus aucun appel en attendant la page de connexion');
});

const programmingData={ok:true,zones:[{id:1,enabled:true,editable:true,known:true,
  program:'FF'.repeat(42),revision:'initiale',mode:'Auto'}]};
const ok=data=>({ok:true,status:200,json:async()=>structuredClone(data)});

for(const action of ['enregistrement','lecture automatique','rechargement manuel']) {
  test('programmation : un 401 pendant '+action+' libère les contrôles et conserve les modifications',async()=>{
    const p=page('programming');p.requests[0].resolve(ok(programmingData));await flush();
    p.element('#allReduced').handlers.get('click')();
    const draft=p.evaluate('encodeProgram(draft)');
    if(action==='enregistrement')p.element('#save').handlers.get('click')();
    else if(action==='rechargement manuel')p.element('#reload').handlers.get('click')();
    else await p.advance(5000);
    p.requests[1].resolve(unauthorized());await flush();
    assert.equal(p.evaluate('busy || loading || polling'),false,'aucune requête ne reste suspendue');
    assert.equal(p.element('#editor').disabled,false);
    assert.equal(p.element('#save').disabled,false);
    assert.equal(p.evaluate('dirty'),true);
    assert.equal(p.evaluate('encodeProgram(draft)'),draft);
    assert.equal(p.evaluate('revision'),'initiale');
    assert.deepEqual(p.visits,[],'l’éditeur reste ouvert');
    await p.advance(5000);
    assert.equal(p.requests.length,3,'le rafraîchissement reprend');
    p.requests[2].resolve(ok(programmingData));await flush();
    p.element('#save').handlers.get('click')();
    assert.equal(p.requests[3].options.method,'POST');
    assert.equal(p.requests[3].options.body.get('program'),draft);
    p.requests[3].resolve(ok({ok:true,revision:'enregistrée'}));await flush();
    assert.equal(p.evaluate('dirty || busy'),false);
    assert.equal(p.evaluate('original'),draft);
  });
}

test('session : les réponses concurrentes ne changent plus la page après le début du départ',async()=>{
  for(const status of [200,401,500,'réseau']) {
    const b=browser(),first=b.fetch('/api/dashboard'),second=b.fetch('/api/config');
    b.requests[0].resolve(unauthorized());await flush();
    if(status==='réseau')b.requests[1].reject(new TypeError('Failed to fetch'));
    else b.requests[1].resolve({status});
    assert.deepEqual(await outcome(first),{state:'pending'});
    assert.deepEqual(await outcome(second),{state:'pending'});
    assert.deepEqual(b.visits,['/login']);
    const third=b.fetch('/api/dashboard');
    assert.equal(b.requests.length,2,'aucune nouvelle requête pendant le départ');
    assert.deepEqual(await outcome(third),{state:'pending'});
  }
});

for(const action of ['save','status'])test('paramètres : un 401 pendant '+action+' conserve les champs modifiés et permet de réessayer',async()=>{
  const p=page('settings');p.requests[0].resolve(ok({wifiSsid:'Ancien réseau'}));await flush();
  p.element('#wifiSsid').value='Nouveau réseau';p.element('#wifiPass').value='nouveau secret';
  p.element('#portalPassword').value='nouveau mot de passe';p.element('#portalPasswordConfirm').value='nouveau mot de passe';
  p.element('#configForm').handlers.get('input')?.();
  if(action==='save')p.element('#configForm').handlers.get('submit')({preventDefault(){}});
  else p.evaluate('loadStatus()');
  p.requests[1].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,[],'ne pas perdre les champs en quittant la page');
  assert.equal(p.element('#wifiSsid').value,'Nouveau réseau');
  assert.equal(p.element('#wifiPass').value,'nouveau secret');
  assert.equal(p.element('#portalPassword').value,'nouveau mot de passe');
  assert.equal(p.requests.length,2,'aucune écriture rejouée automatiquement');
  // La connexion a réussi dans un autre onglet, puis l’utilisateur réessaie.
  p.element('#configForm').handlers.get('submit')({preventDefault(){}});
  const saved=p.requests[2];
  assert.equal(saved.options.method,'POST');
  assert.equal(saved.options.body.get('wifiSsid'),'Nouveau réseau');
  assert.equal(saved.options.body.get('wifiPass'),'nouveau secret');
  assert.equal(saved.options.body.get('portalPassword'),'nouveau mot de passe');
  saved.resolve(ok({ok:true}));await flush();
  assert.match(p.element('#message').textContent,/Configuration enregistrée/);
  p.evaluate('loadStatus()');p.requests[3].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,['/login'],'après sauvegarde, la page peut revenir à la connexion');
});

test('paramètres : un chargement refusé ne remplace pas des valeurs déjà saisies',async()=>{
  const p=page('settings');p.element('#wifiPass').value='secret saisi';p.element('#useConnectMode').value='1';
  p.element('#configForm').handlers.get('change')?.();
  p.requests[0].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,[]);
  assert.equal(p.element('#wifiPass').value,'secret saisi');
  assert.equal(p.element('#useConnectMode').value,'1');
});

test('paramètres : une lecture réussie conserve la protection des modifications',async()=>{
  const p=page('settings');p.requests[0].resolve(ok({}));await flush();
  p.element('#mqttHost').value='nouveau courtier';p.element('#configForm').handlers.get('change')();
  p.evaluate('loadStatus()');p.requests[1].resolve(unauthorized());await flush();
  p.evaluate('loadStatus()');p.requests[2].resolve(ok({uptimeSec:10}));await flush();
  assert.equal(p.element('#mqttHost').value,'nouveau courtier');
  p.evaluate('loadStatus()');p.requests[3].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,[],'les modifications restent protégées lors d’une nouvelle expiration');
});

test('paramètres : une sauvegarde refusée conserve aussi les valeurs sans événement de saisie',async()=>{
  const p=page('settings');p.requests[0].resolve(ok({}));await flush();
  p.element('#wifiPass').value='secret rempli automatiquement';
  p.element('#configForm').handlers.get('submit')({preventDefault(){}});
  p.requests[1].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,[]);
  assert.equal(p.element('#wifiPass').value,'secret rempli automatiquement');
});

test('paramètres : le NetworkID récupéré est une modification à conserver',async()=>{
  const p=page('settings');p.requests[0].resolve(ok({}));await flush();
  p.element('#btnRecupNetworkID').handlers.get('click')();
  p.requests[1].resolve(ok({ok:true,networkID:'12:34:56:78'}));await flush();
  p.evaluate('loadStatus()');p.requests[2].resolve(unauthorized());await flush();
  assert.deepEqual(p.visits,[]);
  assert.equal(p.element('#networkID').value,'12:34:56:78');
});
