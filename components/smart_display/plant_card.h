#pragma once
// The plant room card (firmware 0.55.0): a heating and hot-water plant room in two forms. On a small tile three tabs
// (Heating, Hot water, Cold) with a headline and rows of readings under them, the boiler's state above; on a large one
// (room for the picture, see Measure) a schematic of the same state: boiler, the two zone valves, radiators and the
// cylinder with the hot water in it.
//
// Where everything goes. Pure arithmetic, free of LVGL and ESPHome, so tests/test_plant_card.cpp checks it on a PC;
// plant_view.cpp measures the text and paints the Scene this builds. Every derived state comes from the add-on
// (screen_manager/app/plant_room.py): the screen only draws what it is told.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace plant_card {
// The colours the add-on names (plant_room.py IDLE..IMMERSION), as paints the view maps to theme roles.
enum class Paint : uint8_t { MUTED, OK, HEAT, HOT, COLD, WARN, BAD, IMMERSION, INK, LINE, ACCENT, GAS };
inline Paint paint_of(int code) {
  switch (code) {
    case 1: return Paint::OK;
    case 2: return Paint::HEAT;
    case 3: return Paint::HOT;
    case 4: return Paint::COLD;
    case 5: return Paint::WARN;
    case 6: return Paint::BAD;
    case 7: return Paint::IMMERSION;
    default: return Paint::MUTED;
  }
}

// ---- What the add-on sends.
struct Row {
  std::string label, value, entity;
  int colour = 0;
};
struct Tab {
  std::string key, headline;
  int colour = 0;
  std::vector<Row> rows;
};
// A button that calls a service on the thermostat: `key` = `value` is its data.
struct Control {
  std::string label, service, key, value;
  bool active = false;
};
struct Schematic {
  float flow = NAN, ret = NAN, cylinder = NAN, draw = NAN, mains = NAN, bar = NAN;
  int thermocline = 8;
  bool firing = false, zone1 = false, zone2 = false, immersion = false, hot_draw = false, mains_active = false, condensing = false;
};
struct Data {
  bool any = false;
  int boiler = 0, boiler_colour = 0;
  std::string boiler_text, thermostat, confirm;
  std::vector<Tab> tabs;
  std::vector<Control> controls;
  Schematic map;
  // The sensors behind the schematic's parts, from the rows the tabs carry (plant_room.py keeps their order).
  std::string entity(size_t tab, size_t row) const {
    return tab < tabs.size() && row < tabs[tab].rows.size() ? tabs[tab].rows[row].entity : std::string();
  }
};

enum Face { BODY = 0, SMALL = 1, FACES = 2 };
struct Measure {
  int line[FACES] = {0, 0};
  std::function<int(Face, const std::string &)> width;
  // The room the picture needs (the board's size in mm turned to pixels) and the smallest thing a finger hits.
  int schematic_w = 0, schematic_h = 0, touch = 0;
  std::string decimal = ".";
};

// ---- What it draws.
struct Box {
  int x, y, w, h, radius;
  Paint fill;
  int fill_opa;  // 0..255
  Paint edge;
  int border;
};
struct Seg {
  int x1, y1, x2, y2, width;
  Paint paint;
};
struct Disc {
  int x, y, r;
  Paint paint;
  bool filled;
};
struct Text {
  std::string s;
  Face face;
  int x, y, w;
  int align;  // 0 left, 1 centre, 2 right
  Paint paint;
};
enum class HitKind : uint8_t { TAB, ROW, CONTROL, PART };
struct Hit {
  int x, y, w, h;
  HitKind kind;
  int index;
  std::string entity;
};
struct Scene {
  bool ok = false, schematic = false;
  std::vector<Box> boxes;
  std::vector<Seg> segs;
  std::vector<Disc> discs;
  std::vector<Text> texts;
  std::vector<Hit> hits;
};

inline std::string number(float v, int decimals, const std::string &mark) {
  if (std::isnan(v)) return "–";
  char buf[24];
  std::snprintf(buf, sizeof buf, "%.*f", decimals, double(v));
  std::string s = buf;
  const size_t dot = s.find('.');
  if (dot != std::string::npos) s.replace(dot, 1, mark);
  return s;
}

