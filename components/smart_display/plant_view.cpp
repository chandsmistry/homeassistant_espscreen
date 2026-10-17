// The plant room card on the glass (firmware 0.55.0): the scene plant_card.h builds, painted in one object's draw event.
//
// Compiled on its own, as energy_view.cpp is: every header of this component lands in main.cpp, and on Xtensa the
// literal pool of one object file must stay within reach of all of its code (page_receiver.cpp says how that broke).
#include "esphome/core/log.h"
#include "runtime_tiles.h"
#include "plant_fonts.h"

namespace plant_view {
using runtime_tiles::Tile;
using runtime_tiles::Widgets;
namespace rt = runtime_tiles;
namespace pc = plant_card;
namespace ps = plant_scene;

constexpr uint32_t ASK_MS = 4000;  // how long a thermostat button waits for its second tap

struct View {
  std::shared_ptr<const pc::Data> data;
  pc::Scene scene;
  ps::Scene pic;          // the Lovelace card's schematic, when the tile has the room for it
  bool picture = false;
  float kx = 1, ky = 1;   // card units to pixels: the card fills the tile
  lv_timer_t *anim = nullptr;  // the card's moving parts
  int width = 0, height = 0;
  int tab = 0;
  int pending = -1;  // the button that asked for a second tap
  lv_timer_t *timer = nullptr;
};

static lv_color_t color(pc::Paint p) {
  switch (p) {
    case pc::Paint::kOk: return theme::color(theme::PLANT_OK);
    case pc::Paint::kHeat: return theme::color(theme::PLANT_HEAT);
    case pc::Paint::kHot: return theme::color(theme::PLANT_HOT);
    case pc::Paint::kCold: return theme::color(theme::PLANT_COLD);
    case pc::Paint::kWarn: return theme::color(theme::PLANT_WARN);
    case pc::Paint::kBad: return theme::color(theme::PLANT_BAD);
    case pc::Paint::kImmersion: return theme::color(theme::PLANT_IMMERSION);
    case pc::Paint::kGas: return theme::color(theme::PLANT_GAS);
    case pc::Paint::kLine: return theme::color(theme::LINE);
    case pc::Paint::kAccent: return theme::color(theme::ACCENT);
    case pc::Paint::kInk: return theme::color(theme::INK);
    case pc::Paint::kMuted: default: return theme::color(theme::MUTED);
  }
}

static const lv_font_t *font(pc::Face f) { return f == pc::BODY ? rt::control_font : rt::small_font; }

static View *view_of(lv_obj_t *canvas) { return canvas ? static_cast<View *>(lv_obj_get_user_data(canvas)) : nullptr; }

static pc::Measure measure() {
  pc::Measure m;
  for (int f = 0; f < pc::FACES; ++f) m.line[f] = font(pc::Face(f)) ? lv_font_get_line_height(font(pc::Face(f))) : 0;
  m.width = [](pc::Face f, const std::string &s) { return font(f) ? rt::text_width(s, font(f)) : 1 << 20; };
  // The card's drawing is 1000 x 620 units: its smallest words are 8.5 units tall, so it wants a tile of about a hand.
  m.schematic_w = ui::mm(100);
  m.schematic_h = ui::mm(62);
  m.touch = ui::touch_min();
  m.decimal = screen_text::decimal_mark();
  return m;
}

static void rebuild(lv_obj_t *canvas) {
  View *view = view_of(canvas);
  if (!view || !view->data) return;
  const pc::Measure m = measure();
  view->picture = pc::wants_picture(*view->data, m, view->width, view->height);
  if (view->picture) {
    view->pic = ps::build(view->data->pic, view->pending, float(lv_tick_get() % 1000000) / 1000.0f);
    view->kx = view->width / ps::W;
    view->ky = view->height / ps::H;
    view->scene = pc::Scene();
  } else {
    view->pic = ps::Scene();
    view->scene = pc::build(*view->data, m, view->width, view->height, view->tab, view->pending);
  }
  lv_obj_invalidate(canvas);
}

static bool meets(const lv_area_t &clip, int x1, int y1, int x2, int y2) {
  return x1 <= clip.x2 && x2 >= clip.x1 && y1 <= clip.y2 && y2 >= clip.y1;
}


// ---- The card's picture (plant_scene.h) in pixels.
static lv_color_t role(ps::Role r) {
  const lv_color_t ink = theme::color(theme::INK), card = theme::color(theme::CARD);
  const lv_color_t surface = lv_color_mix(ink, card, 13);  // 5 percent of the text colour over the card
  switch (r) {
    case ps::CARD: return card;
    case ps::SURFACE: return surface;
    case ps::BLOCK: return lv_color_mix(ink, card, 20);
    case ps::BORDER: return lv_color_mix(ink, surface, 36);
    case ps::TXT: return ink;
    case ps::DIM: return theme::color(theme::MUTED);
    case ps::FLOW: return theme::color(theme::PLANT_HEAT);
    case ps::COLD: return theme::color(theme::PLANT_COLD);
    case ps::DHW: return theme::color(theme::PLANT_HOT);
    case ps::ELEC: return theme::color(theme::PLANT_IMMERSION);
    case ps::SOFT: return theme::color(theme::PLANT_SOFT);
    case ps::PROBE: return theme::color(theme::PLANT_PROBE);
    case ps::CTRL: return theme::color(theme::PLANT_CTRL);
    case ps::GAS: return theme::color(theme::PLANT_GAS);
    case ps::OK: return theme::color(theme::PLANT_OK);
    default: return ink;
  }
}
static lv_color_t resolve(const ps::Ink &i) {
  lv_color_t c = i.role == ps::RGB ? lv_color_hex(i.rgb) : role(i.role);
  if (i.t) c = lv_color_mix(role(i.alt), c, i.t);
  if (i.mix < 255) c = lv_color_mix(c, role(i.under), i.mix);
  return c;
}

// One code point out of UTF-8, and the offset of the next.
static uint32_t next_point(const std::string &s, size_t &i) {
  const uint8_t c = uint8_t(s[i]);
  uint32_t cp = c;
  int more = 0;
  if (c >= 0xF0) { cp = c & 0x07; more = 3; } else if (c >= 0xE0) { cp = c & 0x0F; more = 2; } else if (c >= 0xC0) { cp = c & 0x1F; more = 1; }
  ++i;
  while (more-- > 0 && i < s.size()) cp = (cp << 6) | (uint8_t(s[i++]) & 0x3F);
  return cp;
}

struct Pen {
  lv_layer_t *layer;
  lv_area_t clip;
  int x0, y0;  // the card's corner on the display
  float kx, ky, k;  // card units to pixels across, down, and for sizes (the smaller)
  int X(float v) const { return x0 + int(std::lround(v * kx)); }
  int Y(float v) const { return y0 + int(std::lround(v * ky)); }
  bool sees(int x1, int y1, int x2, int y2) const { return meets(clip, x1, y1, x2, y2); }
};

static void pic_rect(const Pen &p, const ps::Rect &r) {
  const int grow = r.edge_w > 0 ? std::max(1, int(std::lround(r.edge_w * p.k / 2))) : 0;
  const int bw = r.edge_w > 0 ? std::max(1, int(std::lround(r.edge_w * p.k))) : 0;
  lv_area_t box{p.X(r.x) - grow, p.Y(r.y) - grow, p.X(r.x + r.w) - 1 + grow, p.Y(r.y + r.h) - 1 + grow};
  if (!p.sees(box.x1, box.y1, box.x2, box.y2)) return;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = int(std::lround(r.r * p.k)) + grow;
  dsc.bg_color = resolve(r.fill);
  dsc.bg_opa = lv_opa_t(r.fill_opa);
  dsc.border_width = bw;
  dsc.border_color = resolve(r.edge);
  dsc.border_opa = bw ? LV_OPA_COVER : LV_OPA_TRANSP;
  lv_draw_rect(p.layer, &dsc, &box);
}

static void pic_disc(const Pen &p, float cx, float cy, float rad, const lv_color_t &fill, int fill_opa, const lv_color_t &edge, int edge_px) {
  const int r = std::max(1, int(std::lround(rad * p.k)));
  lv_area_t box{p.X(cx) - r, p.Y(cy) - r, p.X(cx) + r, p.Y(cy) + r};
  if (!p.sees(box.x1, box.y1, box.x2, box.y2)) return;
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_color = fill;
  dsc.bg_opa = lv_opa_t(fill_opa);
  dsc.border_width = edge_px;
  dsc.border_color = edge;
  dsc.border_opa = edge_px ? LV_OPA_COVER : LV_OPA_TRANSP;
  lv_draw_rect(p.layer, &dsc, &box);
}

static void pic_poly(const Pen &p, const ps::Poly &poly) {
  const int w = std::max(1, int(std::lround(poly.w * p.k)));
  const lv_color_t c = resolve(poly.ink);
  for (size_t i = 1; i < poly.p.size(); ++i) {
    const int x1 = p.X(poly.p[i - 1].x), y1 = p.Y(poly.p[i - 1].y), x2 = p.X(poly.p[i].x), y2 = p.Y(poly.p[i].y);
    if (!p.sees(std::min(x1, x2) - w, std::min(y1, y2) - w, std::max(x1, x2) + w, std::max(y1, y2) + w)) continue;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = c; dsc.width = w;
    dsc.round_start = dsc.round_end = poly.round_cap;
    dsc.p1.x = x1; dsc.p1.y = y1; dsc.p2.x = x2; dsc.p2.y = y2;
    lv_draw_line(p.layer, &dsc);
  }
  // The card's pipes turn their corners with a round join.
  if (!poly.round_cap && w >= 3)
    for (size_t i = 1; i + 1 < poly.p.size(); ++i) pic_disc(p, poly.p[i].x, poly.p[i].y, poly.w / 2, c, LV_OPA_COVER, c, 0);
}

static void pic_blob(const Pen &p, const ps::Blob &b) {
  if (b.p.size() < 3) return;
  const lv_color_t c = resolve(b.ink);
  int ymin = 1 << 20, ymax = -(1 << 20);
  std::vector<lv_point_t> v;
  for (auto &q : b.p) { v.push_back({lv_coord_t(p.X(q.x)), lv_coord_t(p.Y(q.y))}); ymin = std::min<int>(ymin, v.back().y); ymax = std::max<int>(ymax, v.back().y); }
  for (int y = ymin; y <= ymax; ++y) {
    if (y < p.clip.y1 || y > p.clip.y2) continue;
    float xs[24];
    int n = 0;
    const float yc = y + 0.5f;
    for (size_t i = 0; i < v.size() && n < 24; ++i) {
      const lv_point_t a = v[i], d = v[(i + 1) % v.size()];
      if ((a.y <= yc && d.y > yc) || (d.y <= yc && a.y > yc)) xs[n++] = a.x + (yc - a.y) * float(d.x - a.x) / float(d.y - a.y);
    }
    std::sort(xs, xs + n);
    for (int i = 0; i + 1 < n; i += 2) {
      const int x1 = int(std::lround(xs[i])), x2 = int(std::lround(xs[i + 1])) - 1;
      if (x2 < x1 || !p.sees(x1, y, x2, y)) continue;
      lv_draw_rect_dsc_t dsc;
      lv_draw_rect_dsc_init(&dsc);
      dsc.bg_color = c; dsc.bg_opa = lv_opa_t(b.opa);
      lv_area_t box{x1, y, x2, y};
      lv_draw_rect(p.layer, &dsc, &box);
    }
  }
}

static int weight_of(const ps::Run &r) { return r.weight >= 7 ? 700 : 500; }
static float glyph_width(const lv_font_t *f, uint32_t cp) { return float(lv_font_get_glyph_width(f, cp, 0)); }

// The width of a run in pixels: its glyphs and the card's letter spacing between them.
static float run_width(const Pen &p, const ps::Run &r, const lv_font_t *f) {
  float w = 0;
  int n = 0;
  for (size_t i = 0; i < r.s.size();) { w += glyph_width(f, next_point(r.s, i)); ++n; }
  return w + r.spacing * p.k * float(std::max(0, n - 1));
}

static void put_run(const Pen &p, const ps::Run &r, const lv_font_t *f, float x, int top, const lv_color_t &c) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = f; dsc.color = c;
  dsc.flag = LV_TEXT_FLAG_EXPAND;
  const int h = lv_font_get_line_height(f);
  if (std::fabs(r.spacing * p.k) < 0.5f) {
    const int w = int(std::lround(run_width(p, r, f))) + 4;
    if (!p.sees(int(x), top, int(x) + w, top + h)) return;
    dsc.text = r.s.c_str();
    lv_area_t box{int(std::lround(x)), top, int(std::lround(x)) + w, top + h};
    lv_draw_label(p.layer, &dsc, &box);
    return;
  }
  // Spaced letters, one at a time so the spacing is not rounded to whole pixels.
  float cx = x;
  for (size_t i = 0; i < r.s.size();) {
    const size_t from = i;
    const uint32_t cp = next_point(r.s, i);
    const std::string one = r.s.substr(from, i - from);
    const int gw = int(glyph_width(f, cp));
    if (cp != ' ' && p.sees(int(cx), top, int(cx) + gw + 2, top + h)) {
      dsc.text = one.c_str();
      dsc.text_local = 1;
      lv_area_t box{int(std::lround(cx)), top, int(std::lround(cx)) + gw + 2, top + h};
      lv_draw_label(p.layer, &dsc, &box);
    }
    cx += float(gw) + r.spacing * p.k;
  }
}

