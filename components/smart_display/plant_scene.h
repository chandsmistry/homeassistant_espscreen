#pragma once
// The plant room schematic (firmware 0.55.0), a 1:1 port of the Lovelace "plant room overview" card: the same 1000 x 620
// drawing, the same positions, the same words and the same colours for the same states.
//
// The add-on works every state out (screen_manager/app/plant_room.py `scene`); this header only turns that into shapes.
// Pure arithmetic, free of LVGL and ESPHome, so tests/test_plant_scene.cpp checks it on a PC. plant_view.cpp scales the
// shapes into the tile and paints them. Everything is in the card's own units; the path strings below are the card's.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace plant_scene {
constexpr float W = 1000.0f, H = 620.0f;

// ---- What the add-on sends (plant_room.py F_* and S_*).
enum Flag : int {
  F_HEAT_CALL, F_HW_CALL, F_HEAT_PATH, F_DHW_PATH, F_ANY_PRIMARY, F_ACTIVE, F_DHW_FLOW, F_MAINS, F_TANK_COLD,
  F_HOUSE_COLD, F_WATER, F_HOT_DRAW, F_FIRING, F_IMMERSION, F_CYL, F_STANDBY, F_CONDENSING, F_NEST_OFF,
  F_NEST_HEAT, F_PRESET_NONE, F_PRESET_ECO, F_COIL_FLOW, F_COIL_RETURN
};
// The texts of `x`, in the order plant_room.py builds them.
enum Slot : int {
  T_SPUR, T_GAS, T_FLOW, T_FLOW_TAG, T_RET, T_RET_TAG, T_DELTA, T_PRESSURE, T_OUTDOOR, T_CURVE, T_LIMITS, T_STATUS,
  T_COND, T_COND2, T_RET_PROBE, T_FLOW_PROBE, T_RATE, T_DRAW_PROBE, T_INTAKE, T_CYL, T_CYL_TAG, T_CIRCUIT, T_MANAGER,
  T_IMM_STATE, T_IMM_W, T_TARGET, T_HOME, T_SPACE, T_HOT, T_DRAW, T_MAINS, T_AQUARO, T_SALT, T_COUNT
};
// The entities a tap opens, in the order of `e` (plant_room.py SCENE_ENTITIES).
enum Ent : int {
  E_RET, E_FLOW, E_DRAW, E_GAS, E_ZONE, E_SPUR, E_RATE, E_PRESSURE, E_OUTSIDE, E_CURVE, E_SIGNAL, E_INTAKE, E_CYL,
  E_CIRCUIT, E_MANAGER, E_IMMERSION, E_HEATERS, E_THERMOSTAT, E_HOT_WATER, E_MAINS, E_SALT, E_COUNT
};
struct Said {
  std::string s;
  uint8_t ink = 0;  // plant_room.py S_DIM..S_PROBE
};
struct Data {
  bool any = false;
  uint32_t flags = 0;
  uint8_t probe[6] = {0, 0, 0, 0, 0, 0};  // flow, return, hot water, intake, immersion, the thermostat's ring
  int hue = -1;                          // the radiators' colour from the house temperature, -1 for none
  int mix_y10 = 2840;                    // where the cylinder's hot water ends, tenths of a unit
  int coil_hot = 0, coil_cold = 1000;    // the coil's gradient stops, thousandths
  std::vector<Said> said;
  std::vector<std::string> entity;
  bool on(Flag f) const { return (flags >> f) & 1u; }
  const std::string &text(Slot s) const {
    static const std::string none;
    return size_t(s) < said.size() ? said[s].s : none;
  }
  uint8_t ink(Slot s) const { return size_t(s) < said.size() ? said[s].ink : 0; }
  const std::string &ent(int e) const {
    static const std::string none;
    return size_t(e) < entity.size() ? entity[e] : none;
  }
};

// ---- Colours: the card's own roles. `under` is what a translucent colour sits over; mix 255 is the colour itself.
enum Role : uint8_t { CARD, SURFACE, BLOCK, BORDER, TXT, DIM, FLOW, COLD, DHW, ELEC, SOFT, PROBE, CTRL, GAS, OK, RGB };
struct Ink {
  Role role = TXT;
  uint8_t mix = 255;      // how much of the colour shows over `under`
  Role under = SURFACE;
  uint32_t rgb = 0;       // for role RGB
  Role alt = TXT;         // a gradient: `role` at 0, `alt` at 255 ...
  uint8_t t = 0;          // ... and this far between them
  bool operator==(const Ink &o) const {
    return role == o.role && mix == o.mix && under == o.under && rgb == o.rgb && alt == o.alt && t == o.t;
  }
};
inline Ink R(Role r, uint8_t mix = 255, Role under = SURFACE) { Ink i; i.role = r; i.mix = mix; i.under = under; return i; }
inline Ink L(uint32_t rgb, uint8_t mix = 255) { Ink i; i.role = RGB; i.rgb = rgb; i.mix = mix; return i; }
inline Ink ink_of(int code) {  // plant_room.py S_DIM, S_TXT, S_FLOW, S_COLD, S_DHW, S_ELEC, S_OK, S_CTRL, S_GAS, S_SOFT, S_PROBE
  static const Role map[] = {DIM, TXT, FLOW, COLD, DHW, ELEC, OK, CTRL, GAS, SOFT, PROBE};
  return R(code >= 0 && code < 11 ? map[code] : DIM);
}
// A pipe that is idle shows at 38 percent over the card's surface.
inline Ink faded(Role r) { return R(r, 97); }

// ---- What it draws, in the card's units, in the order of the card's SVG.
struct Pt { float x, y; };
struct Rect { float x, y, w, h, r; Ink fill; uint8_t fill_opa; Ink edge; float edge_w; };
struct Poly { std::vector<Pt> p; float w; Ink ink; bool round_cap; };       // a stroke through points
struct Blob { std::vector<Pt> p; Ink ink; uint8_t opa; };                    // a filled shape
struct Dot { float x, y, r; Ink fill; uint8_t fill_opa; Ink edge; float edge_w; };
struct Run { std::string s; float size; uint8_t weight; float spacing; Ink ink; float dx, dy; };
struct Txt {
  std::vector<Run> runs;
  float x, y;           // the baseline, or the middle of the line when `centre`
  uint8_t anchor;       // 0 start, 1 middle, 2 end
  bool centre, halo;
  bool turned;          // rotated a quarter turn clockwise about (x, y)
};
enum class Kind : uint8_t { RECT, POLY, BLOB, DOT, TXT };
struct Step { Kind kind; uint16_t index; };
struct Hit { float x, y, w, h; uint8_t kind; int8_t index; };  // kind 0 reading (index = Ent), 1 button (index 0..3)
struct Scene {
  bool ok = false;
  std::vector<Rect> rects;
  std::vector<Poly> polys;
  std::vector<Blob> blobs;
  std::vector<Dot> dots;
  std::vector<Txt> txts;
  std::vector<Step> order;
  std::vector<Hit> hits;
  int pending = -1;
};

