import {readFileSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const flush = async () => { for (let n=0;n<8;n++) await Promise.resolve(); };

function page(name, hidden=false) {
  const html=readFileSync(new URL('../web/'+name+'.html',import.meta.url),'utf8');
  const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
  const elements=new Map(),events=new Map(),timers=new Map(),requests=[];
  let now=0,nextTimer=0,nextElement=0;
  function element(selector) {
    if(!elements.has(selector))elements.set(selector,{
      value:selector==='#refresh'?'2000':selector==='#limit'?'0':'',
      textContent:'',checked:false,disabled:false,scrollTop:0,scrollHeight:10,
      handlers:new Map(),classList:{remove(){},toggle(){}},options:[],
      append(...items){this.options.push(...items);},replaceChildren(){this.options=[];},
      cloneNode(){return {...this};},
      addEventListener(type,callback){this.handlers.set(type,callback);},
    });
    return elements.get(selector);
  }
  const document={hidden,querySelector:element,querySelectorAll:()=>[],
    createElement:tag=>element(tag+(++nextElement)),
    addEventListener(type,callback){events.set(type,callback);}};
  function setTimer(callback,delay,interval=false){const id=++nextTimer;timers.set(id,{callback,at:now+delay,delay,interval});return id;}
  const context=vm.createContext({document,console,Date,AbortController,FormData,URLSearchParams,
    window:{addEventListener(){}},location:{search:''},confirm:()=>true,
    setTimeout:(callback,delay)=>setTimer(callback,delay),clearTimeout:id=>timers.delete(id),
    setInterval:(callback,delay)=>setTimer(callback,delay,true),clearInterval:id=>timers.delete(id),
    fetch(url,{signal,method='GET',body:requestBody}={}){
      let resolveHeaders,reject,resolveBody,rejectBody,readingBody=false;
      const response=new Promise((resolve,fail)=>{resolveHeaders=resolve;reject=fail;});
      const body=new Promise((resolve,fail)=>{resolveBody=resolve;rejectBody=fail;});
      const result={ok:true,json(){readingBody=true;return body;}};
      signal?.addEventListener('abort',()=>{
        const error=new DOMException('Requête annulée','AbortError');
        reject(error);
        if(readingBody)rejectBody(error);
      });
      requests.push({url,signal,method,body:requestBody,headers:()=>resolveHeaders(result),
        finish(data=name==='programming'?{ok:true,zones:[]}:name==='index'?{}:['sample']){resolveHeaders(result);resolveBody(structuredClone(data));},
        fail:()=>reject(new Error('network unavailable'))});
      return response;
    },
  });
  vm.runInContext(script,context);
  // Polling tests exercise request scheduling, independently of layout rendering.
  context.render=()=>{};
  context.renderGrid=()=>context.refreshControls();
  events.get('DOMContentLoaded')?.();
  return {requests,timers,element,evaluate:code=>vm.runInContext(code,context),
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
    async visibility(value){document.hidden=value;events.get('visibilitychange')?.();await flush();},
    async click(){element(name==='programming'?'#reload':name==='index'?'#refreshBtn':'#btnReload').handlers.get('click')();await flush();},
    async off(){element('#refresh').value='0';element('#refresh').handlers.get('change')();await flush();},
  };
}

for(const name of ['index','logs','radio-logs']) {
  const interval=name==='index'?5000:2000;
  test(name+': one request through slow headers/body and manual refresh',async()=>{
    const p=page(name);assert.equal(p.requests.length,1);
    await p.advance(5000);await p.click();assert.equal(p.requests.length,1);
    p.requests[0].headers();await flush();await p.advance(3000);
    assert.equal(p.requests.length,1,'reading the response body is still in flight');
    p.requests[0].finish();await flush();assert.equal(p.timers.size,1);
    await p.advance(interval-1);assert.equal(p.requests.length,1);
    await p.advance(1);assert.equal(p.requests.length,2);
    assert.equal(p.timers.size,1,'seule la nouvelle échéance reste active');
    assert.equal(p.requests[0].signal.aborted,false,'échéance supprimée après succès');
  });
  test(name+': hidden tabs stop polling, including an in-flight completion',async()=>{
    const p=page(name);await p.visibility(true);
    p.requests[0].finish();await flush();await p.advance(30000);
    assert.equal(p.requests.length,1);assert.equal(p.timers.size,0);
    await p.visibility(false);assert.equal(p.requests.length,2);
    await p.visibility(true);await p.visibility(false);
    assert.equal(p.requests.length,2,'visibility changes cannot duplicate a pending request');
    p.requests[1].finish();await flush();assert.equal(p.timers.size,1);
    await p.visibility(true);assert.equal(p.timers.size,0);
  });
  test(name+': initially hidden page waits until visible',async()=>{
    const p=page(name,true);await p.advance(30000);assert.equal(p.requests.length,0);
    await p.visibility(false);assert.equal(p.requests.length,1);
  });
  test(name+': a failed request releases the guard and schedules one retry',async()=>{
    const p=page(name);p.requests[0].fail();await flush();assert.equal(p.timers.size,1);
    await p.advance(interval);assert.equal(p.requests.length,2);
    await p.click();assert.equal(p.requests.length,2);
  });
  if(name!=='index')test(name+': Off preserves manual reload and disables visibility refresh',async()=>{
    const p=page(name);await p.off();p.requests[0].finish();await flush();
    await p.advance(30000);assert.equal(p.requests.length,1);
    await p.visibility(true);await p.visibility(false);assert.equal(p.requests.length,1);
    await p.click();assert.equal(p.requests.length,2);
    p.requests[1].finish();await flush();assert.equal(p.timers.size,0);
  });
}

const programmingData = {ok:true,zones:[{id:1,enabled:true,editable:true,known:true,
  program:'FF'.repeat(42),revision:'initiale',mode:'Auto'}]};

for(const manual of [false,true])for(const phase of ['en-têtes','corps']) {
  test(`programmation : expiration ${manual?'manuelle':'automatique'} pendant les ${phase}`,async()=>{
    const p=page('programming');
    p.requests[0].finish(programmingData);await flush();
    if(manual)await p.click();else await p.advance(5000);
    const pending=p.requests[1];
    assert.ok(pending.signal,'la lecture doit fournir un signal d’annulation');
    if(phase==='corps'){
      await p.advance(5000);pending.headers();await flush();await p.advance(4999);
    }else await p.advance(9999);
    assert.equal(pending.signal.aborted,false);
    assert.equal(p.requests.length,2,'pas de lecture automatique simultanée');
    assert.equal(p.element('#reload').disabled,manual);
    await p.advance(1);
    assert.equal(pending.signal.aborted,true);
    assert.equal(p.evaluate('loading || polling'),false);
    assert.equal(p.element('#reload').disabled,false);
    assert.equal(p.element('#message').textContent,'Délai de chargement de la programmation dépassé.');
    assert.equal([...p.timers.values()].filter(timer=>!timer.interval).length,0);
    await p.advance(5000);
    assert.equal(p.requests.length,3,'la prochaine lecture automatique peut partir');
    p.requests[2].finish(programmingData);await flush();
    assert.equal(p.element('#message').textContent,'','la reprise efface le message d’expiration');
    await p.advance(5000);assert.equal(p.requests.length,4,'le cycle reprend après succès');
    p.requests[3].finish(programmingData);await flush();
    assert.equal(p.requests[2].signal.aborted,false,'échéance supprimée après succès');
  });
}

test('programmation : une réponse lente conserve son échéance pendant la lecture du corps',async()=>{
  const p=page('programming');
  await p.advance(5000);p.requests[0].headers();await flush();await p.advance(3000);
  assert.equal(p.requests.length,1);
  p.requests[0].finish(programmingData);await flush();
  await p.advance(2000);
  assert.equal(p.requests.length,2);
  assert.equal(p.requests[0].signal.aborted,false);
  p.requests[1].finish(programmingData);await flush();
});

test('programmation : expiration en arrière-plan puis reprise visible',async()=>{
  const p=page('programming');
  await p.visibility(true);await p.advance(30000);
  assert.equal(p.requests[0].signal.aborted,true);
  assert.equal(p.requests.length,1);
  await p.visibility(false);await p.advance(5000);
  assert.equal(p.requests.length,2);
  p.requests[1].finish(programmingData);await flush();
});

test('programmation : une expiration préserve les modifications et ne borne pas leur enregistrement',async()=>{
  const p=page('programming');
  p.requests[0].finish(programmingData);await flush();
  p.evaluate('draft[0][0]=false;dirty=true;refreshControls()');
  const draft=p.evaluate('encodeProgram(draft)');
  await p.advance(15000);
  assert.equal(p.requests[1].signal.aborted,true);
  assert.equal(p.evaluate('encodeProgram(draft)'),draft);
  assert.equal(p.evaluate('dirty'),true);
  assert.equal(p.evaluate('revision'),'initiale');
  p.evaluate('save()');await flush();
  const save=p.requests[2];
  assert.equal(save.method,'POST');assert.equal(save.signal,undefined);
  assert.equal(save.body.get('program'),draft);
  await p.advance(20000);assert.equal(p.requests.length,3,'pas de lecture pendant l’enregistrement');
  save.finish({ok:true,revision:'enregistrée'});await flush();
  assert.equal(p.evaluate('dirty || busy'),false);
  assert.equal(p.evaluate('original'),draft);
});

test('programmation : une reprise sans zone efface aussi le message d’expiration',async()=>{
  const p=page('programming');
  await p.advance(10000);
  assert.equal(p.element('#message').textContent,'Délai de chargement de la programmation dépassé.');
  await p.advance(5000);p.requests[1].finish();await flush();
  assert.equal(p.element('#message').textContent,'');
});

for(const state of ['en cours','réussi','refusé','confirmation identique']) {
  test('programmation : une ancienne lecture expirée préserve un enregistrement '+state,async()=>{
    const p=page('programming');
    p.requests[0].finish(programmingData);await flush();
    p.evaluate('draft[0][0]=false;dirty=true;refreshControls()');
    if(state==='confirmation identique'){
      p.evaluate('save()');await flush();p.requests[1].finish({ok:true,revision:'première'});await flush();
      p.evaluate('draft[0][1]=false;dirty=true;refreshControls()');
    }
    await p.advance(5000);const read=p.requests.at(-1);
    p.evaluate('save()');await flush();const save=p.requests.at(-1);
    assert.equal(save.method,'POST');
    if(state!=='en cours'){
      save.finish(state==='refusé'?{ok:false,err:'Enregistrement refusé.'}:{ok:true,revision:'nouvelle'});
      await flush();
    }
    const feedback=p.element('#message').textContent;
    assert.ok(feedback.startsWith('Enregistrement') || feedback.startsWith('Programmation enregistrée'));
    await p.advance(10000);assert.equal(read.signal.aborted,true);
    assert.equal(p.element('#message').textContent,feedback,'l’expiration ne remplace pas le message plus récent');
    if(state==='en cours'){save.finish({ok:true,revision:'nouvelle'});await flush();}
    const finalFeedback=p.element('#message').textContent;
    const current={...programmingData,zones:[{...programmingData.zones[0],
      program:p.evaluate('original'),revision:p.evaluate('revision')}]};
    await p.advance(5000);p.requests.at(-1).finish(current);await flush();
    assert.equal(p.element('#message').textContent,finalFeedback,'la reprise conserve le message de sauvegarde');
    assert.equal(programmingData.zones[0].revision,'initiale','les réponses ont leurs propres objets');
  });
}

for(const name of ['index','logs','radio-logs']) {
  const interval=name==='index'?5000:2000;
  for(const phase of ['en-têtes','corps'])test(name+': reprise après un blocage des '+phase,async()=>{
    const p=page(name);
    assert.ok(p.requests[0].signal,'la lecture doit fournir un signal d’annulation');
    if(phase==='corps'){
      await p.advance(2000);p.requests[0].headers();await flush();
      await p.advance(7999);
    }else await p.advance(9999);
    await p.click();assert.equal(p.requests.length,1);
    assert.equal(p.requests[0].signal.aborted,false);
    await p.advance(1);
    assert.equal(p.requests[0].signal.aborted,true);
    assert.equal(p.timers.size,1,'une seule reprise programmée');
    if(name==='index')assert.equal(p.element('#refreshBtn').disabled,false);
    else assert.equal(p.element('#log').textContent,'Délai de chargement des journaux dépassé.');
    await p.advance(interval-1);assert.equal(p.requests.length,1);
    await p.advance(1);assert.equal(p.requests.length,2);
    p.requests[1].finish();await flush();
    await p.advance(interval);assert.equal(p.requests.length,3,'la reprise réussie relance le cycle');
    assert.equal(p.requests[1].signal.aborted,false);
    p.requests[2].finish();await flush();
  });
  test(name+': une expiration dans un onglet masqué ne relance pas les lectures',async()=>{
    const p=page(name);await p.visibility(true);await p.advance(10000);
    assert.equal(p.requests[0].signal.aborted,true);assert.equal(p.timers.size,0);
    await p.advance(30000);assert.equal(p.requests.length,1);
    await p.visibility(false);assert.equal(p.requests.length,2);
    p.requests[1].finish();await flush();
  });
  if(name!=='index')test(name+': Off reste respecté après expiration',async()=>{
    const p=page(name);await p.off();await p.advance(10000);
    assert.equal(p.requests[0].signal.aborted,true);assert.equal(p.timers.size,0);
    await p.advance(30000);assert.equal(p.requests.length,1);
    await p.click();assert.equal(p.requests.length,2);
    p.requests[1].finish();await flush();assert.equal(p.timers.size,0);
  });
}

test('programmation : une semaine non confirmée reste visible après expiration', async()=>{
  const p=page('programming');
  const saisie={...programmingData.zones[0], program:'AA'.repeat(42), pending:true};
  p.requests[0].finish({ok:true,zones:[saisie]});await flush();
  await p.click();
  p.requests[1].finish({ok:true,zones:[{...saisie,pending:false,failed:true}]});await flush();
  assert.match(p.element('#availability').textContent,/Programmation non confirmée/);
  assert.equal(p.evaluate('encodeProgram(draft)'),saisie.program);
});
