// Real firmware touch filter, guard, loader, receiver and LVGL; no device actions.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import {connection,configure} from './test_protocol.mjs';
const m=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
assert.equal(m._preview_init(1280,800,149,6,6),1);
const receive=connection(m);configure(receive,'Touch test',2,[]);
let ms=0,count=0;
function tick(n=100){ms+=n;m._preview_time(ms,1789401840,0);m._preview_render();}
function info(){return JSON.parse(m.ccall('preview_diagnostics','string',[],[]));}
function actions(){let a,out=[];while((a=m.ccall('preview_next_action','string',[],[])))out.push(JSON.parse(a));return out;}
function standby(entity='image.screensaver_photos'){receive({op:'saver',k:'camera',e:entity,n:'Photo',pt:'none',ps:30});m._preview_standby(1);tick();const initial=actions().find(a=>a.data.photo_prepare);if(initial){receive({op:'camera',t:'saver',e:entity,u:'http://firmware-preview.invalid/legacy.bmp',view:Number(initial.data.view)});tick();actions();}}
function gesture(x,y,xx=x,yy=y){tick(300);m._preview_touch(x,y,1);tick(100);if(xx!==x||yy!==y){m._preview_touch(xx,yy,1);tick(100);}m._preview_touch(xx,yy,0);tick(100);}
standby();gesture(600,400);
assert.equal(info().saver_active,true);assert.equal(info().photo_next_pending,true);
let a=actions().filter(a=>a.service==='esphome.screen_camera'&&a.data.next);assert.equal(a.length,1);assert.equal(a[0].data.next,'1');count++;
const request=a[0];
receive({op:'camera',t:'saver',e:'image.screensaver_photos',u:'http://firmware-preview.invalid/stale.bmp',view:Number(request.data.view)-1});tick();
assert.equal(m.ccall('preview_next_image','string',[],[]),'','old request response cannot satisfy new manual request');assert.equal(info().photo_next_pending,true);count++;
receive({op:'camera',t:'saver',e:'image.screensaver_photos',u:'http://firmware-preview.invalid/new.bmp',view:Number(request.data.view)});tick();
const image=JSON.parse(m.ccall('preview_next_image','string',[],[]));
const bytes=Buffer.alloc(54+1280*800*3);bytes.write('BM');bytes.writeUInt32LE(bytes.length,2);bytes.writeUInt32LE(54,10);bytes.writeUInt32LE(40,14);bytes.writeUInt32LE(1280,18);bytes.writeUInt32LE(800,22);bytes.writeUInt16LE(1,26);bytes.writeUInt16LE(24,28);
const ptr=m._preview_image_buffer(image.id,bytes.length);m.HEAPU8.set(bytes,ptr);assert.equal(m._preview_image_ready(image.id,1),1);tick();assert.equal(info().photo_next_pending,false);assert.equal(info().saver_active,true);count++;

// Other directions never become tap or page navigation.
for(const [x,y] of [[400,400],[800,400],[600,600]]){gesture(600,400,x,y);assert.equal(info().saver_active,true);assert.equal(info().photo_next_serial,1);assert.equal(actions().filter(a=>a.data.next).length,0);count++;}
gesture(600,500,600,200);assert.equal(info().saver_active,false);assert.equal(info().page,0);assert.equal(info().photo_next_pending,false);assert.equal(actions().length,0);count++;
// Normal page touch after Photos must not inherit a next action.
gesture(600,400);assert.equal(info().photo_next_serial,1);assert.equal(actions().filter(a=>a.data.next).length,0);count++;
standby('camera.test');gesture(600,400);assert.equal(info().photo_next_serial,1);assert.equal(info().saver_active,false);assert.equal(actions().filter(a=>a.data.next).length,0);count++;
for(const kind of ['media','clock']){receive({op:'saver',k:kind,e:kind==='media'?'media_player.test':'',n:'Other'});m._preview_standby(1);tick();assert.equal(info().saver_active,true);gesture(600,400);assert.equal(info().saver_active,false);assert.equal(info().photo_next_serial,1);count++;}
m._preview_standby(0);tick();standby();gesture(0,0);assert.equal(info().photo_next_serial,1);assert.equal(actions().filter(a=>a.data.next).length,0);count++;
console.log(`PASS ${count} Photos-only guarded touch, tap request, swipe-up home and no-action regression cases`);
