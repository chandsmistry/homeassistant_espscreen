// Native inspector renderer uses the same function as saver_photo_clock, including font selection and clamping.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
const m=await createModule({wasmBinary:readFileSync(new URL('../src/wasm/firmware_preview.wasm',import.meta.url))});
assert.equal(m._preview_init(1280,800,149,6,6),1);
let count=0;
for(const percent of [4,12,20,40])for(const anchor of ['top_left','top_center','top_right','center','bottom_left','bottom_center','bottom_right']){
 const p=JSON.parse(m.ccall('preview_photo_clock','string',['number','string','string','string'],[percent,anchor,'23:59','Wednesday, 14 October']));
 assert.equal(p.requested,800*percent/100);
 assert.equal(p.glyphs_complete,true,'clock and date must not use missing-glyph placeholders');
 for(const b of [p.time,p.date]){assert.ok(b.x>=0&&b.y>=0);assert.ok(b.x+b.w<=1280&&b.y+b.h<=800);}
 assert.ok(p.actual>=31);assert.ok(p.font_line>0);assert.ok(p.date.y>=p.time.y+p.time.h);
 const objects=JSON.parse(m.ccall('preview_layout','string',[],[])).objects;
 const time=objects.findLast(o=>o.text==='23:59'),date=objects.findLast(o=>o.text==='Wednesday, 14 October');
 assert.ok(time&&date);assert.equal(time.line_height,p.font_line);
 // The exported metrics are the actual shared C++ plan, not browser arithmetic.
 assert.equal(time.x1,p.time.x);assert.equal(time.y1,p.time.y);assert.equal(date.x1,p.date.x);assert.equal(date.y1,p.date.y);
 count++;
}
console.log(`PASS ${count} native clock size, anchor and actual-object parity cases`);
