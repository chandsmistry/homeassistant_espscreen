"""Power Distribution cards as pictures (MistryneT fork).

`pd-unified-header-card` and `pd-battery-management-card` (pd-console-cards.js) drawn the way the browser draws them:
same sizes, same Roboto, Noto's colour emoji, the glow, the pills and the bars. A screen cannot draw emoji, a glow or
a hex colour, so the add-on draws the card and the screen shows it as a picture (a camera tile; docs/PD_CARDS.md).

Everything here follows the card's source line by line: `header()` is PdUnifiedHeaderCard._render, `device()` is
PdBatteryManagementCard._device. CSS pixels are drawn at SCALE times their size; the result is an RGB picture.
"""
from __future__ import annotations

import asyncio
import io
import logging
import json
import re
from pathlib import Path

LOG = logging.getLogger(__name__)
HEADER = 'sensor.pd_header_json'
BATTERIES = 'sensor.pd_battery_status_json'
ACTIVE_COLOR = 'sensor.pd_active_color'
BURST = 'input_boolean.pd_predictive_boost_active_boolean'
ELIGIBILITY = {
    'dp3': 'input_boolean.pd_batt_dp3_eligible',
    'ultra': 'input_boolean.pd_batt_ultra_x_eligible',
    'ac': 'input_boolean.pd_batt_stream_ac_eligible',
    'max': 'input_boolean.pd_batt_stream_max_eligible',
}
CLASSES = {'dp3': 'dp3', 'ultra': 'ux', 'ac': 'ac', 'max': 'mx'}
SCALE = 3
HEADER_SIZE = (560, 282)   # CSS px: the card's min-height is 282
DEVICE_WIDTH = 530          # CSS px
GLOW_MARGIN = 36            # room around a device for its glow

FONT_DIR = Path(__file__).resolve().parent / 'pd_fonts'
EMOJI_FONTS = ('/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf', '/usr/share/fonts/noto/NotoColorEmoji.ttf',
               '/usr/share/fonts/truetype/noto-color-emoji/NotoColorEmoji.ttf', str(FONT_DIR / 'NotoColorEmoji.ttf'))

# HA's default light and dark themes: what the card's var() colours are.
THEMES = {
    'light': {'card': (255, 255, 255), 'divider': (224, 224, 224), 'track': (229, 229, 229), 'ink': (33, 33, 33),
              'border': (230, 230, 230)},
    'dark': {'card': (28, 28, 28), 'divider': (66, 66, 66), 'track': (50, 50, 50), 'ink': (225, 225, 225),
             'border': (50, 50, 50)},
}
# .ok and .bad pills, the bar colours per device, the fixed bar colours.
OK = ((220, 252, 231), (6, 95, 70), (187, 247, 208))
BAD = ((254, 226, 226), (127, 29, 29), (254, 202, 202))
BAR = {'dp3': ((245, 158, 11), (251, 191, 36)), 'ux': ((34, 197, 94), (74, 222, 128)),
       'ac': ((59, 130, 246), (96, 165, 250)), 'mx': ((139, 92, 246), (167, 139, 250))}
ACT, SOLAR = (96, 160, 196), (253, 224, 71)
GREY = (156, 163, 175)

_fonts, _emoji = {}, {}
# Emoji as the browser decides: anything from the supplementary planes, a symbol that defaults to emoji, or a symbol
# followed by U+FE0F. Other symbols (check marks, bullets, arrows) are text and come from a symbols font.
DEFAULT_EMOJI = set('\u231A\u231B\u23E9\u23EA\u23EB\u23EC\u23F0\u23F3\u25FD\u25FE\u2614\u2615\u2648\u2649\u264A\u264B'
                    '\u264C\u264D\u264E\u264F\u2650\u2651\u2652\u2653\u267F\u2693\u26A1\u26AA\u26AB\u26BD\u26BE\u26C4\u26C5'
                    '\u26CE\u26D4\u26EA\u26F2\u26F3\u26F5\u26FA\u26FD\u2705\u270A\u270B\u2728\u274C\u274E\u2753\u2754\u2755'
                    '\u2757\u2795\u2796\u2797\u27B0\u27BF\u2B1B\u2B1C\u2B50\u2B55')
