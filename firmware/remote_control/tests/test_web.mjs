// Executes the actual page script in Node with a small browser-environment stub.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const html=fs.readFileSync(new URL('../esp32/RemoteControl/index.html',import.meta.url),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements=new Map(),events={},docEvents={},requests=[],beacons=[];
const element=id=>{
 if(!elements.has(id))elements.set(id,{value:'20',textContent:'',disabled:false,
 classList:{add(){},remove(){}},matches(){return false;}});
 return elements.get(id);
};
const buttons=[...'qweasd'].map(key=>({...element('key-'+key),dataset:{key},setPointerCapture(){}}));
let status={online:true,armed:false,estop:false,owner:false,left:0,right:0};
let rejectDrive=false,delayedArm=null;
const context=vm.createContext({
 document:{getElementById:element,querySelectorAll:()=>buttons,hidden:false,
 addEventListener:(type,fn)=>docEvents[type]=fn},
 window:{addEventListener:(type,fn)=>events[type]=fn},
 navigator:{sendBeacon:(path)=>{beacons.push(path);return true;}},
 AbortController,URLSearchParams,setInterval:()=>1,setTimeout:()=>1,clearTimeout:()=>{},console,
 fetch:async(path,options={})=>{
  const body=Object.fromEntries(options.body||[]);requests.push({path,body});
  if(path==='/api/status')return{ok:true,json:async()=>status};
  if(path==='/api/arm'){
   if(delayedArm)await delayedArm.promise;
   status={...status,armed:true,owner:true};return{ok:true,json:async()=>({token:1234})};
  }
  if(rejectDrive&&path==='/api/drive')throw Error('network');
  return{ok:true,json:async()=>({accepted:true})};
 }
});
vm.runInContext(script,context);
const settle=()=>new Promise(resolve=>setImmediate(resolve));
const key=(name,repeat=false)=>({key:name,code:name===' '?'Space':'',repeat,
 target:element('body'),preventDefault(){}});
const run=code=>vm.runInContext(code,context);
await settle();
events.keydown(key('w'));assert.equal(run('held'),''); // cannot move before arm
await element('arm').onclick();assert.equal(run('token'),1234);
assert.equal(requests.filter(r=>r.path==='/api/drive').at(-1).body.left,'0');
events.keydown(key('w'));assert.equal(run('held'),''); // wait for STM32 armed status
await run('poll()');
events.keydown(key('w'));await settle();
assert.equal(requests.at(-1).body.left,'200');assert.equal(requests.at(-1).body.right,'200');
events.keyup(key('w'));await settle();assert.equal(requests.at(-1).body.left,'0');
events.keydown(key('a'));await settle();assert.equal(requests.at(-1).body.left,'-200');
assert.equal(requests.at(-1).body.right,'200');
events.keydown(key(' '));assert.equal(run('token'),0);assert.ok(beacons.length);
await element('arm').onclick();events.blur();assert.equal(run('token'),0);
await element('arm').onclick();await run('poll()');rejectDrive=true;events.keydown(key('w'));await settle();
assert.equal(run('token'),0);rejectDrive=false;
// Cancelling while an arm HTTP request is outstanding must not resurrect the token.
let resolveArm;
delayedArm={promise:new Promise(r=>resolveArm=r)};
const pending=element('arm').onclick();await settle();events.blur();resolveArm();await pending;
assert.equal(run('token'),0);
assert.ok(buttons.every(b=>b.onpointercancel&&b.onlostpointercapture));
console.log('PASS browser: unarmed inhibition, hold/release, turns, space, blur, network failure, cancelled arm');
