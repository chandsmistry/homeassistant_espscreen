// Native PD values and touch behavior through the production receiver and LVGL.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import {connection,configure} from './test_protocol.mjs';
const m=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
const fixtures=JSON.parse(readFileSync(process.env.PD_FIXTURE || new URL('../../tests/fixtures/pd_native.json',import.meta.url)));
const policy=JSON.parse(readFileSync(new URL('../src/model/catalogue.json',import.meta.url))).domains.screen.pd;
assert.equal(m._preview_init(1280,800,149,6,6),1);
let ms=0;const tick=(delta=32)=>{ms+=delta;m._preview_time(ms,1789401840,720);m._preview_render();};
const receive=connection(m), info=()=>JSON.parse(m.ccall('preview_diagnostics','string',[],[]));
const layout=()=>JSON.parse(m.ccall('preview_layout','string',[],[]));
const frame=()=>{const p=m._preview_frame();return Buffer.from(m.HEAPU8.subarray(p,p+1280*800*4));};
const tiles=['screen.pd_header','screen.pd_batteries'].map((entity,i)=>({entity,name:entity,slot:i?12:0,state:'First',a:{},o:{size:i?'2x4':'square',tap:policy.taps[0],act:{s:policy.action,d:[[policy.entity_field,'input_select.example_mode']]}},x:{pm:'First',po:['First','Second'],pd:fixtures[entity]}}));
configure(receive,'Native PD',2,tiles);tick();
assert.deepEqual(info().tiles.map(t=>t.mode),['pd','pd']);
assert.equal(m.ccall('preview_next_action','string',[],[]),'','native PD cards must never ask for an image');
const objects=layout().objects.length, before=frame();
const next=structuredClone(tiles[0].x);next.pd.p[0]=1234;next.pd.mode='Second';
const start=performance.now();receive({op:'state',i:0,entity:tiles[0].entity,name:tiles[0].name,state:'Second',a:{},x:next});tick();
assert.notDeepEqual(frame(),before,'native values paint immediately');
assert.equal(layout().objects.length,objects,'updating native values keeps the same LVGL objects');
assert.equal(m.ccall('preview_next_action','string',[],[]),'','a state update must not fetch a bitmap');
const elapsed=performance.now()-start;
const card=info().tiles[0],x=card.x+card.width/2,y=card.y+card.height/2;
tick(400);m._preview_touch(x,y,1);tick(80);m._preview_touch(x+60,y,1);tick(80);m._preview_touch(x+60,y,0);tick(400);
assert.ok(!layout().objects.some(o=>o.type==='label'&&o.text==='First'),'an internal drag must not open the selector');
assert.equal(m.ccall('preview_next_action','string',[],[]),'');
if(process.env.PD_RENDER_OUT) {
 receive({op:'state',i:0,entity:tiles[0].entity,name:tiles[0].name,state:'First',a:{},x:tiles[0].x});tick();
 writeFileSync(process.env.PD_RENDER_OUT+'.rgba',frame());writeFileSync(process.env.PD_RENDER_OUT+'.json',JSON.stringify(info(),null,2));
 m._preview_dark(1);tick();writeFileSync(process.env.PD_RENDER_OUT+'-dark.rgba',frame());
}
console.log(`PASS native PD: numeric update ${elapsed.toFixed(1)}ms on this Mac, no bitmap requests, no object rebuild, internal drag rejected`);
