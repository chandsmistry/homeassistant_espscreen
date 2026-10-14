// Real receiver, PD hit regions and action transport. All entities exist only in this fake module.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import {connection,configure} from './test_protocol.mjs';
const wasmBinary=readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url));
const fixtures=JSON.parse(readFileSync(new URL('../../tests/fixtures/pd_native.json',import.meta.url)));
const policy=JSON.parse(readFileSync(new URL('../src/model/catalogue.json',import.meta.url))).domains.screen.pd;
async function setup(entity,actions,tapMode=policy.taps[0]){
 const m=await createModule({wasmBinary});assert.equal(m._preview_init(1280,800,149,6,6),1);
 let ms=0;const tick=(d=32)=>{ms+=d;m._preview_time(ms,1789401840,720);m._preview_render();};
 const receive=connection(m),pd=structuredClone(fixtures[entity]);if(actions)pd.actions=actions;
 const tile={entity,name:'PD',slot:0,state:'First',a:{},o:{size:entity==='screen.pd_batteries'?'2x4':'square',tap:tapMode,act:{s:policy.action,d:[[policy.entity_field,'input_select.example_mode']]}},x:{pm:'First',po:['First','Second'],pd}};
 configure(receive,'PD',1,[tile]);tick();
 const info=()=>JSON.parse(m.ccall('preview_diagnostics','string',[],[]));
 const layout=()=>JSON.parse(m.ccall('preview_layout','string',[],[]));
 const next=()=>{let value;while((value=m.ccall('preview_next_action','string',[],[]))){const a=JSON.parse(value);if(!a.event)return a;}return null;};
 const tap=(x,y,hold=100)=>{tick(400);m._preview_touch(x,y,1);tick(hold);m._preview_touch(x,y,0);tick(400);};
 const t=info().tiles[0];const at=i=>[t.x+t.width/2,t.y+t.height*(i+.5)/4];
 const frame=()=>{const p=m._preview_frame();return Buffer.from(m.HEAPU8.subarray(p,p+1280*800*4));};
 return {m,t,tick,tap,at,next,layout,frame,receive,tile};
}
const actions=[1,2,3,4].map(k=>({k,e:`switch.example_${k}`,a:{s:'homeassistant.toggle',d:[],t:[]}}));
const configured=await setup('screen.pd_batteries',actions),plain=await setup('screen.pd_batteries');
assert.deepEqual(configured.frame(),plain.frame(),'configuring block actions must not change accepted artwork');
for(let i=0;i<4;i++){
 configured.tap(...configured.at(i));const request=configured.next();
 assert.ok(request,`block ${i+1} fires its action`);
 assert.equal(request.service,'homeassistant.toggle');assert.deepEqual(request.data,{entity_id:`switch.example_${i+1}`});
 assert.equal(configured.next(),null,'one tap must not fire a neighbour or duplicate action');
 assert.ok(!configured.layout().objects.some(o=>o.type==='label'&&o.text==='Second'),'custom block tap must not open fallback selector');
}
const mixed=await setup('screen.pd_batteries',[{k:1,n:true},actions[3]]);
mixed.tap(...mixed.at(0));assert.equal(mixed.next(),null,'View only does nothing');
assert.ok(!mixed.layout().objects.some(o=>o.type==='label'&&o.text==='Second'));
mixed.tap(...mixed.at(1));assert.ok(mixed.layout().objects.some(o=>o.type==='label'&&o.text==='Second'),'unset block retains selector');
const header=await setup('screen.pd_header',[{k:0,e:'input_select.example',a:{s:'input_select.select_option',d:[['option','Manual']],t:[]}}]);
header.tap(header.t.x+header.t.width/2,header.t.y+header.t.height/2);const selected=header.next();assert.equal(selected.service,'input_select.select_option');assert.deepEqual(selected.data,{entity_id:'input_select.example',option:'Manual'});
const disabled=await setup('screen.pd_batteries',actions,'none');
for(let i=0;i<4;i++){disabled.tap(...disabled.at(i));assert.equal(disabled.next(),null,'whole-card View only disables every block');}
const gestures=await setup('screen.pd_batteries',actions),[x,y]=gestures.at(0);
gestures.tick(400);gestures.m._preview_touch(x,y,1);gestures.tick(100);gestures.m._preview_touch(x+60,y,1);gestures.tick(100);gestures.m._preview_touch(x+60,y,0);gestures.tick(400);assert.equal(gestures.next(),null,'internal drag fires no action');
gestures.tap(...gestures.at(0),1100);assert.equal(gestures.next(),null,'hold fires no action');
await assert.rejects(()=>setup('screen.pd_header',[{k:1,n:true}]),'receiver must reject a battery action on the header');
await assert.rejects(()=>setup('screen.pd_batteries',[{k:1,n:true},{k:1,n:true}]),'receiver must reject duplicate block actions');
console.log('PASS PD: four independent block targets, header action, one action per tap, view-only, default selector, unchanged pixels, no action on drag/hold');
