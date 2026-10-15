// Real receiver, image decoder and LVGL. No HA device actions are performed.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import {connection,configure} from './test_protocol.mjs';
const m=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
const inspector=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
assert.equal(inspector._preview_init(1280,800,149,1,1),1);
const w=1280,h=800;assert.equal(m._preview_init(w,h,149,6,6),1);
const receive=connection(m);configure(receive,'Photos test',1,[]);
let ms=0,epoch=1789401840;function tick(delta=300){ms+=delta;m._preview_time(ms,epoch,0);m._preview_render();}
function frame(){const p=m._preview_frame();return Buffer.from(m.HEAPU8.subarray(p,p+w*h*4));}
function bmp(r,g,b){const stride=w*3,buf=Buffer.alloc(54+stride*h);buf.write('BM');buf.writeUInt32LE(buf.length,2);buf.writeUInt32LE(54,10);buf.writeUInt32LE(40,14);buf.writeUInt32LE(w,18);buf.writeUInt32LE(h,22);buf.writeUInt16LE(1,26);buf.writeUInt16LE(24,28);for(let i=54;i<buf.length;i+=3){buf[i]=b;buf[i+1]=g;buf[i+2]=r;}return buf;}
let seq=0;
function load(bytes){
 for(let tries=0;tries<180;tries++){
  const raw=m.ccall('preview_next_action','string',[],[]);
  if(raw){const a=JSON.parse(raw);if(process.env.PHOTO_DEBUG) console.log(a);if(a.service==='esphome.screen_camera'&&a.data.saver==='camera'){
   receive({op:'camera',t:'saver',e:'image.screensaver_photos',u:`http://firmware-preview.invalid/phototest${++seq}.bmp`,view:Number(a.data.view)});
  }}
  tick(1000);
  const text=m.ccall('preview_next_image','string',[],[]);
  if(text){const req=JSON.parse(text),ptr=m._preview_image_buffer(req.id,bytes.length);assert.ok(ptr);m.HEAPU8.set(bytes,ptr);assert.equal(m._preview_image_ready(req.id,1),1);tick(10);return;}
 }
 assert.fail('No photo request');
}

const labels=()=>JSON.parse(m.ccall('preview_layout','string',[],[])).objects.filter(o=>o.type==='label');
const clock=()=>labels().findLast(o=>/^\d{1,2}:\d{2}$/.test(o.text));
for(const position of ['top_left','top_center','top_right','center','bottom_left','bottom_center','bottom_right']){
 receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photos',pc:true,pp:position,pt:'none',ps:30});m._preview_standby(1);tick();load(bmp(25,32,40));
 const c=clock();
 const date=labels().findLast(o=>o.text!==c.text&&o.y1>c.y1&&o.scale_x!==undefined&&o.type==='label');
 assert.ok(date,'date follows time');
 const metrics=JSON.parse(inspector.ccall('preview_photo_clock','string',['number','string','string','string'],[12,position,c.text,date.text]));
 const native=JSON.parse(inspector.ccall('preview_layout','string',[],[])).objects.filter(o=>o.type==='label');
 const reference=native.find(o=>o.text===c.text),referenceDate=native.find(o=>o.text===date.text);
 for(const [actual,expected] of [[c,reference],[date,referenceDate]])for(const key of ['x1','y1','content_width','content_height','line_height','scale_x','scale_y'])assert.equal(actual[key],expected[key],`panel/inspector ${position} ${key}`);
 assert.equal(metrics.requested,96);assert.ok(metrics.actual>=90,'default visibly larger than old49px line');
 const address=c.address;
 tick(100); // Settle the first image before measuring unchanged clock ticks.
 const flushes=()=>JSON.parse(m.ccall('preview_diagnostics','string',[],[])).flushes;
 const before=flushes();
 tick(1000);assert.equal(clock().address,address);
 assert.equal(flushes(),before,'same-minute clock tick must not invalidate or flush the photo');
 const pixels=frame();let xs=0,ys=0,count=0;
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){const i=(y*w+x)*4;if(pixels[i]>150&&pixels[i+1]>150&&pixels[i+2]>150){xs+=x;ys+=y;count++;}}
 assert.ok(count>500,'readable clock glyphs');
 if(position.includes('left'))assert.ok(xs/count<w/3);
 if(position.includes('right'))assert.ok(xs/count>w*2/3);
 if(position.startsWith('top'))assert.ok(ys/count<h/3);
 if(position.startsWith('bottom'))assert.ok(ys/count>h*2/3);
 if(position==='center')assert.ok(Math.abs(ys/count-h/2)<25);
 epoch+=60;const minuteBefore=flushes();tick(1000);assert.equal(clock().address,address);assert.notEqual(clock().text,c.text,'minute changes in place');assert.ok(flushes()>minuteBefore,'a changed minute redraws the clock');
 assert.ok(labels().some(o=>o.text===date.text),'date is present');
 if(position==='top_right'&&process.env.PHOTO_CLOCK_RENDER)writeFileSync(process.env.PHOTO_CLOCK_RENDER,frame());
 m._preview_standby(0);tick();assert.equal(m.ccall('preview_next_image','string',[],[]),'','wake cancels photo requests');
}
console.log('PASS 7 panel/inspector native clock parity anchors, persistent minute update and wake cancellation');