// A label turned a quarter clockwise (the card's lane names): its glyph pixels laid down by hand, for LVGL draws no rotated text
// without a layer.
static void put_turned(const Pen &p, const ps::Txt &t) {
  float total = 0;
  for (auto &r : t.runs) {
    const float px = r.size * p.k;
    auto face = plant_fonts::face(weight_of(r), px);
    if (!face.g) return;
    for (size_t i = 0; i < r.s.size();) {
      const uint32_t cp = next_point(r.s, i);
      for (int g = 0; g < plant_fonts::GLYPHS; ++g) if (face.g[g].cp == cp) { total += face.g[g].adv + r.spacing * p.k; break; }
    }
  }
  const ps::Run &r0 = t.runs[0];
  auto face = plant_fonts::face(weight_of(r0), r0.size * p.k);
  const int cx = p.X(t.x), cy = p.Y(t.y);
  float pen = -total / 2;
  const lv_color_t c = resolve(r0.ink);
  for (size_t i = 0; i < r0.s.size();) {
    const uint32_t cp = next_point(r0.s, i);
    const plant_fonts::Gly *gl = nullptr;
    for (int g = 0; g < plant_fonts::GLYPHS; ++g) if (face.g[g].cp == cp) { gl = &face.g[g]; break; }
    if (!gl) continue;
    for (int gy = 0; gy < gl->h; ++gy) {
      int run_from = -1, run_nib = 0;
      for (int gx = 0; gx <= gl->w; ++gx) {
        int nib = 0;
        if (gx < gl->w) {
          const int idx = gy * gl->w + gx;
          const uint8_t b = gl->data[idx / 2];
          nib = (idx % 2 == 0) ? (b >> 4) : (b & 15);
        }
        if (run_from >= 0 && nib != run_nib) {
          // One column of the turned glyph: its pixels gx in [run_from, gx) become a vertical run.
          const int sx = cx - ((gl->oy + gy) - face.ascent);
          const int sy = cy + int(std::lround(pen)) + gl->ox + run_from;
          lv_area_t box{sx, sy, sx, sy + (gx - run_from) - 1};
          if (run_nib > 0 && p.sees(box.x1, box.y1, box.x2, box.y2)) {
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            dsc.bg_color = c; dsc.bg_opa = lv_opa_t(run_nib * 17);
            lv_draw_rect(p.layer, &dsc, &box);
          }
          run_from = -1;
        }
        if (run_from < 0 && gx < gl->w && nib > 0) { run_from = gx; run_nib = nib; }
      }
    }
    pen += gl->adv + r0.spacing * p.k;
  }
}

