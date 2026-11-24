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
let status={online:true,armed:false,estop:false,owner:false,left:0,right:0,
 upper:{available:true,mixer_output:false,pump_output:false}};
let rejectDrive=false,rejectStatus=false,delayedArm=null,delayedStatus=null;
const context=vm.createContext({
 document:{getElementById:element,querySelectorAll:()=>buttons,hidden:false,
 addEventListener:(type,fn)=>docEvents[type]=fn},
 window:{addEventListener:(type,fn)=>events[type]=fn},
 navigator:{sendBeacon:(path)=>{beacons.push(path);return true;}},
 AbortController,URLSearchParams,setInterval:()=>1,setTimeout:()=>1,clearTimeout:()=>{},console,
 fetch:async(path,options={})=>{
  const body=Object.fromEntries(options.body||[]);requests.push({path,body});
  if(path==='/api/status'){
   if(rejectStatus)throw Error('status polling timeout');
   const sampled=status;
   if(delayedStatus)await delayedStatus;
   return{ok:true,json:async()=>sampled};
  }
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
assert.equal(element('mixer').disabled,false);
element('mixer').onclick();await settle();
assert.equal(requests.at(-1).body.mixer,'1');assert.equal(requests.at(-1).body.pump,'0');
element('pump').onclick();await settle();
assert.equal(requests.at(-1).body.mixer,'1');assert.equal(requests.at(-1).body.pump,'1');
element('mixer').onclick();await settle();
assert.equal(requests.at(-1).body.mixer,'0');assert.equal(requests.at(-1).body.pump,'1');
status={...status,upper:{available:false}};await run('poll()');
assert.equal(run('token'),0);assert.equal(run('wantPump'),false);assert.equal(element('pump').disabled,true);
status={...status,upper:{available:true,mixer_output:false,pump_output:false}};
await element('arm').onclick();await run('poll()');
rejectStatus=true;await run('poll()');
assert.equal(run('token'),1234);assert.equal(run('ready'),true);
rejectStatus=false;
events.keydown(key('w'));await settle();
assert.equal(requests.at(-1).body.left,'200');assert.equal(requests.at(-1).body.right,'200');
events.keyup(key('w'));await settle();assert.equal(requests.at(-1).body.left,'0');
status={...status,maintenance:true};await run('poll()');
assert.equal(run('token'),0);assert.equal(element('arm').disabled,true);
events.keydown(key('w'));assert.equal(run('held'),'');
status={...status,maintenance:false};await run('poll()');
assert.equal(element('arm').disabled,false);
await element('arm').onclick();await run('poll()');
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
// A poll started during re-arm can report the old stopped session after
// /api/arm has already returned the new owner token.
status={...status,owner:false,armed:false};
delayedArm={promise:new Promise(r=>resolveArm=r)};
const restart=element('arm').onclick();await settle();
let resolveStatus;
delayedStatus=new Promise(r=>resolveStatus=r);
const stalePoll=run('poll()');await settle();
resolveArm();await restart;
delayedArm=null;
resolveStatus();await stalePoll;
delayedStatus=null;
assert.equal(run('token'),1234,'stale pre-arm status must not lock the new session');
await run('poll()');
for(const pump of ['1','0','1']){
 element('pump').onclick();await settle();
 assert.equal(requests.at(-1).body.pump,pump);
 assert.equal(run('ready'),true);
}
element('stop').onclick();
assert.equal(element('pump').disabled,true);
assert.equal(run('wantPump'),false);
console.log('PASS browser: unarmed inhibition, hold/release, OTA maintenance lock, turns, space, blur, network failure, cancelled arm');
console.log('PASS upper page: independent requests, on/off/on, re-arm with stale status, and global stop');
const sensorHTML=fs.readFileSync(new URL('../../sensor_test/esp32/main/index.html',import.meta.url),'utf8');
new vm.Script(sensorHTML.match(/<script>([\s\S]*?)<\/script>/)[1]);
console.log('PASS camera/TOF page JavaScript syntax');
const otaHTML=fs.readFileSync(new URL('../esp32/RemoteControl/ota.html',import.meta.url),'utf8');
new vm.Script(otaHTML.match(/<script>([\s\S]*?)<\/script>/)[1]);
console.log('PASS OTA page JavaScript syntax');
