#pragma once
// The plant room card (firmware 0.55.0): a heating and hot-water plant room in two forms. On a small tile three tabs
// (Heating, Hot water, Cold) with a headline and rows of readings under them, the boiler's state above; on a large one
// (room for the picture, see Measure) the Lovelace card's own schematic, plant_scene.h.
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

#include "plant_scene.h"

namespace plant_card {
// The colours the add-on names (plant_room.py IDLE..IMMERSION), as paints the view maps to theme roles.
enum class Paint : uint8_t { kMuted, kOk, kHeat, kHot, kCold, kWarn, kBad, kImmersion, kInk, kLine, kAccent, kGas };
inline Paint paint_of(int code) {
  switch (code) {
    case 1: return Paint::kOk;
    case 2: return Paint::kHeat;
    case 3: return Paint::kHot;
    case 4: return Paint::kCold;
    case 5: return Paint::kWarn;
    case 6: return Paint::kBad;
    case 7: return Paint::kImmersion;
    default: return Paint::kMuted;
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
struct Data {
  bool any = false;
  int boiler = 0, boiler_colour = 0;
  std::string boiler_text, thermostat, confirm;
  std::vector<Tab> tabs;
  std::vector<Control> controls;
  plant_scene::Data pic;  // the schematic's own data
  // A tab row's sensor.
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
  bool ok = false;
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
  sc.texts.push_back({d.boiler_text, BODY, text_x, y + (head - m.line[BODY]) / 2, std::max(0, w - text_x), 0, Paint::kInk});
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
    sc.boxes.push_back({x, y, tab_w, tab_h, tab_h / 3, open ? Paint::kAccent : Paint::kLine, open ? 60 : 0, open ? Paint::kAccent : Paint::kLine, 1});
    const std::string name = i < 3 ? NAMES[i] : d.tabs[i].key;
    sc.texts.push_back({name, SMALL, x, y + (tab_h - m.line[SMALL]) / 2, tab_w, 1, open ? Paint::kInk : Paint::kMuted});
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
    sc.texts.push_back({r.label, SMALL, 0, ry + pad / 2, w * 6 / 10, 0, Paint::kMuted});
    sc.texts.push_back({r.value, SMALL, w * 4 / 10, ry + pad / 2, w * 6 / 10, 2, paint_of(r.colour)});
    if (!r.entity.empty()) sc.hits.push_back({0, ry, w, row_h, HitKind::ROW, i, r.entity});
    if (i + 1 < rows) sc.segs.push_back({0, ry + row_h - 1, w, ry + row_h - 1, 1, Paint::kLine});
  }
  if (controls) {
    const int c = int(d.controls.size()), cols = c <= 2 ? c : 2;
    const int bw = (w - gap * (cols - 1)) / cols;
    const int top = h - control_rows * btn_h - (control_rows - 1) * gap;
    for (int i = 0; i < c; ++i) {
      const Control &b = d.controls[i];
      const int bx = (i % cols) * (bw + gap), by = top + (i / cols) * (btn_h + gap);
      const bool asking = pending == i;
      const Paint edge = asking ? Paint::kWarn : b.active ? Paint::kAccent : Paint::kLine;
      sc.boxes.push_back({bx, by, bw, btn_h, btn_h / 4, edge, b.active || asking ? 60 : 0, edge, asking ? 2 : 1});
      sc.texts.push_back({asking ? d.confirm : b.label, SMALL, bx, by + (btn_h - m.line[SMALL]) / 2, bw, 1,
                          asking ? Paint::kWarn : b.active ? Paint::kInk : Paint::kMuted});
      sc.hits.push_back({bx, by, bw, btn_h, HitKind::CONTROL, i, ""});
    }
  }
  return sc;
}

// The form the room holds: the card's picture when the app sent it and the tile has the room for it, else the tabs.
inline bool wants_picture(const Data &d, const Measure &m, int w, int h) {
  return d.pic.any && m.schematic_w > 0 && w >= m.schematic_w && h >= m.schematic_h;
}

inline Scene build(const Data &d, const Measure &m, int w, int h, int tab = 0, int pending = -1) {
  Scene sc;
  if (w <= 0 || h <= 0 || !d.any || wants_picture(d, m, w, h)) return sc;
  return summary(d, m, w, h, tab, pending);
}
}  // namespace plant_card