SYMBOL_FONTS = ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', '/usr/share/fonts/dejavu/DejaVuSans.ttf',
                '/usr/share/fonts/TTF/DejaVuSans.ttf', str(FONT_DIR / 'DejaVuSans.ttf'))


def is_emoji_at(text, i):
    ch = text[i]
    if ord(ch) >= 0x1F000:
        return True
    return ch in DEFAULT_EMOJI or (i + 1 < len(text) and text[i + 1] == '\uFE0F')


def needs_symbols(ch):
    """A character Roboto lacks that the browser takes from a symbols font."""
    o = ord(ch)
    return 0x2190 <= o <= 0x2BFF or 0x2500 <= o <= 0x27BF or o in (0x25CF, 0x25CB)


def font(px, weight=400):
    from PIL import ImageFont
    key = (round(px * SCALE), weight)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(str(FONT_DIR / ('Roboto-%d.ttf' % weight)), key[0])
    return _fonts[key]


def emoji_image(char):
    """An emoji as an RGBA picture 136 px high (Noto's bitmap size), or None without the font."""
    from PIL import Image, ImageDraw, ImageFont
    if char in _emoji:
        return _emoji[char]
    image = None
    for path in EMOJI_FONTS:
        if Path(path).exists():
            try:
                face = ImageFont.truetype(path, 109)
                image = Image.new('RGBA', (136 + 20, 128 + 20), (0, 0, 0, 0))
                ImageDraw.Draw(image).text((10, 0), char, font=face, embedded_color=True)
                box = image.getbbox()
                image = image.crop(box) if box else None
            except Exception:
                image = None
            break
    _emoji[char] = image
    return image


def runs(text):
    """The text as (kind, piece) runs, kind 'emoji', 'symbol' or 'text'; a variation selector goes with its emoji."""
    out, i = [], 0
    while i < len(text):
        ch = text[i]
        if ch in '\uFE0F\u200D':
            i += 1
            continue
        if is_emoji_at(text, i):
            j = i + 1
            while j < len(text) and text[j] in '\uFE0F\u200D':
                j += 1
            out.append(('emoji', ch))
            i = j
            continue
        kind = 'symbol' if needs_symbols(ch) else 'text'
        if out and out[-1][0] == kind:
            out[-1] = (kind, out[-1][1] + ch)
        else:
            out.append((kind, ch))
        i += 1
    return out


def symbol_font(px):
    from PIL import ImageFont
    key = ('sym', round(px * SCALE))
    if key not in _fonts:
        for path in SYMBOL_FONTS:
            if Path(path).exists():
                _fonts[key] = ImageFont.truetype(path, key[1])
                break
        else:
            _fonts[key] = font(px)
    return _fonts[key]


