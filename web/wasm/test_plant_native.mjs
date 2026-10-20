// Native Plant Room values through the production receiver and firmware renderer.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import createModule from '../src/wasm/firmware_preview.js';
import { connection, configure } from './test_protocol.mjs';

const m = await createModule({ wasmBinary: readFileSync(new URL('../src/wasm/firmware_preview.wasm', import.meta.url)) });
const fixture = JSON.parse(readFileSync(new URL('../../tests/fixtures/plant_native.json', import.meta.url)));
assert.equal(m._preview_init(1280, 800, 149, 6, 6), 1);
let ms = 0;
const tick = (delta = 32) => { ms += delta; m._preview_time(ms, 1792499000, 0); m._preview_render(); };
const receive = connection(m);
const diagnostics = () => JSON.parse(m.ccall('preview_diagnostics', 'string', [], []));
const tile = { entity: 'screen.plant_room', name: 'Plant room', slot: 0, state: 'Online', a: {}, o: { size: 'full' }, x: fixture };

configure(receive, 'Native Plant Room', 1, [tile]);
tick();
assert.equal(diagnostics().tiles[0].mode, 'plant');
assert.equal(m.ccall('preview_next_action', 'string', [], []), '', 'native Plant Room must not ask for a picture');
assert.equal(diagnostics().images.length, 0, 'native Plant Room must not create a picture layer');
if (process.env.PLANT_RENDER_OUT) {
  const pointer = m._preview_frame();
  writeFileSync(process.env.PLANT_RENDER_OUT, Buffer.from(m.HEAPU8.subarray(pointer, pointer + 1280 * 800 * 4)));
}
console.log('PASS native Plant Room: top/bulk rows, aligned values and countdown render without a bitmap request');
