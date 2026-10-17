"""Generate the plant room schematic's Roboto glyphs: the card's 1000 x 620 drawing scaled to a tile needs a handful of
pixel sizes in two weights (medium for the grey words, bold for names, values and tags). The view picks the nearest."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root = Path(__file__).resolve().parents[1]
out = ['#pragma once', '// Plant room schematic Roboto glyphs (tools/generate_plant_fonts.py): pixel sizes in two weights.',
       '#ifdef ESP_SCREEN_HOST', '#include "../../web/wasm/generated/font.h"', '#else', '#include "esphome/components/font/font.h"', '#endif',
       'namespace plant_fonts {',
       '// One glyph for the labels the view turns a quarter: its 4 bit pixels (rows run on, two to a byte), its advance, and where its',
       '// top left sits from the pen along the line top.',
       'struct Gly { uint16_t cp; const uint8_t *data; int8_t adv; int8_t ox; int8_t oy; uint8_t w; uint8_t h; };']
chars = ''.join(sorted(set(''.join(chr(i) for i in range(32, 127)) + '·°…Δ—–')))
SETS = {500: [9, 10, 11, 12, 13, 14, 16], 700: [9, 10, 11, 12, 13, 14, 16, 18, 21, 24, 27]}
tables = {}
for weight, sizes in SETS.items():
    tables[weight] = []
    for size in sizes:
        name = f'w{weight}_{size}'
        f = ImageFont.truetype(str(root / 'screen_manager/app/pd_fonts' / f'Roboto-{weight}.ttf'), size)
        ascent, descent = f.getmetrics()
        entries = []
        gly = []
        for i, ch in enumerate(chars):
            l, t, r, b = f.getbbox(ch)
            w, h = r - l, b - t
            im = Image.new('L', (max(1, w), max(1, h)))
            ImageDraw.Draw(im).text((-l, -t), ch, font=f, fill=255)
            pix = list(im.tobytes()) if w * h else [0]
            packed = []
            for j in range(0, len(pix), 2):
                packed.append((pix[j] // 17 << 4) | ((pix[j + 1] // 17) if j + 1 < len(pix) else 0))
            out.append(f'inline constexpr uint8_t {name}_{i}[]={{' + ','.join(map(str, packed)) + '};')
            entries.append(f'{{{ord(ch)}, {name}_{i}, {round(f.getlength(ch))}, {l}, {t}, {w}, {h}}}')
            gly.append(f'{{{ord(ch)}, {name}_{i}, {round(f.getlength(ch))}, {l}, {t}, {w}, {h}}}')
        out.append(f'inline constexpr esphome::font::Glyph {name}_glyphs[]={{' + ','.join(entries) + '};')
        out.append(f'inline constexpr Gly {name}_g[]={{' + ','.join(gly) + '};')
        out.append(f'inline esphome::font::Font {name}_font({name}_glyphs, {len(chars)}, {ascent}, {ascent + descent}, {descent}, {size // 2}, {size * 3 // 4}, 4);')
        tables[weight].append((size, name, ascent, descent))
for weight, items in tables.items():
    out.append(f'struct Entry{weight} {{ int px; esphome::font::Font *font; const Gly *g; int ascent; }};')
    out.append(f'inline Entry{weight} table{weight}[]={{' + ','.join(f'{{{s}, &{n}_font, {n}_g, {a}}}' for s, n, a, dsc in items) + '};')
out.append('// The font of this weight whose pixel size is nearest to `px`.')
out.append('inline const lv_font_t *nearest(int weight, float px) {')
for weight in tables:
    out.append(f'  if (weight == {weight}) {{ const Entry{weight} *best = &table{weight}[0]; float d = 1e9f;'
               f' for (auto &e : table{weight}) {{ float x = e.px > px ? e.px - px : px - e.px; if (x < d) {{ d = x; best = &e; }} }}'
               f' return best->font->get_lv_font(); }}')
out.append('  return nullptr;')
out.append('}')
out.append('// The glyph table of that font, for a label turned a quarter: the ascent, then the glyph of a code point.')
out.append('struct Face { const Gly *g; int ascent; };')
out.append('inline Face face(int weight, float px) {')
for weight in tables:
    out.append(f'  if (weight == {weight}) {{ const Entry{weight} *best = &table{weight}[0]; float d = 1e9f;'
               f' for (auto &e : table{weight}) {{ float x = e.px > px ? e.px - px : px - e.px; if (x < d) {{ d = x; best = &e; }} }}'
               f' return {{best->g, best->ascent}}; }}')
out.append('  return {nullptr, 0};')
out.append('}')
out.append(f'inline constexpr int GLYPHS = {len(chars)};')
out.append('}')
(root / 'components/smart_display/plant_fonts.h').write_text('\n'.join(out) + '\n')
print(len('\n'.join(out)))