class Canvas:
    def __init__(self, width, height, background, scale=SCALE):
        from PIL import Image, ImageDraw
        self.s, self.w, self.h = scale, width, height
        self.image = Image.new('RGBA', (round(width * scale), round(height * scale)), background + (255,))
        self.draw = ImageDraw.Draw(self.image)

    def px(self, v):
        return round(v * self.s)

    def text_width(self, text, size, weight=400):
        total = 0.0
        for kind, piece in runs(text):
            if kind == 'emoji':
                total += size * 1.17 * self.s
            else:
                total += (symbol_font(size) if kind == 'symbol' else font(size, weight)).getlength(piece)
        return total / self.s

    def text(self, x, baseline_top, text, size, weight=400, fill=(33, 33, 33), anchor='l', opacity=1.0):
        """Draw `text` with the top of its line box at `baseline_top` (CSS px); anchor l, m or r on x."""
        from PIL import Image
        width = self.text_width(text, size, weight)
        if anchor == 'm':
            x -= width / 2
        elif anchor == 'r':
            x -= width
        face = font(size, weight)
        ascent, descent = face.getmetrics()
        line = (ascent + descent) / self.s
        top = baseline_top + (size * 1.2 - line) / 2   # the CSS line box (line-height 1.2) centres the glyph box
        layer = Image.new('RGBA', self.image.size, (0, 0, 0, 0))
        from PIL import ImageDraw
        d = ImageDraw.Draw(layer)
        cx = x * self.s
        for kind, piece in runs(text):
            if kind == 'emoji':
                pic = emoji_image(piece[0])
                if pic:
                    edge = size * 1.17 * self.s
                    ratio = pic.width / pic.height
                    height = edge * 0.98
                    pic = pic.resize((max(1, round(height * ratio)), max(1, round(height))), Image.LANCZOS)
                    layer.alpha_composite(pic, (round(cx + (edge - pic.width) / 2), round(top * self.s + ascent - height * 0.86)))
                cx += size * 1.17 * self.s
            else:
                f = symbol_font(size) if kind == 'symbol' else face
                a2, _ = f.getmetrics()
                d.text((cx, top * self.s + ascent - a2), piece, font=f, fill=fill + (round(255 * opacity),))
                cx += f.getlength(piece)
        self.image.alpha_composite(layer)

    def rect(self, x, y, w, h, radius=0, fill=None, outline=None, width=1):
        box = (self.px(x), self.px(y), self.px(x + w) - 1, self.px(y + h) - 1)
        self.draw.rounded_rectangle(box, radius=self.px(radius), fill=fill + (255,) if fill else None,
                                    outline=outline + (255,) if outline else None, width=max(1, self.px(width)) if outline else 0)

    def shadow(self, x, y, w, h, radius, blur, color, alpha, dx=0, dy=0, spread=0):
        from PIL import Image, ImageDraw, ImageFilter
        layer = Image.new('RGBA', self.image.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.rounded_rectangle((self.px(x + dx - spread), self.px(y + dy - spread), self.px(x + dx + w + spread),
                             self.px(y + dy + h + spread)), radius=self.px(radius + spread), fill=color + (round(255 * alpha),))
        layer = layer.filter(ImageFilter.GaussianBlur(self.px(blur) / 2))
        self.image.alpha_composite(layer)
        self.draw = ImageDraw.Draw(self.image)

    def png(self, scale=None):
        from PIL import Image
        image = self.image.convert('RGB')
        if scale:
            image = image.resize((round(image.width * scale / self.s), round(image.height * scale / self.s)), Image.LANCZOS)
        out = io.BytesIO()
        image.save(out, 'PNG')
        return out.getvalue()


# ----- the values, as the card reads them -----
def number(value, fallback=0.0):
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return fallback
    return parsed if parsed == parsed and abs(parsed) != float('inf') else fallback


def truthy(value):
    return value is True or str(value).lower() == 'true'


def trunc(value):
    return int(number(value))


def rounded(value, places=0):
    factor = 10 ** places
    import math
    return math.floor(number(value) * factor + 0.5) / factor


def watts(value):
    w = trunc(value)
    if abs(w) >= 1000:
        k = rounded(w / 1000, 2)
        return '%s kW' % (('%d' % k) if k == int(k) else ('%g' % k))
    return '%d W' % w


def hex_rgb(value, fallback=GREY):
    m = re.fullmatch(r'#?([0-9a-fA-F]{6})', str(value or '').strip())
    if not m:
        m3 = re.fullmatch(r'#?([0-9a-fA-F]{3})', str(value or '').strip())
        if m3:
            return tuple(int(c * 2, 16) for c in m3.group(1))
        return fallback
    v = m.group(1)
    return (int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16))


def mix(a, b, share):
    """color-mix(in srgb, a share%, b)"""
    return tuple(round(a[i] * share + b[i] * (1 - share)) for i in range(3))


def state_of(states, entity, fallback=''):
    value = (states.get(entity) or {}).get('state')
    return fallback if value is None else value


