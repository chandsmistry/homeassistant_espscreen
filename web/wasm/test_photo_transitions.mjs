// Real receiver, image decoder and LVGL. No HA device actions are performed.
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import {connection,configure} from './test_protocol.mjs';
const m=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
const w=480,h=320;assert.equal(m._preview_init(w,h,149,3,2),1);
const receive=connection(m);configure(receive,'Photos test',1,[]);
let ms=0;function tick(delta=300){ms+=delta;m._preview_time(ms,1789401840,0);m._preview_render();}
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
const options=JSON.parse(readFileSync(new URL('../src/model/catalogue.json',import.meta.url))).domains.screen.photos.transitions;
for(const name of options){
 m._preview_standby(0);receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photos',pc:false,pt:name,ps:15});m._preview_standby(1);tick();
 load(bmp(255,0,0));const red=frame();load(bmp(0,0,255));tick(400);const middle=frame();
 tick(16000);const done=frame();assert.notDeepEqual(red,done,`${name} reaches new photo`);
 assert.ok(done[2]>200,`${name} final blue pixel`);
 if(name!=='none') assert.notDeepEqual(middle,red,`${name} progresses`);
 if(name==='fade'){assert.ok(middle[0]>30&&middle[2]>30,'dissolve mixes both frames');}
 if(process.env.PHOTO_RENDER_OUT&&name==='fade')writeFileSync(process.env.PHOTO_RENDER_OUT+'.rgba',middle);
 m._preview_standby(0);tick();
}
// The actual receiver and loader must retain short intervals instead of clamping to 15 seconds.
for(const seconds of [5,10]){
 m._preview_standby(0);receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photos',pc:false,pt:'none',ps:seconds});
 m._preview_standby(1);tick();load(bmp(255,0,0));const firstAt=ms;
 load(bmp(0,0,255));assert.ok(ms-firstAt<=seconds*1000+2000,`${seconds}s receiver/loader interval retained`);
 m._preview_standby(0);tick();
}
// A patterned image must visibly move during a slow zoom, rather than merely switch colour.
receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photos',pc:false,pt:'zoom_in',ps:15});m._preview_standby(1);tick();load(bmp(255,0,0));
const pattern=bmp(0,0,255);for(let y=0;y<h;y++)for(let x=0;x<w;x++)if((Math.floor(x/24)+Math.floor(y/24))%2){const pos=54+(y*w+x)*3;pattern[pos]=40;pattern[pos+1]=180;pattern[pos+2]=40;}
load(pattern);const early=frame();tick(7500);assert.notDeepEqual(frame(),early,'slow zoom transforms patterned pixels');m._preview_standby(0);tick();
receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photos',pc:true,pt:'none',ps:30});m._preview_standby(1);tick();load(bmp(10,10,10));
const clockObjects=()=>JSON.parse(m.ccall('preview_layout','string',[],[])).objects.filter(o=>o.type==='label');
const clockLabel=()=>clockObjects().findLast(o=>/^\d{1,2}:\d{2}$/.test(o.text));
const originalClock=clockLabel();assert.ok(originalClock,'time-only clock visible');
assert.ok(!clockObjects().some(o=>/Monday|Tuesday|Wednesday|Thursday|Friday|Saturday|Sunday/.test(o.text)),'photo overlay has no full date');
for(let i=0;i<3;i++){tick(1000);assert.equal(clockLabel().address,originalClock.address,'clock object retained between ticks');}
m._preview_time(ms+1000,1789401900,0);m._preview_render();
assert.equal(clockLabel().address,originalClock.address,'minute update retains clock object');
assert.notEqual(clockLabel().text,originalClock.text,'minute update changes time text');
const clockFrame=frame();let white=0;for(let i=0;i<clockFrame.length;i+=4) if(clockFrame[i]>150&&clockFrame[i+1]>150&&clockFrame[i+2]>150) white++;assert.ok(white>100,'native clock overlay draws live white text');
m._preview_standby(0);tick();assert.equal(m.ccall('preview_next_image','string',[],[]),'','closing standby ends photo downloads');
const anchors=JSON.parse(readFileSync(new URL('../src/model/catalogue.json',import.meta.url))).domains.screen.photos.clock_positions;
for(const position of anchors){
 receive({op:'saver',k:'camera',e:'image.screensaver_photos',n:'Photos',pc:true,pp:position,pt:'none',ps:30});m._preview_standby(1);tick();load(bmp(10,10,10));
 const pixels=frame();let xs=0,ys=0,count=0;
 for(let y=0;y<h;y++)for(let x=0;x<w;x++){const i=(y*w+x)*4;if(pixels[i]>150&&pixels[i+1]>150&&pixels[i+2]>150){xs+=x;ys+=y;count++;}}
 assert.ok(count>100,position+' clock visible');const x=xs/count,y=ys/count;
 if(position.includes('left'))assert.ok(x<w/2,position+' left');
 if(position.includes('right'))assert.ok(x>w/2,position+' right');
 if(position.startsWith('top'))assert.ok(y<h/3,position+' top');
 if(position.startsWith('bottom'))assert.ok(y>h*2/3,position+' bottom');
 if(position==='center')assert.ok(Math.abs(y-h/2)<h/5,'centre clock');
 m._preview_standby(0);tick();
}
console.log(`PASS ${options.length} photo transitions, dissolve pixels, seven clock anchors and wake lifecycle`);