static void pic_text(const Pen &p, const ps::Txt &t) {
  if (t.runs.empty()) return;
  if (t.turned) { put_turned(p, t); return; }
  struct Laid { const ps::Run *run; const lv_font_t *font; float w; };
  Laid laid[4];
  int n = 0;
  float total = 0;
  for (auto &r : t.runs) {
    if (n == 4) break;
    const lv_font_t *f = plant_fonts::nearest(weight_of(r), r.size * p.k);
    if (!f) return;
    laid[n] = {&r, f, run_width(p, r, f)};
    total += laid[n].w + r.dx * p.k;
    ++n;
  }
  float x = p.x0 + t.x * p.kx - (t.anchor == 0 ? 0.0f : t.anchor == 1 ? total / 2 : total);
  for (int i = 0; i < n; ++i) {
    const ps::Run &r = *laid[i].run;
    const lv_font_t *f = laid[i].font;
    x += r.dx * p.k;
    float base = p.y0 + t.y * p.ky + r.dy * p.k;
    if (t.centre) base += 0.26f * r.size * p.k;
    const int top = int(std::lround(base)) - (lv_font_get_line_height(f) - f->base_line);
    if (t.halo) {
      // The card strokes this label's edge in the surface colour so it reads over the pipe beneath.
      const lv_color_t halo = role(ps::SURFACE);
      const float d = 2 * p.k;
      for (int s = 0; s < 8; ++s) {
        static const float dx[] = {1, -1, 0, 0, 0.7f, -0.7f, 0.7f, -0.7f}, dy[] = {0, 0, 1, -1, 0.7f, 0.7f, -0.7f, -0.7f};
        put_run(p, r, f, x + dx[s] * d, top + int(std::lround(dy[s] * d)), halo);
      }
    }
    put_run(p, r, f, x, top, resolve(r.ink));
    x += laid[i].w;
  }
}