# ----- the header: PdUnifiedHeaderCard._render -----
def header(states, theme='light', scale=SCALE):
    t = THEMES[theme]
    source = states.get(HEADER)
    w, h = HEADER_SIZE
    c = Canvas(w, h, t['card'], scale)
    c.rect(0, 0, w, h, 12, None, t['border'], 1)
    unavailable = not source or str(source.get('state', '')).lower() in ('unknown', 'unavailable', 'none', '')
    if unavailable:
        c.text(w / 2, h / 2 - 12, '⏳ Waiting for controller data… ⏳', 17.6, 400, t['ink'], 'm', 0.8)
        return c
    a = source.get('attributes') or {}
    mode = a.get('mode') or '\U0001F534 Controller Disabled \U0001F534'
    color = hex_rgb(a.get('active_color') or '#9ca3af')
    controller = truthy(a.get('ctrl'))
    story = a.get('story') or 'Waiting for controller state…'
    grid, total = trunc(a.get('grid_w')), trunc(a.get('total_w'))
    target = trunc(a.get('tgt_w')) if a.get('tgt_w') is not None else -100
    burst = truthy(a.get('burst'))
    glance = a.get('glance') or '⚖️ DP3: 0 W | Ultra X: 0 W | AC: 0 W ⚖️'
    ink = t['ink']
    icon = '⚙️' if controller else '\U0001F534'
    # title: font 1.55em of 14 px, weight 800, translateY(17px)
    # flex row: icon (1.3em), gap 10, text, gap 10, icon
    isz = 21.7 * 1.3
    iw, tw = c.text_width(icon, isz), c.text_width('Power Distribution Console', 21.7, 900)
    x = (w - (iw * 2 + tw + 20)) / 2
    ty = 6 + 4 + 17 + 4
    c.text(x, ty - 3, icon, isz, 400, ink, 'l', 1.0 if controller else 0.65)
    c.text(x + iw + 10, ty, 'Power Distribution Console', 21.7, 900, ink)
    c.text(x + iw + 10 + tw + 10, ty - 3, icon, isz, 400, ink, 'l', 1.0 if controller else 0.65)
    # the four lines are centred in the space under the title (the head grows to fill)
    y = 6 + 4 + 21.7 * 1.2 + 10 + 4
    fs1, fs2, fs3 = 16.8, 16.1, 16.1
    block = (10 + fs1 * 1.2 + 10) + (12 + 34 + 12) + (10 + fs3 * 1.2 + 10) + (10 + fs3 * 1.2 + 10)
    top = y + max(0, (h - 12 - y - block) / 2)
    # line one: Grid | Target | Σ
    y1 = top + 10 + 5
    parts = [('⚡ Grid: ', ink), (watts(grid), (255, 0, 0) if grid > 0 else ink),
             ('  |  \U0001F3AF Target: %d W  |  Σ ' % target, ink), (watts(total), ink)]
    total_w = sum(c.text_width(p, fs1) for p, _ in parts)
    x = (w - total_w) / 2
    for p, col in parts:
        c.text(x, y1, p, fs1, 400, col)
        x += c.text_width(p, fs1)
    # line two: the mode pill, and Burst
    y2 = y1 + fs1 * 1.2 + 10 + 12 - 0
    pill_font = fs2 * 0.9
    pills = [(mode, color, (17, 17, 17))]
    if burst:
        pills.append(('\U0001F680 Burst Enabled \U0001F680', (254, 226, 226), (127, 29, 29)))
    widths = [c.text_width(p[0], pill_font, 700) + 32 for p in pills]
    row = sum(widths) + 10 * (len(pills) - 1)
    x = (w - row) / 2
    ph = pill_font * 1.2 + 12
    for (label, fill, fg), pw in zip(pills, widths):
        c.shadow(x, y2, pw, ph, ph / 2, 4, (0, 0, 0), 0.25, 0, 2)
        c.rect(x, y2, pw, ph, ph / 2, fill)
        c.text(x + pw / 2, y2 + 6, label, pill_font, 700, fg, 'm')
        x += pw + 10
    y3 = y2 + 34 + 12 + 10
    c.text(w / 2, y3, story, fs3, 400, ink, 'm')
    c.text(w / 2, y3 + fs3 * 1.2 + 10 + 10, glance, fs3, 400, ink, 'm', 0.92)
    return c


# ----- a battery: PdBatteryManagementCard._device -----
def parse_batteries(states):
    raw = ((states.get(BATTERIES) or {}).get('attributes') or {}).get('batteries')
    if not isinstance(raw, str):
        return None
    try:
        parsed = json.loads(raw)
    except ValueError:
        return None
    return parsed if isinstance(parsed, dict) else None


def eligible_of(states, key, d):
    live = state_of(states, ELIGIBILITY[key], 'unavailable')
    return live == 'on' if live in ('on', 'off') else truthy(d.get('eligible'))