inline const Hit *hit(const Scene &s, int x, int y, int slack = 0) {
  const Hit *best = nullptr;
  long best_area = 0;
  for (const auto &h : s.hits) {
    if (x < h.x - slack || x >= h.x + h.w + slack || y < h.y - slack || y >= h.y + h.h + slack) continue;
    const long area = long(h.w) * h.h;
    if (!best || area < best_area) { best = &h; best_area = area; }
  }
  return best;
}

inline Scene summary(const Data &d, const Measure &m, int w, int h, int tab, int pending) {
  Scene sc;
  sc.ok = true;
  const int pad = std::max(4, std::min(w, h) / 36);
  int y = 0;
  // The boiler's state: a dot and a word, and the state of the thermostat's call to the right.
  const int head = m.line[BODY] + pad / 2;
  sc.discs.push_back({m.line[BODY] / 3 + 1, y + head / 2, std::max(3, m.line[BODY] / 4), paint_of(d.boiler_colour), true});
  const int text_x = m.line[BODY] * 2 / 3 + pad;
  sc.texts.push_back({d.boiler_text, BODY, text_x, y + (head - m.line[BODY]) / 2, std::max(0, w - text_x), 0, Paint::INK});
  y += head + pad / 2;
  if (d.tabs.empty()) return sc;
  tab = std::clamp(tab, 0, int(d.tabs.size()) - 1);
  // The tabs: equal segments, the open one filled.
  const int tab_h = std::min(std::max(m.touch, m.line[SMALL] + pad * 2), std::max(m.line[SMALL] + pad, h / 4));
  const int gap = std::max(2, pad / 2), n = int(d.tabs.size());
  const int tab_w = (w - gap * (n - 1)) / n;
  static const char *const NAMES[] = {"Heating", "Hot water", "Cold"};
  for (int i = 0; i < n; ++i) {
    const int x = i * (tab_w + gap);
    const bool open = i == tab;
    sc.boxes.push_back({x, y, tab_w, tab_h, tab_h / 3, open ? Paint::ACCENT : Paint::LINE, open ? 60 : 0, open ? Paint::ACCENT : Paint::LINE, 1});
    const std::string name = i < 3 ? NAMES[i] : d.tabs[i].key;
    sc.texts.push_back({name, SMALL, x, y + (tab_h - m.line[SMALL]) / 2, tab_w, 1, open ? Paint::INK : Paint::MUTED});
    sc.hits.push_back({x, y, tab_w, tab_h, HitKind::TAB, i, ""});
  }
  y += tab_h + pad;
  const Tab &t = d.tabs[tab];
  if (!t.headline.empty() && y + m.line[BODY] <= h) {
    sc.texts.push_back({t.headline, BODY, 0, y, w, 0, paint_of(t.colour)});
    y += m.line[BODY] + pad / 2;
  }
  // The buttons take the bottom of the heating tab when they fit; the rows take what is left.
  bool controls = tab == 0 && !d.controls.empty();
  const int btn_h = std::max(m.touch, m.line[SMALL] + pad * 2);
  int control_h = 0, control_rows = 0;
  if (controls) {
    const int c = int(d.controls.size());
    control_rows = c <= 2 ? 1 : (c + 1) / 2;
    control_h = control_rows * btn_h + (control_rows - 1) * gap + pad;
  }
  const int row_h = m.line[SMALL] + pad;
  int room = h - y - (controls ? control_h : 0);
  if (controls && room < row_h * 2) { controls = false; control_h = 0; room = h - y; }
  const int rows = std::min<int>(int(t.rows.size()), std::max(0, room / row_h));
  for (int i = 0; i < rows; ++i) {
    const Row &r = t.rows[i];
    const int ry = y + i * row_h;
    sc.texts.push_back({r.label, SMALL, 0, ry + pad / 2, w * 6 / 10, 0, Paint::MUTED});
    sc.texts.push_back({r.value, SMALL, w * 4 / 10, ry + pad / 2, w * 6 / 10, 2, paint_of(r.colour)});
    if (!r.entity.empty()) sc.hits.push_back({0, ry, w, row_h, HitKind::ROW, i, r.entity});
    if (i + 1 < rows) sc.segs.push_back({0, ry + row_h - 1, w, ry + row_h - 1, 1, Paint::LINE});
  }
  if (controls) {
    const int c = int(d.controls.size()), cols = c <= 2 ? c : 2;
    const int bw = (w - gap * (cols - 1)) / cols;
    const int top = h - control_rows * btn_h - (control_rows - 1) * gap;
    for (int i = 0; i < c; ++i) {
      const Control &b = d.controls[i];
      const int bx = (i % cols) * (bw + gap), by = top + (i / cols) * (btn_h + gap);
      const bool asking = pending == i;
      const Paint edge = asking ? Paint::WARN : b.active ? Paint::ACCENT : Paint::LINE;
      sc.boxes.push_back({bx, by, bw, btn_h, btn_h / 4, edge, b.active || asking ? 60 : 0, edge, asking ? 2 : 1});
      sc.texts.push_back({asking ? d.confirm : b.label, SMALL, bx, by + (btn_h - m.line[SMALL]) / 2, bw, 1,
                          asking ? Paint::WARN : b.active ? Paint::INK : Paint::MUTED});
      sc.hits.push_back({bx, by, bw, btn_h, HitKind::CONTROL, i, ""});
    }
  }
  return sc;
}