static void paint_picture(lv_layer_t *layer, const lv_area_t &a, const lv_area_t &clip, const View &view) {
  Pen p{layer, clip, a.x1, a.y1, view.kx, view.ky, std::min(view.kx, view.ky)};
  const ps::Scene &sc = view.pic;
  for (auto &st : sc.order) {
    switch (st.kind) {
      case ps::Kind::RECT: pic_rect(p, sc.rects[st.index]); break;
      case ps::Kind::POLY: pic_poly(p, sc.polys[st.index]); break;
      case ps::Kind::BLOB: pic_blob(p, sc.blobs[st.index]); break;
      case ps::Kind::DOT: {
        const ps::Dot &d = sc.dots[st.index];
        pic_disc(p, d.x, d.y, d.r + (d.edge_w > 0 ? d.edge_w / 2 : 0), resolve(d.fill), d.fill_opa, resolve(d.edge),
                 d.edge_w > 0 ? std::max(1, int(std::lround(d.edge_w * p.k))) : 0);
        break;
      }
      case ps::Kind::TXT: pic_text(p, sc.txts[st.index]); break;
    }
  }
}

static void draw(lv_event_t *e) {
  auto *canvas = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  View *view = view_of(canvas);
  if (!view) return;
  auto *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(canvas, &a);
  const lv_area_t clip = layer->_clip_area;
  if (view->picture) { if (view->pic.ok) paint_picture(layer, a, clip, *view); return; }
  if (!view->scene.ok) return;
  const pc::Scene &sc = view->scene;
  for (auto &b : sc.boxes) {
    if (!meets(clip, a.x1 + b.x, a.y1 + b.y, a.x1 + b.x + b.w - 1, a.y1 + b.y + b.h - 1)) continue;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = b.radius;
    dsc.bg_color = color(b.fill); dsc.bg_opa = lv_opa_t(b.fill_opa);
    dsc.border_width = b.border; dsc.border_color = color(b.edge); dsc.border_opa = b.border ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_area_t box{a.x1 + b.x, a.y1 + b.y, a.x1 + b.x + b.w - 1, a.y1 + b.y + b.h - 1};
    lv_draw_rect(layer, &dsc, &box);
  }
  for (auto &s : sc.segs) {
    const int x1 = a.x1 + std::min(s.x1, s.x2) - s.width, x2 = a.x1 + std::max(s.x1, s.x2) + s.width;
    const int y1 = a.y1 + std::min(s.y1, s.y2) - s.width, y2 = a.y1 + std::max(s.y1, s.y2) + s.width;
    if (!meets(clip, x1, y1, x2, y2)) continue;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color(s.paint); dsc.width = s.width;
    dsc.p1.x = a.x1 + s.x1; dsc.p1.y = a.y1 + s.y1; dsc.p2.x = a.x1 + s.x2; dsc.p2.y = a.y1 + s.y2;
    lv_draw_line(layer, &dsc);
  }
  for (auto &d : sc.discs) {
    if (!meets(clip, a.x1 + d.x - d.r, a.y1 + d.y - d.r, a.x1 + d.x + d.r, a.y1 + d.y + d.r)) continue;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.bg_color = color(d.paint); dsc.bg_opa = d.filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    dsc.border_width = d.filled ? 0 : 2; dsc.border_color = color(d.paint); dsc.border_opa = d.filled ? LV_OPA_TRANSP : LV_OPA_COVER;
    lv_area_t box{a.x1 + d.x - d.r, a.y1 + d.y - d.r, a.x1 + d.x + d.r, a.y1 + d.y + d.r};
    lv_draw_rect(layer, &dsc, &box);
  }
  for (auto &t : sc.texts) {
    const lv_font_t *f = font(t.face);
    if (!f || t.s.empty()) continue;
    const int h = lv_font_get_line_height(f);
    const int x1 = a.x1 + t.x, y1 = a.y1 + t.y;
    if (!meets(clip, x1, y1, x1 + t.w - 1, y1 + h - 1)) continue;
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.font = f; dsc.color = color(t.paint); dsc.text = t.s.c_str();
    dsc.flag = LV_TEXT_FLAG_EXPAND;
    dsc.align = t.align == 1 ? LV_TEXT_ALIGN_CENTER : t.align == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
    lv_area_t box{x1, y1, x1 + t.w - 1, y1 + h - 1};
    lv_draw_label(layer, &dsc, &box);
  }
}

