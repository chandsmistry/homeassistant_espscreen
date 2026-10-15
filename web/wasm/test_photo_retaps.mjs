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

const bytes=Buffer.alloc(54+1280*800*3);bytes.write('BM');bytes.writeUInt32LE(bytes.length,2);bytes.writeUInt32LE(54,10);bytes.writeUInt32LE(40,14);bytes.writeUInt32LE(1280,18);bytes.writeUInt32LE(800,22);bytes.writeUInt16LE(1,26);bytes.writeUInt16LE(24,28);
function requested(){const a=actions().filter(a=>a.service==='esphome.screen_camera'&&a.data.next);assert.equal(a.length,1);return a[0];}
function link(a,name){receive({op:'camera',t:'saver',e:'image.screensaver_photos',u:`http://firmware-preview.invalid/${name}.bmp`,view:Number(a.data.view)});tick();return JSON.parse(m.ccall('preview_next_image','string',[],[]));}
function finish(image){const ptr=m._preview_image_buffer(image.id,bytes.length);assert.ok(ptr,'retaps must not cancel original image');m.HEAPU8.set(bytes,ptr);assert.equal(m._preview_image_ready(image.id,1),1);tick(300);}
standby();gesture(600,400);const first=requested(),image=link(first,'first');
const initial=info();
for(let n=0;n<7;n++){gesture(600,400);const current=info();assert.equal(current.photo_next_serial,initial.photo_next_serial);assert.equal(current.photo_next_view,initial.photo_next_view);assert.equal(current.photo_next_tapped_at,initial.photo_next_tapped_at);assert.equal(actions().filter(a=>a.data.next).length,0);}
count++;
finish(image);assert.equal(info().photo_next_pending,false);assert.equal(actions().filter(a=>a.data.next).length,0);assert.equal(info().photo_next_serial,1);count++;
// A fresh tap after successful completion starts the next request.
gesture(600,400);const second=requested();assert.equal(second.data.next,'2');finish(link(second,'second'));assert.equal(info().photo_next_pending,false);count++;
// A loaded unchanged link is not credited as a successful advance or left pending.
gesture(600,400);const same=requested();finish(link(same,'second'));assert.equal(info().photo_next_pending,false);assert.equal(actions().filter(a=>a.data.next).length,0);count++;
// Busy retaps cannot survive exit.
gesture(600,400);const third=requested(),image3=link(third,'third');gesture(600,400);gesture(600,500,600,200);assert.equal(info().photo_next_pending,false);assert.equal(info().saver_active,false);assert.equal(m._preview_image_ready(image3.id,1),0);count++;
// Failed current request clears pending, never loops manual next or reports success.
standby();gesture(600,400);const fourth=requested(),image4=link(fourth,'fourth');gesture(600,400);assert.equal(m._preview_image_ready(image4.id,0),0);tick();assert.equal(info().photo_next_pending,false);assert.equal(actions().filter(a=>a.data.next).length,0);count++;
// A settings change clears pending as well.
gesture(600,400);requested();gesture(600,400);receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photo',pt:'none',ps:15});tick();assert.equal(info().photo_next_pending,false);count++;
// No-answer timeout frees future taps instead of leaving a permanent pending queue.
gesture(600,400);requested();gesture(600,400);tick(61000);assert.equal(info().photo_next_pending,false);actions();gesture(600,400);assert.equal(requested().data.next,String(info().photo_next_serial));count++;
console.log(`PASS ${count} retap coalescing, retained image, fresh tap, exit, failure and settings cancellation cases`);