inline Scene schematic(const Data &d, const Measure &m, int w, int h) {
  Scene sc;
  sc.ok = sc.schematic = true;
  const Schematic &s = d.map;
  const int pad = std::max(4, std::min(w, h) / 40), sm = m.line[SMALL];
  auto X = [&](float f) { return int(std::lround(w * f)); };
  auto Y = [&](float f) { return int(std::lround(h * f)); };
  const int pipe = std::max(2, std::min(w, h) / 60);
  // The boiler: a box, a flame, its state and pressure.
  const int bx = X(0.04f), by = Y(0.18f), bw = X(0.22f), bh = Y(0.62f);
  sc.boxes.push_back({bx, by, bw, bh, pad, Paint::LINE, 0, paint_of(d.boiler_colour), 2});
  sc.texts.push_back({d.boiler_text, SMALL, bx, by + pad, bw, 1, Paint::INK});
  const int fr = std::max(4, std::min(bw, bh) / 5);
  sc.discs.push_back({bx + bw / 2, by + bh / 2, fr, s.firing ? Paint::HEAT : Paint::LINE, s.firing});
  if (!std::isnan(s.bar)) sc.texts.push_back({number(s.bar, 1, m.decimal) + " bar", SMALL, bx, by + bh - sm - pad, bw, 1, Paint::MUTED});
  sc.hits.push_back({bx, by, bw, bh, HitKind::PART, 0, d.entity(0, 3)});
  // Flow pipes out of the boiler through the two zone valves.
  const int y1 = Y(0.30f), y2 = Y(0.70f), vx = X(0.43f), cx = X(0.60f);
  sc.segs.push_back({bx + bw, y1, cx, y1, pipe, s.zone1 ? Paint::HEAT : Paint::LINE});
  sc.segs.push_back({bx + bw, y2, cx, y2, pipe, s.zone2 ? Paint::HOT : Paint::LINE});
  const int vr = std::max(5, sm / 2);
  sc.discs.push_back({vx, y1, vr, s.zone1 ? Paint::OK : Paint::MUTED, s.zone1});
  sc.discs.push_back({vx, y2, vr, s.zone2 ? Paint::OK : Paint::MUTED, s.zone2});
  sc.texts.push_back({"ZV1", SMALL, vx - X(0.06f), y1 - vr - sm - 1, X(0.12f), 1, Paint::MUTED});
  sc.texts.push_back({"ZV2", SMALL, vx - X(0.06f), y2 - vr - sm - 1, X(0.12f), 1, Paint::MUTED});
  // The boiler's flow and return temperatures sit at its edge.
  sc.texts.push_back({"▲ " + number(s.flow, 0, m.decimal) + "°", SMALL, bx + bw + pad / 2, y1 + pipe, vx - bx - bw - vr - pad, 0, Paint::HEAT});
  sc.texts.push_back({"▼ " + number(s.ret, 0, m.decimal) + "°", SMALL, bx, by + bh + pad, bw, 1, s.condensing ? Paint::COLD : Paint::WARN});
  sc.hits.push_back({bx + bw, y1 - pad * 2, vx - bx - bw, pad * 4, HitKind::PART, 1, d.entity(0, 0)});
  sc.hits.push_back({bx, by + bh, bw, sm + pad * 2, HitKind::PART, 2, d.entity(0, 2)});
  // The radiators.
  const int rx = cx, rw = X(0.96f) - cx, ry = Y(0.08f), rh = Y(0.30f) - Y(0.08f) + Y(0.14f);
  sc.boxes.push_back({rx, ry, rw, rh, pad, Paint::LINE, 0, s.zone1 ? Paint::HEAT : Paint::LINE, 2});
  sc.texts.push_back({"Radiators", SMALL, rx, ry + rh / 2 - sm / 2, rw, 1, s.zone1 ? Paint::HEAT : Paint::MUTED});
  const std::string house = d.entity(0, 6);
  if (!house.empty()) sc.hits.push_back({rx, ry, rw, rh, HitKind::PART, 3, house});
  // The cylinder: cold water at the bottom, hot above the thermocline.
  const int cy0 = Y(0.50f), cyh = Y(0.94f) - cy0, cw = rw;
  sc.boxes.push_back({rx, cy0, cw, cyh, pad, Paint::LINE, 0, Paint::LINE, 2});
  const int inner = std::max(1, cyh - 4), cold = std::lround(inner * (1.0f - std::clamp(s.thermocline, 8, 92) / 100.0f));
  sc.boxes.push_back({rx + 2, cy0 + 2, cw - 4, inner - cold, pad / 2, Paint::HOT, 110, Paint::HOT, 0});
  sc.boxes.push_back({rx + 2, cy0 + 2 + inner - cold, cw - 4, cold, pad / 2, Paint::COLD, 110, Paint::COLD, 0});
  sc.texts.push_back({"Cylinder " + number(s.cylinder, 0, m.decimal) + "°", SMALL, rx, cy0 + cyh / 2 - sm / 2, cw, 1, Paint::INK});
  if (s.immersion) {
    const int ix = rx + cw / 4, iy = cy0 + cyh - cyh / 5;
    sc.segs.push_back({ix, iy, ix + cw / 2, iy, std::max(2, pipe), Paint::IMMERSION});
    sc.texts.push_back({"Immersion", SMALL, rx, iy - sm - 1, cw, 1, Paint::IMMERSION});
  }
  sc.hits.push_back({rx, cy0, cw, cyh, HitKind::PART, 4, d.entity(1, 0)});
  // Hot water out at the top of the cylinder, mains in at the bottom.
  const int hx = X(0.96f);
  sc.segs.push_back({hx, cy0, X(0.99f), cy0, pipe, s.hot_draw ? Paint::HOT : Paint::LINE});
  const int my = Y(0.97f) - sm / 2;
  sc.segs.push_back({bx, my + sm / 2, rx, my + sm / 2 - pipe, pipe, s.mains_active ? Paint::COLD : Paint::LINE});
  sc.texts.push_back({"Mains " + number(s.mains, 1, m.decimal) + " L/min", SMALL, bx, my - sm - pipe, X(0.5f), 0, s.mains_active ? Paint::COLD : Paint::MUTED});
  sc.hits.push_back({bx, my - sm - pipe, X(0.5f), sm * 2 + pipe, HitKind::PART, 5, d.entity(2, 0)});
  return sc;
}

// The form the room holds: the picture when it has the room for it, else the tabs.
inline bool wants_schematic(const Measure &m, int w, int h) { return m.schematic_w > 0 && w >= m.schematic_w && h >= m.schematic_h; }

inline Scene build(const Data &d, const Measure &m, int w, int h, int tab = 0, int pending = -1) {
  Scene sc;
  if (w <= 0 || h <= 0 || !d.any) return sc;
  if (wants_schematic(m, w, h)) return schematic(d, m, w, h);
  return summary(d, m, w, h, tab, pending);
}
}  // namespace plant_card