def device(states, key, theme='light', scale=SCALE):
    t = THEMES[theme]
    data = parse_batteries(states)
    d = (data or {}).get(key) or {}
    css = CLASSES[key]
    margin = GLOW_MARGIN
    w = DEVICE_WIDTH
    inner_h = 150
    c = Canvas(w + 2 * margin, inner_h + 2 * margin, t['card'], scale)
    x0, y0 = margin, margin
    glow = str(d.get('glow_class') or 'glow-off')
    glow_color = hex_rgb(state_of(states, ACTIVE_COLOR, '#9ca3af'))
    if glow == 'glow-charge':
        c.shadow(x0, y0, w, inner_h, 12, 25, (34, 197, 94), 0.4)
        c.shadow(x0, y0, w, inner_h, 12, 12, (34, 197, 94), 0.8)
        border, bw = (34, 197, 94), 2
    elif glow in ('glow-active', 'glow-master', 'glow-equal'):
        wide = {'glow-active': 22, 'glow-master': 34, 'glow-equal': 30}[glow]
        near = {'glow-active': 10, 'glow-master': 14, 'glow-equal': 14}[glow]
        outer = mix(glow_color, (255, 255, 255), 0.7) if glow == 'glow-master' else glow_color
        c.shadow(x0, y0, w, inner_h, 12, wide, outer, 0.6 if glow == 'glow-active' else 0.7)
        c.shadow(x0, y0, w, inner_h, 12, near, glow_color, 0.9)
        border, bw = glow_color, 2
    else:
        border, bw = t['divider'], 1
    c.rect(x0, y0, w, inner_h, 12, t['card'], border, bw)
    pad_x, pad_y = 14, 12
    ink = t['ink']
    eligible = eligible_of(states, key, d)
    online = truthy(d.get('online'))
    fs = 14 * 0.78
    ph = fs * 1.2 + 8 + 2
    py = y0 + pad_y
    def pill(x, label, look, anchor='l'):
        fill, fg, edge = look
        pw = c.text_width(label, fs, 900) + 20 + 2
        px_ = x if anchor == 'l' else x - pw
        c.rect(px_, py, pw, ph, ph / 2, fill, edge, 1)
        c.text(px_ + pw / 2, py + 1 + 4, label, fs, 900, fg, 'm')
    pill(x0 + pad_x, '✓ Eligible' if eligible else '✗ Ineligible', OK if eligible else BAD)
    pill(x0 + w - pad_x, ('● %s' % (d.get('conn_type') or '')) if online else '○ Offline', OK if online else BAD, 'r')
    name_size = 14 * 1.05
    c.text(x0 + w / 2, py + (ph - name_size * 1.2) / 2, str(d.get('name') or ''), name_size, 900, ink, 'm')
    # the two lines
    fd = 14 * 0.95
    ly = py + ph + 4 + 2
    charging = number(d.get('charge')) > 10
    segs1 = [('SoC ', False), ('%.1f%%' % number(d.get('soc')), True), (' • Rem ', False), ('%.2f kWh' % number(d.get('kwh')), True),
             (' • ', False)]
    if charging:
        segs1 += [('⚡ Charging ', False), ('%d W' % int(rounded(d.get('charge'))), True)]
    else:
        segs1 += [('Cap ', False), ('%d W' % trunc(d.get('cap')), True)]
    segs2 = [('Set ', False), ('%d W' % trunc(d.get('set')), True), (' • Act ', False), ('%d W' % trunc(d.get('act')), True)]
    for index, segs in enumerate((segs1, segs2)):
        total = sum(c.text_width(s, fd, 700 if b else 400) for s, b in segs)
        x = x0 + w / 2 - total / 2
        for s, b in segs:
            c.text(x, ly + index * fd * 1.4, s, fd, 700 if b else 400, ink, 'l', 0.9 if not b else 0.9)
            x += c.text_width(s, fd, 700 if b else 400)
    # three bars: 8 px high, 6 px apart
    by = ly + 2 * fd * 1.4 + 6
    bx, bwid = x0 + pad_x, w - 2 * pad_x
    soc_col, set_col = BAR[css]
    def bar(y, parts):
        c.rect(bx, y, bwid, 8, 4, t['track'])
        offset = 0
        for percent, color in parts:
            width = bwid * max(0, min(100, trunc(percent))) / 100
            if width > 0:
                c.rect(bx + offset, y, width, 8, 0 if offset else 4, color)
            offset += width
    bar(by, [(d.get('bar_soc'), soc_col)])
    bar(by + 20, [(d.get('bar_set'), set_col)])
    if d.get('has_solar'):
        bar(by + 40, [(d.get('bar_act_solar'), SOLAR), (d.get('bar_act_batt'), ACT)])
    else:
        bar(by + 40, [(d.get('bar_act'), ACT)])
    return c