// The card's moving parts: the picture is built again for the moment, a few times a second.
static void animate(lv_timer_t *timer) {
  auto *canvas = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
  View *view = view_of(canvas);
  if (view && view->picture && !lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN)) rebuild(canvas);
}

// A button's wait for its second tap is over: it goes back to its label.
static void ask_over(lv_timer_t *timer) {
  auto *canvas = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
  View *view = view_of(canvas);
  if (!view) return;
  view->timer = nullptr;  // a timer with one repeat deletes itself
  view->pending = -1;
  rebuild(canvas);
}

static void stop_asking(View &view) {
  if (view.timer) lv_timer_delete(view.timer);
  view.timer = nullptr;
  view.pending = -1;
}

void render(Widgets &w, const Tile &t, int width, int height) {
  rt::begin_extra(w, "plant", width, height);
  auto *&canvas = w.parts[0];
  if (!canvas) {
    canvas = lv_obj_create(w.extra);
    lv_obj_remove_style_all(canvas);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(canvas, new View());
    lv_obj_add_event_cb(canvas, draw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(canvas, [](lv_event_t *e) {
      auto *c = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
      if (auto *v = view_of(c)) { if (v->timer) lv_timer_delete(v->timer); if (v->anim) lv_timer_delete(v->anim); delete v; }
    }, LV_EVENT_DELETE, nullptr);
  }
  View &view = *view_of(canvas);
  view.data = t.extra().plant;
  view.width = width; view.height = height;
  // A new moment may no longer have the button that asked.
  if (!view.data || (!view.picture && view.pending >= int(view.data->controls.size())) || view.pending >= 4) stop_asking(view);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_set_size(canvas, width, height);
  rebuild(canvas);
  if (view.picture && !view.anim) view.anim = lv_timer_create(animate, 120, canvas);
  if (!view.picture && view.anim) { lv_timer_delete(view.anim); view.anim = nullptr; }
}

// The reading behind a sensor as the card a tap opens shows it: the row's label and value.
static bool open_reading(const pc::Data &data, const std::string &entity) {
  for (auto &tab : data.tabs)
    for (auto &row : tab.rows)
      if (row.entity == entity) {
        rt::open_sensor_detail(entity, row.label, row.value, "");
        return true;
      }
  return false;
}

// What a tap on the picture's readings opens: the name the card gives that reading.
static const char *reading_title(int ent) {
  static const char *const names[ps::E_COUNT] = {
      "Return", "Flow", "Draw-off", "Gas", "Zone 1", "Primary flow", "Flow rate", "Pressure", "Outside", "Heat curve",
      "Signal", "Megaflo intake", "Cylinder", "Heating circuit", "Heating manager", "Immersion", "Heaters", "Thermostat",
      "Hot water", "Mains flow", "Softener salt"};
  return ent >= 0 && ent < ps::E_COUNT ? names[ent] : "";
}

// The picture's four thermostat buttons: Off, Heat, Preset none, Preset eco.
struct Call { const char *service, *key, *value; };
static const Call BUTTONS[4] = {
    {"climate.set_hvac_mode", "hvac_mode", "off"}, {"climate.set_hvac_mode", "hvac_mode", "heat"},
    {"climate.set_preset_mode", "preset_mode", "none"}, {"climate.set_preset_mode", "preset_mode", "eco"}};

static bool tap_picture(lv_obj_t *canvas, View *view, const lv_point_t &point) {
  lv_area_t a;
  lv_obj_get_coords(canvas, &a);
  const float x = (point.x - a.x1) / view->kx, y = (point.y - a.y1) / view->ky;
  const ps::Hit *h = ps::hit(view->pic, x, y);
  if (!h) { if (view->pending >= 0) { stop_asking(*view); rebuild(canvas); } return false; }
  const ps::Data &d = view->data->pic;
  if (h->kind == 0) {
    stop_asking(*view);
    rebuild(canvas);
    const std::string &entity = d.ent(h->index);
    if (entity.empty()) return false;
    rt::open_sensor_detail(entity, reading_title(h->index), "", "");
    return true;
  }
  if (h->index < 0 || h->index > 3) return false;
  if (view->pending != h->index) {
    // The first tap only asks: these change the heating.
    stop_asking(*view);
    view->pending = h->index;
    view->timer = lv_timer_create(ask_over, ASK_MS, canvas);
    lv_timer_set_repeat_count(view->timer, 1);
    rebuild(canvas);
    return true;
  }
  stop_asking(*view);
  rebuild(canvas);
  const std::string &thermostat = d.ent(ps::E_THERMOSTAT);
  if (thermostat.empty()) return false;
  Tile target;
  target.entity = thermostat;
  target.received = true;
  rt::Extra extra;
  extra.action = BUTTONS[h->index].service;
  extra.action_data = {{BUTTONS[h->index].key, BUTTONS[h->index].value}};
  target.set_extra(std::move(extra), true);
  rt::perform(target);
  return true;
}

bool tap(Widgets &w, const Tile &t, const lv_point_t &point) {
  auto *canvas = w.parts[0];
  View *view = view_of(canvas);
  if (view && view->data && view->picture && view->pic.ok) return tap_picture(canvas, view, point);
  if (!view || !view->data || !view->scene.ok) return false;
  lv_area_t a;
  lv_obj_get_coords(canvas, &a);
  const pc::Hit *h = pc::hit(view->scene, point.x - a.x1, point.y - a.y1, 0);
  if (!h) { if (view->pending >= 0) { stop_asking(*view); rebuild(canvas); } return false; }
  const pc::Data &data = *view->data;
  switch (h->kind) {
    case pc::HitKind::TAB:
      stop_asking(*view);
      view->tab = h->index;
      rebuild(canvas);
      return true;
    case pc::HitKind::ROW:
    case pc::HitKind::PART:
      stop_asking(*view);
      rebuild(canvas);
      return !h->entity.empty() && open_reading(data, h->entity);
    case pc::HitKind::CONTROL: {
      if (h->index < 0 || h->index >= int(data.controls.size())) return false;
      if (view->pending != h->index) {
        // The first tap only asks: these change the heating.
        stop_asking(*view);
        view->pending = h->index;
        view->timer = lv_timer_create(ask_over, ASK_MS, canvas);
        lv_timer_set_repeat_count(view->timer, 1);
        rebuild(canvas);
        return true;
      }
      const pc::Control &c = data.controls[h->index];
      stop_asking(*view);
      rebuild(canvas);
      if (data.thermostat.empty()) return false;
      Tile target;
      target.entity = data.thermostat;
      target.received = true;
      rt::Extra extra;
      extra.action = c.service;
      extra.action_data = {{c.key, c.value}};
      target.set_extra(std::move(extra), true);
      rt::perform(target);
      (void) t;
      return true;
    }
  }
  return false;
}
}  // namespace plant_view
