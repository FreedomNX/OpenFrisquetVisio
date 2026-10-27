import {readFileSync,readdirSync} from 'node:fs';
import {test} from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';

const script=readFileSync(new URL('../web/theme.js',import.meta.url),'utf8');
function browser({saved=null,hour=12,blocked=false}={}) {
  const documentEvents={},windowEvents={},intervals=[];
  const select={value:'',addEventListener(name,fn){this[name]=fn;}};
  const status={textContent:''},dataset={},writes=[];
  const document={hidden:false,documentElement:{dataset},
    getElementById(id){return id==='portalTheme'?select:id==='portalThemeStatus'?status:null;},
    addEventListener(name,fn){documentEvents[name]=fn;}};
  const context=vm.createContext({document,
    Date:class {getHours(){return hour;}},
    localStorage:{getItem(){if(blocked)throw Error('blocked');return saved;},
      setItem(key,value){if(blocked)throw Error('blocked');writes.push([key,value]);saved=value;}},
    window:{addEventListener(name,fn){windowEvents[name]=fn;}},
    setInterval(fn,ms){intervals.push({fn,ms});},
  });
  vm.runInContext(script,context);
  return {dataset,select,status,writes,documentEvents,windowEvents,intervals,document,
    setHour(value){hour=value;},loaded(){documentEvents.DOMContentLoaded();}};
}

test('default remains light and stored preference applies before DOM load',()=>{
  assert.equal(browser({hour:23}).dataset.theme,'light');
  assert.equal(browser({saved:'dark'}).dataset.theme,'dark');
  assert.equal(browser({saved:'invalid'}).dataset.theme,'light');
});

test('Auto switches at 7 and 20 using the current local time while the page stays open',()=>{
  const b=browser({saved:'auto',hour:6});assert.equal(b.dataset.theme,'dark');
  b.setHour(7);b.intervals[0].fn();assert.equal(b.dataset.theme,'light');
  b.setHour(19);b.intervals[0].fn();assert.equal(b.dataset.theme,'light');
  b.setHour(20);b.intervals[0].fn();assert.equal(b.dataset.theme,'dark');
  b.setHour(0);b.intervals[0].fn();assert.equal(b.dataset.theme,'dark');
  b.setHour(12);b.documentEvents.visibilitychange();assert.equal(b.dataset.theme,'light');
});

test('settings applies and persists immediately, and synchronizes other tabs',()=>{
  const b=browser();b.loaded();assert.equal(b.select.value,'light');
  b.select.value='dark';b.select.change();assert.equal(b.dataset.theme,'dark');
  assert.deepEqual(b.writes,[['ofv.portal.theme','dark']]);
  assert.match(b.status.textContent,/enregistré/);
  b.windowEvents.storage({key:'unrelated',newValue:'light'});assert.equal(b.dataset.theme,'dark');
  b.windowEvents.storage({key:'ofv.portal.theme',newValue:'auto'});assert.equal(b.select.value,'auto');
  b.setHour(21);b.windowEvents.pageshow();assert.equal(b.dataset.theme,'dark');
  b.windowEvents.storage({key:null,newValue:null});assert.equal(b.dataset.theme,'light');
});

test('blocked storage allows a temporary theme and never breaks the portal',()=>{
  const b=browser({blocked:true});b.loaded();b.select.value='dark';b.select.change();
  assert.equal(b.dataset.theme,'dark');assert.match(b.status.textContent,/ne permet pas/);
});

test('login and every portal page load the same theme before the body',()=>{
  for(const file of readdirSync(new URL('../web/',import.meta.url)).filter(f=>f.endsWith('.html'))) {
    const html=readFileSync(new URL('../web/'+file,import.meta.url),'utf8');
    assert.match(html,/<link rel="stylesheet" href="\/theme.css">/);
    assert.match(html,/<script src="\/theme.js"><\/script>/);
    assert.ok(html.indexOf('src="/theme.js"')<html.indexOf('</head>'),file);
  }
});
