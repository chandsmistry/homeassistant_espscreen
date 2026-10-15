// Runs the real loader, immutable response parsing, store, LVGL and touch guard.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import {connection,configure} from './test_protocol.mjs';
const m=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
assert.equal(m._preview_init(1280,800,149,6,6),1);
m._preview_picture_budget(6*1024*1024);
const receive=connection(m);configure(receive,'Prepared test',2,[]);
let ms=0,count=0;
function tick(n=100){ms+=n;m._preview_time(ms,1789401840,0);m._preview_render();}
function info(){return JSON.parse(m.ccall('preview_diagnostics','string',[],[]));}
function actions(){let a,out=[];while((a=m.ccall('preview_next_action','string',[],[])))out.push(JSON.parse(a));return out;}
function request(){const a=actions().filter(x=>x.data.photo_prepare==='1');assert.equal(a.length,1);return a[0];}
function link(a,id){receive({op:'camera',t:'saver',e:'image.screensaver_photos',pf:id,u:`http://firmware-preview.invalid/${id}.bmp`,view:Number(a.data.view)});tick();return JSON.parse(m.ccall('preview_next_image','string',[],[]));}
const bytes=Buffer.alloc(54+1280*800*3);bytes.write('BM');bytes.writeUInt32LE(bytes.length,2);bytes.writeUInt32LE(54,10);bytes.writeUInt32LE(40,14);bytes.writeUInt32LE(1280,18);bytes.writeUInt32LE(800,22);bytes.writeUInt16LE(1,26);bytes.writeUInt16LE(24,28);
function finish(image){const ptr=m._preview_image_buffer(image.id,bytes.length);assert.ok(ptr);m.HEAPU8.set(bytes,ptr);assert.equal(m._preview_image_ready(image.id,1),1);tick(300);}
function gesture(x,y,xx=x,yy=y){tick(300);m._preview_touch(x,y,1);tick(100);if(xx!==x||yy!==y){m._preview_touch(xx,yy,1);tick(100);}m._preview_touch(xx,yy,0);tick(100);}
receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photo',pt:'none',ps:5});m._preview_standby(1);tick();
const first=request();assert.equal(first.data.photo_commit,undefined);finish(link(first,'first'));
assert.equal(info().photo_preload_displayed,'first');count++;
const next=request();assert.equal(next.data.photo_commit,'first');assert.equal(next.data.photo_session,first.data.photo_session);
const image=link(next,'second');for(let n=0;n<4;n++)gesture(600,400);
assert.equal(info().photo_preload_wanted,true);assert.equal(info().photo_preload_displayed,'first');assert.equal(actions().length,0);count++;
finish(image);assert.equal(info().photo_preload_displayed,'second');assert.equal(info().photo_preload_wanted,false);count++;
const third=request();assert.equal(third.data.photo_commit,'second');finish(link(third,'third'));
assert.equal(info().photo_preload_ready,true);assert.equal(info().photo_preload_displayed,'second');assert.ok(info().photo_store_bytes<=6*1024*1024);count++;
// A prepared tap swaps locally; no new network payload is needed for its source.
gesture(600,400);assert.equal(info().photo_preload_displayed,'third');assert.equal(info().saver_active,true);assert.equal(m.ccall('preview_next_image','string',[],[]),'','prepared swap needs no download');count++;
const fourth=request();const image4=link(fourth,'fourth');finish(image4);tick(5000);
assert.equal(info().photo_preload_displayed,'fourth');count++;
for(let n=0;n<20;n++){const a=request();finish(link(a,`cycle-${n}`));assert.ok(info().photo_store_bytes<=2*1280*800*2);gesture(600,400);assert.equal(info().photo_preload_displayed,`cycle-${n}`);assert.equal(m.ccall('preview_next_image','string',[],[]),'');}count++;
const fifth=request();const stale=link(fifth,'fifth');gesture(600,500,600,200);
assert.equal(info().saver_active,false);assert.equal(info().page,0);assert.equal(info().photo_preload_ready,false);
assert.equal(m._preview_image_ready(stale.id,1),0);count++;
// Old reservation response cannot install after exit.
receive({op:'camera',t:'saver',e:'image.screensaver_photos',pf:'late',u:'http://firmware-preview.invalid/late.bmp',view:Number(fifth.data.view)});tick();assert.equal(info().saver_active,false);tick(300);assert.equal(info().photo_store_bytes,0);count++;
// Re-entry owns a fresh session; a settings change rejects the old in-flight buffer.
m._preview_standby(1);tick();const reenter=request();assert.notEqual(reenter.data.photo_session,first.data.photo_session);
const obsolete=link(reenter,'obsolete');
receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photo',pt:'none',ps:10});tick();
assert.equal(m._preview_image_ready(obsolete.id,1),0);count++;
const changed=request();finish(link(changed,'changed'));assert.equal(info().photo_preload_displayed,'changed');
const failed=request();const bad=link(failed,'bad');assert.equal(m._preview_image_ready(bad.id,0),0);tick(2300);
assert.equal(info().photo_preload_displayed,'changed');const retry=request();assert.equal(retry.data.photo_commit,'changed');count++;
// Older add-ons answer without pf: negotiate legacy path and install its ordinary image.
receive({op:'camera',t:'saver',e:'image.screensaver_photos',u:'http://firmware-preview.invalid/legacy.bmp',view:Number(retry.data.view)});tick();
assert.equal(info().photo_preload_enabled,false);const legacy=actions().find(a=>a.service==='esphome.screen_camera');assert.ok(legacy);
receive({op:'camera',t:'saver',e:'image.screensaver_photos',u:'http://firmware-preview.invalid/legacy.bmp',view:Number(legacy.data.view)});tick();
finish(JSON.parse(m.ccall('preview_next_image','string',[],[])));assert.equal(info().saver_active,true);count++;
// Motion leaves a final overlay, but preparation resumes after its animation finishes.
m._preview_standby(0);tick();receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photo',pt:'zoom_in',ps:5});m._preview_standby(1);tick();
finish(link(request(),'motion-first'));finish(link(request(),'motion-next'));gesture(600,400);
assert.equal(info().photo_preload_displayed,'motion-next');assert.equal(actions().filter(a=>a.data.photo_prepare).length,0);
tick(5100);tick(300);assert.equal(request().data.photo_commit,'motion-next');count++;
m._preview_standby(0);tick();
console.log(`PASS ${count} prepared-first, commit, retap, RAM bound, local swap, timer and stale-exit cases`);