// ---- A small SVG path reader: M L H V Q C A and the relative a, c, with Z. Arcs are circular.
struct Sub { std::vector<Pt> p; bool closed = false; };
inline void arc_to(std::vector<Pt> &out, Pt a, float r, bool large, bool sweep, Pt b) {
  const float dx = b.x - a.x, dy = b.y - a.y, d = std::hypot(dx, dy);
  if (d < 1e-4f) return;
  r = std::max(r, d / 2);
  const float h = std::sqrt(std::max(0.0f, r * r - d * d / 4));
  const float sign = large != sweep ? 1.0f : -1.0f;
  const float cx = a.x + dx / 2 + sign * h * (-dy / d), cy = a.y + dy / 2 + sign * h * (dx / d);
  float a0 = std::atan2(a.y - cy, a.x - cx), a1 = std::atan2(b.y - cy, b.x - cx);
  const float pi = 3.14159265f;
  if (sweep) { while (a1 <= a0) a1 += 2 * pi; } else { while (a1 >= a0) a1 -= 2 * pi; }
  const int n = std::max(4, int(std::fabs(a1 - a0) / (pi / 30)) + 1);
  for (int i = 1; i <= n; ++i) {
    const float t = a0 + (a1 - a0) * i / n;
    out.push_back({cx + r * std::cos(t), cy + r * std::sin(t)});
  }
}
inline std::vector<Sub> path(const char *d) {
  std::vector<Sub> subs;
  const char *p = d;
  Pt cur{0, 0}, start{0, 0};
  char cmd = 0;
  auto skip = [&] { while (*p == ' ' || *p == ',') ++p; };
  auto number = [&](float &v) {
    skip();
    char *end = nullptr;
    v = std::strtof(p, &end);
    if (end == p) return false;
    p = end;
    return true;
  };
  auto flag = [&](bool &v) { skip(); if (*p != '0' && *p != '1') return false; v = *p++ == '1'; return true; };
  while (true) {
    skip();
    if (!*p) break;
    if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) cmd = *p++;
    const bool rel = cmd >= 'a';
    const char c = char(rel ? cmd - 32 : cmd);
    float a, b, e, f2, g, h2;
    bool large = false, sweep = false;
    if (c == 'Z') {
      if (!subs.empty()) subs.back().closed = true;
      cur = start;
      continue;
    }
    if (c == 'M') {
      if (!number(a) || !number(b)) break;
      cur = rel ? Pt{cur.x + a, cur.y + b} : Pt{a, b};
      start = cur;
      subs.push_back({{cur}, false});
      cmd = rel ? 'l' : 'L';
    } else if (c == 'L') {
      if (!number(a) || !number(b)) break;
      cur = rel ? Pt{cur.x + a, cur.y + b} : Pt{a, b};
      subs.back().p.push_back(cur);
    } else if (c == 'H') {
      if (!number(a)) break;
      cur.x = rel ? cur.x + a : a;
      subs.back().p.push_back(cur);
    } else if (c == 'V') {
      if (!number(a)) break;
      cur.y = rel ? cur.y + a : a;
      subs.back().p.push_back(cur);
    } else if (c == 'Q') {
      if (!number(a) || !number(b) || !number(e) || !number(f2)) break;
      const Pt c1 = rel ? Pt{cur.x + a, cur.y + b} : Pt{a, b}, p2 = rel ? Pt{cur.x + e, cur.y + f2} : Pt{e, f2};
      for (int i = 1; i <= 12; ++i) {
        const float t = i / 12.0f, u = 1 - t;
        subs.back().p.push_back({u * u * cur.x + 2 * u * t * c1.x + t * t * p2.x, u * u * cur.y + 2 * u * t * c1.y + t * t * p2.y});
      }
      cur = p2;
    } else if (c == 'C') {
      if (!number(a) || !number(b) || !number(e) || !number(f2) || !number(g) || !number(h2)) break;
      const Pt c1 = rel ? Pt{cur.x + a, cur.y + b} : Pt{a, b}, c2 = rel ? Pt{cur.x + e, cur.y + f2} : Pt{e, f2},
               p2 = rel ? Pt{cur.x + g, cur.y + h2} : Pt{g, h2};
      for (int i = 1; i <= 14; ++i) {
        const float t = i / 14.0f, u = 1 - t;
        subs.back().p.push_back({u * u * u * cur.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p2.x,
                                 u * u * u * cur.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p2.y});
      }
      cur = p2;
    } else if (c == 'A') {
      float rx, ry, rot;
      if (!number(rx) || !number(ry) || !number(rot) || !flag(large) || !flag(sweep) || !number(a) || !number(b)) break;
      const Pt to = rel ? Pt{cur.x + a, cur.y + b} : Pt{a, b};
      arc_to(subs.back().p, cur, rx, large, sweep, to);
      cur = to;
    } else {
      break;
    }
  }
  return subs;
}

// ---- The builder.
class Builder {
 public:
  Builder(const Data &d, int pending, float t) : d_(d), t_(t) { sc_.pending = pending; sc_.ok = true; }
  Scene build();

 private:
  const Data &d_;
  float t_;  // seconds, for the card's moving parts
  Scene sc_;

