// Real receiver and physical-touch route, with an isolated HA action queue.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import { connection, configure } from './test_protocol.mjs';
const wasmBinary=readFileSync(new URL('../src/wasm/firmware_preview.wasm', import.meta.url));
let m;
const policy = JSON.parse(readFileSync(new URL('../src/model/catalogue.json', import.meta.url))).domains.screen.pd;
let ms = 0;
const tick = (d=32) => {ms+=d;m._preview_time(ms,1789401840,12*3600);m._preview_render();};
const info = () => JSON.parse(m.ccall('preview_diagnostics','string',[],[]));
const layout = () => JSON.parse(m.ccall('preview_layout','string',[],[]));
const action = () => {let value; while ((value=m.ccall('preview_next_action','string',[],[]))) {const request=JSON.parse(value); if (!request.event || request.service !== 'esphome.screen_camera') return value;} return '';};
const tap = (x,y,hold=100) => {tick(400);m._preview_touch(x,y,1);tick(hold);m._preview_touch(x,y,0);tick(400);};
for (const entity of ['screen.pd_header','screen.pd_batteries']) {
 for (const target of ['input_select.example_mode','select.example_mode']) {
  m=await createModule({wasmBinary});
  m=await createModule({wasmBinary});
 assert.equal(m._preview_init(1280,800,149,6,6),1);ms=0;
  const receive=connection(m);
  const tile={entity,name:'PD',slot:0,state:'First',a:{},o:{display:'map',size:'square',tap:policy.taps[0],act:{s:policy.action,d:[[policy.entity_field,target]]}},x:{pm:'First',po:['First','Second']}};
  configure(receive,'PD',1,[tile]);tick();
  const t=info().tiles[0];tap(t.x+t.width/2,t.y+t.height/2);
  assert.equal(action(),'','opening the selector must not change a mode');
  const choice=layout().objects.find(o=>o.type==='label' && o.text==='Second');
  assert.ok(choice,`${entity} opens ${target}`);
  tap((choice.x1+choice.x2)/2,(choice.y1+choice.y2)/2);
  const request=JSON.parse(action());
  assert.equal(request.service,target.split('.')[0]+'.select_option');
  assert.deepEqual(request.data,{entity_id:target,option:'Second'});
  assert.equal(action(),'','one tap sends one selection');
  receive({...tile,o:undefined,slot:undefined,op:'state',i:0,state:'Second',x:{pm:'Second',po:['First','Second']}});tick();
  assert.equal(info().tiles[0].state,'Second','the HA-confirmed value reaches the card');
 }
}
for (const [tapMode,hold,state,options] of [['none',100,'First',['First','Second']], [policy.taps[0],1100,'First',['First','Second']], [policy.taps[0],100,'unavailable',[]]]) {
 m=await createModule({wasmBinary});
 assert.equal(m._preview_init(1280,800,149,6,6),1);ms=0;
 const receive=connection(m);
 configure(receive,'PD',1,[{entity:'screen.pd_header',name:'PD',slot:0,state,a:{},o:{display:'map',size:'square',tap:tapMode,act:{s:policy.action,d:[[policy.entity_field,'input_select.example_mode']]}},x:{pm:state,po:options}}]);tick();
 const t=info().tiles[0];tap(t.x+t.width/2,t.y+t.height/2,hold);
 assert.equal(action(),'','Nothing, hold and unavailable selectors send no action');
 assert.ok(!layout().objects.some(o=>o.type==='label' && o.text==='Second'));
}
console.log('PASS PD receiver → configurable selector → select_option; HA confirmation, Nothing, hold and unavailable');