def footer(states, theme='light', scale=SCALE):
    """The offline pills under the four devices ('DP3 Offline', 'ULTRA BLE Offline', ...)."""
    data = parse_batteries(states) or {}
    labels = []
    if not (data.get('dp3') or {}).get('online'):
        labels.append('DP3 Offline')
    for key, name in (('ultra', 'ULTRA'), ('ac', 'AC'), ('max', 'MAX')):
        dev = data.get(key) or {}
        if not dev.get('ble_online'):
            labels.append('%s BLE Offline' % name)
        if not dev.get('iob_online'):
            labels.append('%s IoB Offline' % name)
    return labels


def render(name, states, theme='light', scale=SCALE):
    """PNG bytes for `header`, or a battery key (`dp3`, `ultra`, `ac`, `max`)."""
    canvas = header(states, theme) if name == 'header' else device(states, name, theme)
    return canvas.png(scale)


WATCH = {HEADER, BATTERIES, ACTIVE_COLOR, BURST, 'input_select.pd_control_mode', *ELIGIBILITY.values()}
FOLDER = '/config/www/pd_cards'
NAMES = ('header', 'dp3', 'ultra', 'ac', 'max')


def write_all(states, folder=FOLDER, theme='light', scale=2):
    """Draw every card and write the pictures whose bytes changed; returns the names written."""
    root = Path(folder)
    root.mkdir(parents=True, exist_ok=True)
    written = []
    for name in NAMES:
        data = render(name, states, theme, scale)
        target = root / f'{name}.png'
        if target.exists() and target.read_bytes() == data:
            continue
        tmp = target.with_suffix('.tmp')
        tmp.write_bytes(data)
        tmp.replace(target)
        written.append(name)
    return written


async def keep_drawn(server, folder=FOLDER):
    """Redraw after the PD sensors change (a second of quiet first), for as long as the add-on runs."""
    loop = asyncio.get_running_loop()
    while True:
        await server.pd_changed.wait()
        await asyncio.sleep(1)
        server.pd_changed.clear()
        if HEADER not in server.states and BATTERIES not in server.states:
            continue
        try:
            await loop.run_in_executor(None, write_all, dict(server.states), folder)
        except Exception:
            LOG.exception('PD cards could not be drawn')


# ----- the screen's own cards (Library, Screen): one picture tile per card -----
TILES = {'screen.pd_header': 'header', 'screen.pd_dp3': 'dp3', 'screen.pd_ultra': 'ultra', 'screen.pd_ac': 'ac', 'screen.pd_max': 'max'}


def is_tile(entity):
    return entity in TILES


def fingerprint(tile, states, dark=False):
    """A short hash of everything the card is drawn from; the screen asks for a new picture when it changes."""
    import hashlib
    name = TILES.get(tile['entity'], 'header')
    keys = [HEADER, ACTIVE_COLOR, BURST, 'input_select.pd_control_mode'] if name == 'header' else [BATTERIES, ACTIVE_COLOR, ELIGIBILITY[name]]
    seen = [(k, (states.get(k) or {}).get('state'), json.dumps((states.get(k) or {}).get('attributes'), sort_keys=True, default=str)) for k in keys]
    return hashlib.sha1(json.dumps(seen, default=str).encode()).hexdigest()[:10]


def render_tile(tile, states, size, dark=False):
    """The card as a picture of exactly `size`: drawn at its own proportions, fitted inside, on the card's own colour."""
    from PIL import Image
    name = TILES.get(tile['entity'], 'header')
    theme = 'dark' if dark else 'light'
    canvas = header(states, theme) if name == 'header' else device(states, name, theme)
    card = canvas.image.convert('RGB')
    width, height = size
    ratio = min(width / card.width, height / card.height)
    card = card.resize((max(1, round(card.width * ratio)), max(1, round(card.height * ratio))), Image.LANCZOS)
    out = Image.new('RGB', (width, height), THEMES[theme]['card'])
    out.paste(card, ((width - card.width) // 2, (height - card.height) // 2))
    return out