  bool on(Flag f) const { return d_.on(f); }
  void add(Kind k, size_t i) { sc_.order.push_back({k, uint16_t(i)}); }
  void rect(float x, float y, float w, float h, float r, Ink fill, uint8_t fill_opa, Ink edge, float edge_w) {
    sc_.rects.push_back({x, y, w, h, r, fill, fill_opa, edge, edge_w});
    add(Kind::RECT, sc_.rects.size() - 1);
  }
  void block(float x, float y, float w, float h, float r = 12) { rect(x, y, w, h, r, R(BLOCK), 255, R(BORDER), 1.5f); }
  void outline(float x, float y, float w, float h, float r, Ink ink, float sw) { rect(x, y, w, h, r, ink, 0, ink, sw); }
  void stroke(const std::vector<Pt> &p, float w, Ink ink, bool round_cap = false) {
    if (p.size() < 2) return;
    sc_.polys.push_back({p, w, ink, round_cap});
    add(Kind::POLY, sc_.polys.size() - 1);
  }
  void line(float x1, float y1, float x2, float y2, float w, Ink ink, bool round_cap = false) { stroke({{x1, y1}, {x2, y2}}, w, ink, round_cap); }
  void strokes(const char *d, float w, Ink ink, bool round_cap = false) { for (auto &s : path(d)) stroke(s.p, w, ink, round_cap); }
  void fill(const std::vector<Pt> &p, Ink ink, uint8_t opa = 255) {
    if (p.size() < 3) return;
    sc_.blobs.push_back({p, ink, opa});
    add(Kind::BLOB, sc_.blobs.size() - 1);
  }
  void dot(float x, float y, float r, Ink fill_ink, uint8_t fill_opa, Ink edge, float edge_w) {
    sc_.dots.push_back({x, y, r, fill_ink, fill_opa, edge, edge_w});
    add(Kind::DOT, sc_.dots.size() - 1);
  }
  Txt &text_(float x, float y, uint8_t anchor, bool centre = false) {
    sc_.txts.push_back({{}, x, y, anchor, centre, false, false});
    add(Kind::TXT, sc_.txts.size() - 1);
    return sc_.txts.back();
  }
  // The card's text classes.
  enum Cls { LANE, LBL, LBL_SM, SUB, VAL, VAL_BIG, VAL_TINY, TAG, PH, PEN };
  static Run run(Cls c, const std::string &s, Ink ink) {
    switch (c) {
      case LANE: return {s, 10, 7, 1.8f, ink, 0, 0};
      case LBL: return {s, 13, 7, 0, ink, 0, 0};
      case LBL_SM: return {s, 12, 7, 0, ink, 0, 0};
      case SUB: return {s, 11, 5, 0, ink, 0, 0};
      case VAL: return {s, 14, 7, 0, ink, 0, 0};
      case VAL_BIG: return {s, 19, 7, 0, ink, 0, 0};
      case VAL_TINY: return {s, 11, 7, 0, ink, 0, 0};
      case TAG: return {s, 9.5f, 7, 1.33f, ink, 0, 0};
      case PH: return {s, 9.5f, 7, 0.95f, ink, 0, 0};
      case PEN: return {s, 10, 7, 0, ink, 0, 0};
    }
    return {s, 11, 5, 0, ink, 0, 0};
  }
  void say(Cls c, const std::string &s, float x, float y, Ink ink, uint8_t anchor = 0, bool centre = false) {
    if (s.empty()) return;
    text_(x, y, anchor, centre).runs.push_back(run(c, s, ink));
  }
  void say(Cls c, Slot slot, float x, float y, uint8_t anchor = 0, bool centre = false) {
    say(c, d_.text(slot), x, y, ink_of(d_.ink(slot)), anchor, centre);
  }
  void hit(float x, float y, float w, float h, int ent) { sc_.hits.push_back({x, y, w, h, 0, int8_t(ent)}); }
  void button(float x, float y, float w, float h, int index) { sc_.hits.push_back({x, y, w, h, 1, int8_t(index)}); }

  // Dashes along a path: `on_len` drawn, `off_len` skipped, starting `phase` into the pattern. The card's marching dots are
  // 1 on and 15 off with round ends; they are held still here (the card moves them with a clock).
  void dashes(const char *d, float on_len, float off_len, float w, Ink ink, bool round_cap, float phase = 0, float speed = 0) {
    const float period = on_len + off_len;
    for (auto &sub : path(d)) {
      // The pattern slides along the path `speed` units a second.
      float carry = std::fmod(std::fmod(phase - speed * t_, period) + period, period);
      for (size_t i = 1; i < sub.p.size(); ++i) {
        const Pt a = sub.p[i - 1], b = sub.p[i];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len < 1e-4f) continue;
        float pos = 0;
        while (pos < len) {
          const bool inside = carry < on_len;
          const float step = std::min(inside ? on_len - carry : period - carry, len - pos);
          if (inside) stroke({{a.x + (b.x - a.x) * pos / len, a.y + (b.y - a.y) * pos / len},
                              {a.x + (b.x - a.x) * (pos + step) / len, a.y + (b.y - a.y) * (pos + step) / len}}, w, ink, round_cap);
          pos += step;
          carry = std::fmod(carry + step, period);
        }
      }
    }
  }
  void march(const char *d, float on_len, float off_len, float w, Ink ink, float phase = 0) { dashes(d, on_len, off_len, w, ink, true, phase, 16 / 1.5f); }
  // The ring around an open valve, less the part under the labels; it swells from the valve and fades, every two seconds.
  void valve_ring(float cx, float cy) {
    const float u = std::fmod(t_, 2.0f) / 2.0f, e = 1 - (1 - u) * (1 - u);
    const float rad = 20 * (1 + 0.25f * e);
    const uint8_t a = uint8_t(255 * 0.6f * (1 - e));
    std::vector<Pt> ring;
    for (int i = 0; i <= 48; ++i) {
      const float t = float(i) / 48 * 6.2831853f, x = cx + 20 * std::cos(t), y = cy + 20 * std::sin(t);
      const bool masked = (x >= 632 && x <= 676 && y >= 21 && y <= 37) || (x >= 335 && x <= 376 && y >= 154 && y <= 180);
      if (masked) { stroke(ring, 2.5f, R(OK, a), false); ring.clear(); } else ring.push_back({cx + rad * std::cos(t), cy + rad * std::sin(t)});
    }
    stroke(ring, 2.5f, R(OK, a), false);
  }
  // A swing between 0 and 1 and back, `period` seconds for the whole there and back (CSS alternate).
  float swing(float period, float offset = 0) const { return 0.5f - 0.5f * std::cos(6.2831853f * (t_ + offset) / period); }
  void probe_icon(float cx, float cy, Ink ink) {
    dot(cx, cy, 10, R(BLOCK), 255, ink, 2);
    line(cx, cy - 6, cx, cy + 1.5f, 2.2f, ink, true);
    dot(cx, cy + 4, 2.6f, ink, 0, ink, 2);
  }
  // A pipe: its idle form always, the lit form over it for the paths the state names.
  void pipe_base(const char *d, Role r) { for (auto &s : path(d)) stroke(s.p, 8, faded(r)); }
  void pipe_on(const char *d, Role r) { for (auto &s : path(d)) stroke(s.p, 8, R(r)); }
  void ellipse(float cx, float cy, float rx, float ry, float w, Ink ink) {
    std::vector<Pt> p;
    for (int i = 0; i <= 36; ++i) {
      const float t = float(i) / 36 * 6.2831853f;
      p.push_back({cx + rx * std::cos(t), cy + ry * std::sin(t)});
    }
    stroke(p, w, ink, true);
  }
};

inline std::vector<Pt> moved(const std::vector<Sub> &subs, float tx, float ty, float k) {
  std::vector<Pt> out;
  for (auto &s : subs) for (auto &p : s.p) out.push_back({tx + p.x * k, ty + p.y * k});
  return out;
}

inline Scene Builder::build() {
  const Data &d = d_;
  const bool heating = on(F_HEAT_CALL), hot_call = on(F_HW_CALL), heat_path = on(F_HEAT_PATH), dhw_path = on(F_DHW_PATH);
  const bool any_primary = on(F_ANY_PRIMARY), active = on(F_ACTIVE), mains = on(F_MAINS), hot_draw = on(F_HOT_DRAW);
  const bool firing = on(F_FIRING), immersion = on(F_IMMERSION), water = on(F_WATER);
  const float mix_y = d.mix_y10 / 10.0f;
  auto probe_ink = [&](int i) { return ink_of(d.probe[i]); };

  // The card's surface and the lane between the plant room and the house.
  rect(0, 0, W, H, 16, R(SURFACE), 255, R(BORDER), 1);
  {
    // 45 degree hatch over x 700..756, y 44..524, spaced 8 units apart across the lines (11.3 along x).
    const float x0 = 700, x1 = 756, y0 = 44, y1 = 524, step = 8 * 1.41421356f;
    for (float c = x0 + y0; c <= x1 + y1; c += step) {
      // The line x + y = c clipped to the rectangle.
      float ax = std::max(x0, c - y1), bx = std::min(x1, c - y0);
      if (ax >= bx) continue;
      line(ax, c - ax, bx, c - bx, 3, R(BORDER, 140, SURFACE));
    }
  }
  line(700, 44, 700, 524, 1.5f, R(BORDER));
  line(756, 44, 756, 524, 1.5f, R(BORDER));
  {
    Txt &t = text_(724, 173, 1);
    t.runs.push_back(run(LANE, "UNDER FLOOR", R(DIM)));
    t.turned = true;
    Txt &u = text_(724, 361, 1);
    u.runs.push_back(run(LANE, "4 PIPES", R(DIM)));
    u.turned = true;
  }
  say(LANE, "PLANT ROOM · GARAGE", 135, 20, R(DIM), 1);
  say(LANE, "HOUSE", 885, 20, R(DIM), 1);

  // The control signals from the thermostat: dashes, with an arrow where they land.
  auto ctrl = [&](const char *dpath, bool lit) {
    dashes(dpath, lit ? 6 : 5, lit ? 8 : 7, lit ? 1.8f : 1.25f, lit ? R(CTRL, 230) : R(CTRL, 56), true, 0, lit ? 28.0f : 0.0f);
  };
  auto arrow = [&](float x, float y, bool lit) {  // pointing up, tip at the path's end
    const float s = (lit ? 1.8f : 1.25f) * 6 / 8;
    fill({{x, y - s}, {x + 3 * s, y + 6 * s}, {x - 3 * s, y + 6 * s}}, R(CTRL), lit ? 230 : 56);
  };
  ctrl("M790,269 H654 V84 M654,68 V64", heating);
  arrow(654, 64, heating);
  ctrl("M790,297 H654 V407 M654,423 V457 M654,473 V494 H495", hot_call);
  ctrl("M479,494 H355 V210", hot_call);
  arrow(355, 210, hot_call);
  const uint8_t pulse = uint8_t(255 * (0.55f + 0.45f * swing(1.35f)) * 0.85f);
  if (heating) fill({{649, 68}, {654, 60}, {659, 68}}, R(CTRL), pulse);
  if (hot_call) fill({{351, 214}, {355, 206}, {359, 214}}, R(CTRL), pulse);

  // Flow pipes.
  pipe_base("M230,104 H260 M260,104 H300 V52 H790 M300,104 V194 H392", FLOW);
  if (any_primary) pipe_on("M230,104 H260", FLOW);
  if (heat_path) pipe_on("M260,104 H300 V52 H790", FLOW);
  if (dhw_path) { pipe_on("M260,104 H300", FLOW); pipe_on("M300,104 V194 H392", FLOW); }
  {
    // The S-plan's hub: a disc of 11 cut flat at x 304.
    std::vector<Pt> hub;
    const float cx = 300, cy = 104, r = 11, cut = std::acos(4.0f / r);
    for (int i = 0; i <= 30; ++i) {
      const float t = cut + (6.2831853f - 2 * cut) * i / 30;
      hub.push_back({cx + r * std::cos(t), cy + r * std::sin(t)});
    }
    fill(hub, R(SURFACE));
    fill(hub, R(FLOW), any_primary ? 255 : 97);
    if (!heating) rect(295, 93.4f, 10, 3.2f, 1.6f, R(CARD), 255, R(CARD), 0);
    if (!hot_call) rect(295, 111.4f, 10, 3.2f, 1.6f, R(CARD), 255, R(CARD), 0);
  }
  if (heat_path) march("M230,104 H288 Q300,104 300,92 V52 H790", 1, 15, 3.5f, L(0xFFD0CC));
  if (dhw_path) march("M230,104 H288 Q300,104 300,116 V194 H392", 1, 15, 3.5f, L(0xFFD0CC));

  // Return pipes.
  pipe_base("M790,76 H324 V184 M324,204 V420 H230 M392,420 H324", COLD);
  if (heat_path) {
    pipe_on("M790,76 H324 V184 M324,204 V420 H230", COLD);
    if (dhw_path) pipe_on("M392,420 H324", COLD);
    march("M790,76 H324 V184 M324,204 V420 H230", 1, 15, 3.5f, L(0xFFFFFF));
    if (dhw_path) march("M392,420 H324", 1, 15, 3.5f, L(0xFFFFFF));
  } else if (dhw_path) {
    pipe_on("M392,420 H230", COLD);
    march("M392,420 H230", 1, 15, 3.5f, L(0xFFFFFF));
  }
  hit(240, 406, 70, 60, E_RET);
  probe_icon(275, 420, probe_ink(1));
  say(TAG, "RETURN", 275, 444, R(DIM), 1);
  say(SUB, T_RET_PROBE, 275, 457, 1);

  // Hot water to the house, with its probe.
  pipe_base("M487,124 V100 H608 V415 H900", DHW);
  if (on(F_DHW_FLOW)) {
    pipe_on("M487,124 V100 H608 V415 H900", DHW);
    march("M487,124 V100 H608 V415 H900", 1, 15, 3.5f, L(0xFFE4BB));
  }
  hit(596, 278, 38, 106, E_DRAW);
  hit(596, 384, 94, 48, E_DRAW);
  probe_icon(608, 415, probe_ink(2));
  {
    Txt &t = text_(635, 411, 0);
    t.halo = true;
    t.runs.push_back(run(SUB, d.text(T_DRAW_PROBE), ink_of(d.ink(T_DRAW_PROBE))));
    Txt &u = text_(621, 344, 1);
    u.runs.push_back(run(PH, "HOT WATER OUTLET", R(PROBE)));
    u.turned = true;
  }

  // Cold mains.
  pipe_base("M10,561 H40 M230,561 H260 M450,561 H487 V444 M487,465 H900", COLD);
  if (mains) {
    pipe_on("M10,561 H40 M230,561 H260 M450,561 H487 V465", COLD);
    march("M10,561 H40", 1, 15, 3.5f, L(0xCFE8FF));
    march("M230,561 H260", 1, 15, 3.5f, L(0xCFE8FF));
    march("M450,561 H487 V465", 1, 15, 3.5f, L(0xCFE8FF));
  }
  if (on(F_TANK_COLD)) { pipe_on("M487,465 V444", COLD); march("M487,465 V444", 1, 15, 3.5f, L(0xCFE8FF)); }
  if (on(F_HOUSE_COLD)) { pipe_on("M487,465 H900", COLD); march("M487,465 H900", 1, 15, 3.5f, L(0xCFE8FF)); }

  // Gas in.
  hit(0, 406, 38, 38, E_GAS);
  march("M0,420 H40", 4, 3, 8, R(GAS, 140));
  dot(20, 420, 6, R(BLOCK), 255, firing ? R(SOFT, uint8_t(255 * (0.72f + 0.28f * swing(1.6f)))) : R(GAS), firing ? 1.5f + 1.3f * swing(1.6f) : 1.5f);
  say(TAG, "GAS", 20, 438, R(GAS), 1);

  // The four pipe numbers.
  struct Pen { float y; Role r; const char *n; };
  for (const Pen &p : {Pen{465, COLD, "1"}, Pen{415, DHW, "2"}, Pen{52, FLOW, "3"}, Pen{76, COLD, "4"}}) {
    dot(728, p.y, 9, R(CARD), 255, R(p.r), 1.5f);
    say(PEN, p.n, 728, p.y + 4, R(TXT), 1);
  }

  // The boiler.
  block(40, 32, 190, 460);
  if (on(F_STANDBY)) {
    // The standby mark: a drop struck through, 55 percent of its size at (204, 64).
    auto drop = path("M0 -28C12 -15 17 -6 17 5a17 17 0 0 1-34 0C-17 -5 -7 -12 0 -28z");
    stroke(moved(drop, 204, 64, 0.55f), 1.4f, L(0x5FB2CC), true);
    line(204 - 20 * 0.55f, 64 + 20 * 0.55f, 204 + 20 * 0.55f, 64 - 28 * 0.55f, 1.4f, L(0x5FB2CC), true);
  }
  if (firing) {
    const float glow = 0.5f + 0.5f * std::sin(t_ * 6.2831853f / 4.4f);  // the card's glow, 2.2 s each way
    dot(204, 64, 20, L(0xF08A3C), uint8_t(12 + 24 * glow), L(0xF08A3C), 0);
    dot(204, 64, 14, L(0xF08A3C), 30, L(0xF08A3C), 0);
    dot(204, 64, 9, L(0xF08A3C), 40, L(0xF08A3C), 0);
    fill(moved(path("M0 -28C12 -15 17 -6 17 5a17 17 0 0 1-34 0C-17 -5 -7 -12 0 -28z"), 204, 64, 0.55f), L(0xF08A3C));
    fill(moved(path("M0 -11C6 -4 9 0 9 6a9 9 0 0 1-18 0C-9 0 -3 -4 0 -11z"), 204, 64, 0.55f), L(0xFFD36B));
  }
  rect(227, 98, 5, 12, 1.5f, R(BLOCK), 255, R(FLOW), 1.5f);
  rect(227, 414, 5, 12, 1.5f, R(BLOCK), 255, R(COLD), 1.5f);
  rect(38, 414, 5, 12, 1.5f, R(BLOCK), 255, R(GAS), 1.5f);
  if (active) outline(40, 32, 190, 460, 12, R(FLOW), 2.5f);
  hit(50, 42, 170, 38, E_ZONE);
  rect(56, 50, 18, 24, 3, R(DIM), 0, R(DIM), 1.8f);
  line(59, 58, 71, 58, 1.8f, R(DIM), true);
  dot(65, 66, 3, R(DIM), 0, R(DIM), 1.8f);
  line(61, 74, 61, 77, 1.8f, R(DIM), true);
  line(69, 74, 69, 77, 1.8f, R(DIM), true);
  say(LBL, "Vaillant 635", 84, 66, R(TXT));
  line(56, 84, 214, 84, 1, R(BORDER));
  hit(50, 91, 78, 42, E_SPUR);
  say(VAL, T_SPUR, 56, 110);
  say(TAG, "SPUR", 56, 125, R(DIM));
  hit(132, 91, 88, 42, E_GAS);
  say(VAL, T_GAS, 214, 110, 2);
  say(TAG, "LIVE GAS", 214, 125, R(DIM), 2);
  line(56, 134, 214, 134, 1, R(BORDER));
  hit(50, 140, 170, 42, E_FLOW);
  say(VAL_BIG, T_FLOW, 56, 160);
  say(TAG, d.text(T_FLOW_TAG), 56, 174, R(DIM));
  hit(50, 194, 170, 42, E_RET);
  say(VAL_BIG, T_RET, 56, 214);
  say(TAG, d.text(T_RET_TAG), 56, 228, R(DIM));
  hit(50, 239, 170, 42, E_FLOW);
  say(VAL, T_DELTA, 56, 258);
  say(TAG, "FLOW MINUS RETURN", 56, 272, R(DIM));
  line(56, 286, 214, 286, 1, R(BORDER));
  hit(50, 292, 170, 42, E_PRESSURE);
  say(VAL, T_PRESSURE, 56, 312);
  say(TAG, "SYSTEM PRESSURE", 56, 326, R(DIM));
  hit(50, 337, 100, 25, E_OUTSIDE);
  say(SUB, T_OUTDOOR, 56, 354);
  hit(152, 337, 68, 25, E_CURVE);
  say(SUB, T_CURVE, 214, 354, 2);
  say(SUB, T_LIMITS, 56, 376);
  line(56, 396, 214, 396, 1, R(BORDER));
  hit(50, 402, 170, 28, E_SIGNAL);
  say(SUB, T_STATUS, 56, 420);
  hit(50, 440, 170, 45, E_RET);
  say(TAG, T_COND, 56, 458);
  say(TAG, T_COND2, 56, 478);

  // The flow probe and the S-plan's label.
  hit(236, 90, 52, 58, E_FLOW);
  probe_icon(260, 104, probe_ink(0));
  say(TAG, "FLOW", 263, 130, R(DIM), 1);
  say(SUB, T_FLOW_PROBE, 263, 143, 1);
  hit(236, 148, 54, 14, E_RATE);
  say(SUB, T_RATE, 263, 157, 1);
  say(TAG, "S-PLAN", 263, 84, R(DIM), 1);

  // Zone valve 1, heating.
  rect(642, 40, 24, 24, 4, R(BLOCK), 255, heating ? R(FLOW) : R(BORDER), heating ? 2 : 1.5f);
  if (heating) {
    line(646, 52, 662, 52, 3, R(OK), true);
    valve_ring(654, 52);
  } else {
    line(654, 44, 654, 60, 3, R(FLOW), true);
  }
  say(TAG, "ZV1", 648, 33, R(DIM), 2);
  say(TAG, "-", 654, 33, R(DIM), 1);
  say(TAG, "HTG", 660, 33, R(DIM), 0);
  // Zone valve 2, hot water.
  rect(343, 182, 24, 24, 4, R(BLOCK), 255, hot_call ? R(DHW) : R(BORDER), hot_call ? 2 : 1.5f);
  if (hot_call) {
    line(347, 194, 363, 194, 3, R(OK), true);
    valve_ring(355, 194);
  } else {
    line(355, 186, 355, 202, 3, R(FLOW), true);
  }
  say(TAG, "ZV2", 355.7f, 164, R(DIM), 1);
  say(TAG, "DHW", 355.7f, 176, R(DIM), 1);

  // The cylinder: the body, the hot and cold water in it, its ports.
  block(392, 124, 190, 320, 20);
  {
    // The mix, soft at the thermocline: hot above, cold below, drawn as strips inside the rounded body.
    const float left = 393, right = 581, top = 125, bottom = 443, rr = 19;
    for (float y = top; y < bottom; y += 2) {
      const float t = std::clamp((y + 1 - (mix_y - 22)) / 44.0f, 0.0f, 1.0f), s = t * t * (3 - 2 * t);
      float inset = 0;
      if (y < top + rr) inset = rr - std::sqrt(std::max(0.0f, rr * rr - (top + rr - y - 1) * (top + rr - y - 1)));
      if (y > bottom - rr) inset = rr - std::sqrt(std::max(0.0f, rr * rr - (y + 1 - (bottom - rr)) * (y + 1 - (bottom - rr))));
      const int cold_part = int(std::lround(s * 255));
      if (cold_part < 255) rect(left + inset, y, right - left - 2 * inset, 2, 0, L(0xF7BF86), uint8_t(100 * (255 - cold_part) / 255), L(0xF7BF86), 0);
      if (cold_part) rect(left + inset, y, right - left - 2 * inset, 2, 0, L(0xA6D5E3), uint8_t(100 * cold_part / 255), L(0xA6D5E3), 0);
    }
    // Cold water coming in at the bottom while a tap is drawing: three soft plumes that rise and fade.
    if (on(F_TANK_COLD)) {
      for (int i = 0; i < 3; ++i) {
        const float rx0 = i == 0 ? 24 : i == 1 ? 30 : 33, ry0 = i == 0 ? 19 : i == 1 ? 23 : 25;
        const float u = std::fmod(t_ + 1.1f * i, 3.4f) / 3.4f, e = 1 - (1 - u) * (1 - u);
        const float sc = 0.7f + 0.6f * u, cy = 440 - 143 * e, op = 0.7f * (1 - u);
        for (int ring = 5; ring >= 1; --ring) {
          const float f = ring / 5.0f;
          std::vector<Pt> p;
          for (int k = 0; k < 20; ++k) {
            const float a = k / 20.0f * 6.2831853f;
            p.push_back({487 + rx0 * sc * f * std::cos(a), std::min(443.0f, cy + ry0 * sc * f * std::sin(a))});
          }
          fill(p, L(0x5FB2CC), uint8_t(255 * op * 0.9f * 0.22f));
        }
      }
    }
    // Bubbles climbing the whole tank, each at its own pace.
    {
      struct Bub { float x, r, dur, off, drift; };
      for (const Bub &b : {Bub{410, 3, 9, 0, 6}, Bub{429, 4.5f, 9.6f, 1.4f, -8}, Bub{446, 3.5f, 10, 2.6f, 10}, Bub{466, 5, 10.8f, 0.8f, -12},
                           Bub{484, 6, 12, 3.4f, 14}, Bub{503, 4, 12.8f, 4.2f, -10}, Bub{520, 5.5f, 13.5f, 2, 6}, Bub{539, 3, 14.4f, 5.6f, -8},
                           Bub{558, 4.5f, 15, 7.2f, 14}}) {
        const float u = std::fmod(t_ + b.off, b.dur) / b.dur, e = u * u * (3 - 2 * u);
        const float op = u < 0.08f ? 0.4f * u / 0.08f : u > 0.9f ? 0.4f * (1 - u) / 0.1f : 0.4f;
        dot(b.x + b.drift * e, 436 - 306 * e, b.r, L(0xFFFFFF), uint8_t(255 * op), L(0xFFFFFF), 0);
      }
    }
    // Two waves on the surface of the water.
    for (int w = 0; w < 2; ++w) {
      std::vector<Pt> p;
      for (float x = 393; x <= 581; x += 4) p.push_back({x, mix_y - 3.85f * std::sin((x - 392 + (w ? 96 * t_ / 8 : -96 * t_ / 5)) / 96 * 6.2831853f)});
      stroke(p, w ? 1.7f : 2.8f, L(0xFFFFFF, w ? 102 : 178), true);
    }
  }
  if (dhw_path) {
    // Charging: the heat soaks in from the coil's side.
    for (float x = 412; x < 562; x += 3) {
      const float a = 0.8f * (1 - (x - 412) / 150) * (0.5f + 0.4f * swing(5.2f));
      rect(x, 188, 3, 238, 0, L(0xF08A5D), uint8_t(a * 255), L(0xF08A5D), 0);
    }
  }
  rect(389, 414, 5, 12, 1.5f, R(BLOCK), 255, R(COLD), 1.5f);
  rect(481, 121, 12, 5, 1.5f, R(BLOCK), 255, R(DHW), 1.5f);
  rect(481, 442, 12, 5, 1.5f, R(BLOCK), 255, R(COLD), 1.5f);
  rect(389, 188, 5, 12, 1.5f, R(BLOCK), 255, R(FLOW), 1.5f);
  if (dhw_path || immersion) outline(392, 124, 190, 320, 20, R(DHW), 2.5f);
  rect(420, 138, 18, 24, 6, R(DHW), 0, R(DHW), 1.8f);
  strokes("M426,138 V134 H432 V138", 1.8f, R(DHW), true);
  strokes("M423,155 Q429,159 435,155", 1.8f, R(DHW, 204), true);
  strokes("M424,162 V166 M434,162 V166", 1.8f, R(DHW), true);
  say(LBL, "Megaflo 300L", 448, 154, R(TXT));
  say(SUB, "unvented indirect cylinder", 487, 177, R(DIM), 1);
  hit(236, 176, 76, 36, E_INTAKE);
  probe_icon(300, 194, probe_ink(3));
  say(TAG, "INTAKE", 263, 190, R(DIM), 1);
  say(SUB, T_INTAKE, 263, 204, 1);

  // The coil, coloured by where the hot water ends, and the cylinder's own reading.
  hit(408, 184, 158, 88, E_CYL);
  if (dhw_path) rect(412, 188, 150, 72, 18, R(FLOW), 15, R(FLOW), 0);
  {
    const uint8_t coil_mix = dhw_path ? 255 : 122;
    const float hot_at = d.coil_hot / 1000.0f, cold_at = d.coil_cold / 1000.0f;
    auto coil = [&](const char *dpath) {
      for (auto &s : path(dpath)) {
        // Walk the path in short pieces and colour each by its height on the gradient from y 194 to 420.
        auto tone = [&](float y) {
          if (on(F_COIL_RETURN)) return R(COLD, coil_mix);
          if (on(F_COIL_FLOW)) return R(FLOW, coil_mix);
          const float t = std::clamp((y - 194) / 226.0f, 0.0f, 1.0f);
          if (t <= hot_at) return R(FLOW, coil_mix);
          if (t >= cold_at) return R(COLD, coil_mix);
          Ink i = R(FLOW, coil_mix);
          i.alt = COLD;
          i.t = uint8_t((t - hot_at) / std::max(0.001f, cold_at - hot_at) * 255);  // how far from flow to return
          return i;
        };
        std::vector<Pt> run_pts;
        Ink run_ink = tone(s.p[0].y);
        run_pts.push_back(s.p[0]);
        for (size_t i = 1; i < s.p.size(); ++i) {
          const Pt a = s.p[i - 1], b = s.p[i];
          const float len = std::hypot(b.x - a.x, b.y - a.y);
          const int n = std::max(1, int(len / 4));
          for (int k = 1; k <= n; ++k) {
            const Pt q{a.x + (b.x - a.x) * k / n, a.y + (b.y - a.y) * k / n};
            const Ink t = tone((run_pts.back().y + q.y) / 2);
            if (!(t == run_ink)) { run_pts.push_back(q); stroke(run_pts, 5.5f, run_ink); run_pts.assign(1, q); run_ink = t; }
            else run_pts.push_back(q);
          }
        }
        stroke(run_pts, 5.5f, run_ink);
      }
    };
    coil("M422,194 H523 A29,29 0 0 1 523,252 H433");
    if (dhw_path) march("M422,194 H523 A29,29 0 0 1 523,252 H433", 1, 15, 2, L(0xFFD0CC));
    coil("M433,252 A11,11 0 0 0 433,274 H541 A11,11 0 0 1 541,296 H433 A11,11 0 0 0 433,318 H541 A11,11 0 0 1 541,340 H433 A11,11 0 0 0 433,362 H523 A29,29 0 0 1 523,420 H422");
    if (dhw_path) march("M433,252 A11,11 0 0 0 433,274 H541 A11,11 0 0 1 541,296 H433 A11,11 0 0 0 433,318 H541 A11,11 0 0 1 541,340 H433 A11,11 0 0 0 433,362 H523 A29,29 0 0 1 523,420 H422", 1, 15, 2, L(0xFFFFFF), 5.9f);
  }
  say(VAL_BIG, T_CYL, 487, 223, 1, true);
  say(TAG, T_CYL_TAG, 487, 263, 1, true);
  hit(430, 275, 114, 20, E_CIRCUIT);
  {
    Txt &t = text_(487, 285, 1, true);
    t.runs.push_back(run(TAG, "CIRCUIT ", R(DIM)));
    Run v = run(VAL_TINY, d.text(T_CIRCUIT), ink_of(d.ink(T_CIRCUIT)));
    v.dx = 8;
    t.runs.push_back(v);
  }
  hit(430, 297, 114, 20, E_MANAGER);
  {
    Txt &t = text_(487, 307, 1, true);
    t.runs.push_back(run(TAG, "MANAGER ", R(DIM)));
    Run v = run(VAL_TINY, d.text(T_MANAGER), ink_of(d.ink(T_MANAGER)));
    v.dx = 8;
    t.runs.push_back(v);
  }

  // The immersion heater.
  if (immersion) rect(393, 355, 188, 88, 18, R(ELEC), 26, R(ELEC), 0);
  hit(404, 332, 166, 86, E_IMMERSION);
  say(TAG, "IMMERSION · SPUR", 487, 351, R(DIM), 1, true);
  say(SUB, T_IMM_STATE, 467.125f, 384.5f, 1, true);
  say(SUB, T_IMM_W, 467.125f, 398.5f, 1, true);
  {
    const Ink c = probe_ink(4);
    const float k = 0.75f, tx = 497.75f, ty = 375;
    auto P = [&](float x, float y) { return Pt{tx + x * k, ty + y * k}; };
    for (float cx : {20.0f, 27.0f, 34.0f}) {
      std::vector<Pt> p;
      for (int i = 0; i <= 36; ++i) {
        const float t = float(i) / 36 * 6.2831853f;
        p.push_back(P(cx + 7 * std::cos(t), 22 + 19 * std::sin(t)));
      }
      stroke(p, 3 * k, c, true);
    }
    stroke({P(12, 9), P(15, 9)}, 3 * k, c, true);
    stroke({P(12, 35), P(15, 35)}, 3 * k, c, true);
    rect(tx + 6 * k, ty + 5 * k, 6 * k, 8 * k, 1, c, 255, c, 0);
    rect(tx + 6 * k, ty + 31 * k, 6 * k, 8 * k, 1, c, 255, c, 0);
    stroke({P(0, 9), P(6, 9)}, 2 * k, c, true);
    stroke({P(0, 35), P(6, 35)}, 2 * k, c, true);
  }

  // The radiators.
  block(790, 32, 190, 160);
  if (heat_path) outline(790, 32, 190, 160, 12, R(FLOW), 2.5f);
  hit(804, 40, 162, 50, E_HEATERS);
  {
    Ink rad = R(DIM);
    if (d.hue >= 0) {
      // hsl(hue 78% 62%)
      const float h = d.hue / 60.0f, s = 0.78f, l = 0.62f, c = (1 - std::fabs(2 * l - 1)) * s, x = c * (1 - std::fabs(std::fmod(h, 2.0f) - 1)), m = l - c / 2;
      float r = 0, g = 0, b = 0;
      const int sector = int(h);
      if (sector == 0) { r = c; g = x; } else if (sector == 1) { r = x; g = c; } else if (sector == 2) { g = c; b = x; }
      else if (sector == 3) { g = x; b = c; } else if (sector == 4) { r = x; b = c; } else { r = c; b = x; }
      rad = L((uint32_t(std::lround((r + m) * 255)) << 16) | (uint32_t(std::lround((g + m) * 255)) << 8) | uint32_t(std::lround((b + m) * 255)));
    }
    for (float x : {807.0f, 814.0f, 821.0f, 828.0f}) line(x, 53, x, 88, 3.2f, rad, true);
    say(VAL, "Radiators", 839, 62, R(TXT));
    sc_.txts.back().runs[0].size = 15;
    {
      Txt &t = text_(839, 81, 0);
      Run a{"TARGET ", 8.7f, 7, 1.04f, R(DIM), 0, 0};
      Run b{d.text(T_TARGET), 8.7f, 7, 0, R(TXT), 0, 0};
      t.runs.push_back(a);
      t.runs.push_back(b);
    }
    {
      Txt &t = text_(966, 71, 2);
      const std::string home = d.text(T_HOME);
      t.runs.push_back({home, 22, 7, -0.44f, d.hue >= 0 ? rad : R(TXT), 0, 0});
      if (home != "n/a" && !home.empty()) t.runs.push_back({"°", 10, 7, 0, d.hue >= 0 ? rad : R(TXT), 1, -7});
    }
  }
  line(804, 97, 966, 97, 1, R(BORDER));
  say(TAG, "HVAC", 804, 123, R(DIM), 0, true);
  struct Key { float x, y; const char *name; bool lit; Role tint; uint8_t tint_opa; int index; };
  const Key keys[] = {
      {843, 108, "OFF", on(F_NEST_OFF), TXT, 43, 0},
      {907, 108, "HEAT", on(F_NEST_HEAT), FLOW, 48, 1},
      {843, 144, "NONE", on(F_PRESET_NONE), TXT, 43, 2},
      {907, 144, "ECO", on(F_PRESET_ECO), COLD, 46, 3},
  };
  for (const Key &k : keys) {
    button(k.x - 3, k.y - 3, 62, 36, k.index);
    const bool asking = sc_.pending == k.index;
    rect(k.x, k.y, 56, 30, 9, R(BLOCK), 0, asking ? R(ELEC) : R(BORDER), asking ? 2.0f : 1.2f);
    if (k.lit) rect(k.x + 4, k.y + 4, 48, 22, 6, R(k.tint), k.tint_opa, R(k.tint), 0);
    Ink text = k.lit ? (k.tint == TXT ? R(TXT) : R(k.tint)) : R(DIM);
    say(TAG, asking ? "SURE?" : k.name, k.x + 28, k.y + 15, text, 1, true);
    auto &run0 = sc_.txts.back().runs[0];
    run0.size = 8.5f; run0.spacing = 0.34f;
  }
  say(TAG, "SET", 804, 159, R(DIM), 0, true);

  // The thermostat.
  block(790, 204, 190, 120);
  hit(804, 212, 162, 36, E_THERMOSTAT);
  if (heating || hot_call) outline(790, 204, 190, 120, 12, R(CTRL), 2);
  dot(866, 228, 9, R(CARD), 0, probe_ink(5), 2.5f);
  say(LBL, "Nest", 884, 233, R(TXT));
  line(804, 248, 966, 248, 1, R(BORDER));
  hit(804, 255, 162, 27, E_THERMOSTAT);
  say(SUB, "Space heating", 804, 273, R(DIM));
  say(VAL_TINY, T_SPACE, 966, 273, 2);
  hit(804, 283, 162, 27, E_HOT_WATER);
  say(SUB, "Hot water", 804, 301, R(DIM));
  say(VAL_TINY, T_HOT, 966, 301, 2);

  // Water outlets.
  block(790, 336, 190, 156);
  if (water) outline(790, 336, 190, 156, 12, hot_draw ? R(DHW) : R(COLD), 2.5f);
  {
    const Ink c = water ? R(COLD) : R(DIM);
    strokes("M804,362 H822 Q828,362 828,368 V379", 2.3f, c, true);
    strokes("M810,362 V356 H822 V362", 2.3f, c, true);
    strokes("M816,356 V352 M812,352 H820", 2.3f, c, true);
  }
  say(LBL, "Water Outlets", 846, 363, R(TXT));
  say(SUB, "domestic hot & cold", 846, 380, R(DIM));
  line(804, 392, 966, 392, 1, R(BORDER));
  hit(804, 393, 162, 45, E_DRAW);
  hit(804, 439, 162, 48, E_MAINS);
  say(TAG, "HOT WATER", 885, 405, hot_draw ? R(DHW) : R(DIM), 1);
  say(VAL_TINY, T_DRAW, 885, 422, 1);
  line(804, 438, 966, 438, 1, R(BORDER));
  say(TAG, "MAINS WATER", 885, 456, mains ? R(COLD) : R(DIM), 1);
  say(VAL_TINY, T_MAINS, 885, 474, 1);

  // Mains in and the softener.
  block(40, 516, 190, 90);
  if (mains) outline(40, 516, 190, 90, 12, R(COLD), 2.5f);
  hit(50, 526, 170, 70, E_MAINS);
  if (mains) dot(70, 561, 13, R(COLD), 90, R(COLD), 0);
  dot(70, 561, 11, R(CARD), 255, R(CARD), 0);
  dot(70, 561, 13, R(COLD), 0, R(COLD), 1.5f);
  say(TAG, "MAINS IN", 94, 546, R(DIM));
  say(LBL_SM, "Aquaro", 94, 564, R(TXT));
  say(SUB, T_AQUARO, 94, 580);
  block(260, 516, 190, 90);
  hit(270, 526, 170, 70, E_SALT);
  if (mains) outline(260, 516, 190, 90, 12, R(COLD), 2.5f);
  rect(276, 540, 22, 42, 7, R(SOFT), 0, R(SOFT), 2.5f);
  rect(304, 540, 22, 42, 7, R(SOFT, 115), 0, R(SOFT, 115), 2.5f);
  say(LBL_SM, "Kinetico", 338, 546, R(TXT));
  say(SUB, "2020c · block salt", 338, 564, R(DIM));
  say(SUB, T_SALT, 338, 580);

  // The key.
  block(610, 516, 370, 90);
  say(TAG, "SYSTEM KEY", 630, 538, R(DIM));
  struct Key2 { float x, y; Role r; const char *n; const char *label; };
  for (const Key2 &k : {Key2{630, 562, COLD, "1", "Cold feed"}, Key2{630, 588, DHW, "2", "Hot water supply"},
                        Key2{750, 562, FLOW, "3", "Heating flow"}, Key2{750, 588, COLD, "4", "Heating return"}}) {
    dot(k.x, k.y, 7, R(CARD), 0, R(k.r), 1.5f);
    say(PEN, k.n, k.x, k.y + 4, R(TXT), 1);
    say(SUB, k.label, k.x + 16, k.y + 4, R(DIM));
  }
  dashes("M860,562 H882", 5, 4, 2, R(CTRL), true);
  say(SUB, "Control signal", 890, 566, R(DIM));
  march("M860,588 H882", 4, 3, 2, R(GAS));
  say(SUB, "Gas supply", 890, 592, R(DIM));
  return sc_;
}

inline Scene build(const Data &d, int pending = -1, float t = 0) {
  Builder b(d, pending, t);
  return b.build();
}

inline const Hit *hit(const Scene &s, float x, float y) {
  const Hit *best = nullptr;
  float best_area = 0;
  for (const auto &h : s.hits) {
    if (x < h.x || x >= h.x + h.w || y < h.y || y >= h.y + h.h) continue;
    const float area = h.w * h.h;
    if (!best || area <= best_area) { best = &h; best_area = area; }
  }
  return best;
}
}  // namespace plant_scene
