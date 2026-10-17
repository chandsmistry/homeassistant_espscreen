// clang++ -std=c++17 -Wall -Wextra -Werror -I. tests/test_plant_card.cpp -o /tmp/test_plant_card && /tmp/test_plant_card
#include "../components/smart_display/plant_card.h"
#include <cassert>
#include <cstdio>

using namespace plant_card;

static int checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static int line(int size) { return (int) std::lround(size * 1.172); }
static int wide(const std::string &s, int size) {
  double em = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = s[i];
    if (c >= 0xF0) { em += 1.0; i += 3; }
    else if (c >= 0xE0) { em += 0.9; i += 2; }
    else if (c >= 0xC0) { em += 0.6; i += 1; }
    else if (c == ' ') em += 0.25;
    else if (c >= 'A' && c <= 'Z') em += 0.65;
    else em += 0.56;
  }
  return (int) std::lround(em * size);
}
struct Board { const char *name; int dpi, body, small_; };
static const Board BOARDS[] = {{"cyd", 143, 14, 11}, {"guition", 170, 21, 16}, {"p4", 149, 19, 15}};
static Measure measure(const Board &b) {
  Measure m;
  m.line[BODY] = line(b.body); m.line[SMALL] = line(b.small_);
  const int sizes[2] = {b.body, b.small_};
  m.width = [sizes](Face f, const std::string &s) { return wide(s, sizes[f]); };
  m.schematic_w = int(b.dpi * 2.5); m.schematic_h = int(b.dpi * 1.9);
  m.touch = int(b.dpi * 0.28);
  return m;
}

static Data sample() {
  Data d;
  d.any = true; d.boiler = 2; d.boiler_colour = 2; d.boiler_text = "Firing"; d.confirm = "Tap again to confirm";
  d.tabs = {{"heat", "Calling for heat", 2, {{"Flow", "45 °C", "sensor.flow", 2}, {"Flow target", "50 °C", "sensor.target", 0}, {"Return", "40 °C", "sensor.ret", 4},
                                              {"Pressure", "1.5 bar", "sensor.bar", 1}, {"Gas", "4.2 kW", "sensor.gas", 2}, {"Outside", "9 °C", "sensor.out", 0},
                                              {"House", "20.5 °C", "sensor.house", 0}}},
            {"hot", "Hot draw", 3, {{"Cylinder (est.)", "52 °C", "sensor.cyl", 2}, {"Megaflo intake", "40 °C", "sensor.in", 3}}},
            {"cold", "Mains running", 4, {{"Mains flow", "2.1 L/min", "sensor.mains", 4}, {"Softener salt", "OK", "binary_sensor.salt", 1}}}};
  d.controls = {{"Radiators off", "climate.set_hvac_mode", "hvac_mode", "off", false}, {"Radiators on", "climate.set_hvac_mode", "hvac_mode", "heat", true},
                {"Preset normal", "climate.set_preset_mode", "preset_mode", "none", true}, {"Preset eco", "climate.set_preset_mode", "preset_mode", "eco", false}};
  d.map.flow = 45; d.map.ret = 40; d.map.cylinder = 52; d.map.mains = 2.1f; d.map.bar = 1.5f; d.map.thermocline = 70;
  d.map.firing = d.map.zone1 = d.map.mains_active = true;
  return d;
}

static bool inside(const Scene &s, int w, int h) {
  for (auto &b : s.boxes) if (b.x < 0 || b.y < 0 || b.x + b.w > w || b.y + b.h > h) { std::printf("box %d,%d %dx%d\n", b.x, b.y, b.w, b.h); return false; }
  for (auto &t : s.texts) if (t.x < 0 || t.y < 0 || t.x + t.w > w || t.y > h) { std::printf("text %s %d,%d w%d\n", t.s.c_str(), t.x, t.y, t.w); return false; }
  for (auto &g : s.segs) if (g.x1 < 0 || g.x2 > w || g.y1 < 0 || g.y2 > h) { std::printf("seg %d,%d-%d,%d\n", g.x1, g.y1, g.x2, g.y2); return false; }
  return true;
}

int main() {
  const Data d = sample();
  for (auto &b : BOARDS) {
    const Measure m = measure(b);
    // A small tile: the tabs.
    const int w = int(b.dpi * 1.9), h = int(b.dpi * 1.7);
    Scene s = build(d, m, w, h);
    CHECK(s.ok && !s.schematic);
    CHECK(inside(s, w, h));
    int tabs = 0;
    for (auto &x : s.hits) if (x.kind == HitKind::TAB) ++tabs;
    CHECK(tabs == 3);
    // The rows never run into the buttons: every row text lies above the first button.
    int top = h;
    for (auto &x : s.hits) if (x.kind == HitKind::CONTROL) top = std::min(top, x.y);
    for (auto &x : s.hits) if (x.kind == HitKind::ROW) CHECK(x.y + x.h <= top);
    // The other tabs carry no buttons.
    Scene hot = build(d, m, w, h, 1);
    for (auto &x : hot.hits) CHECK(x.kind != HitKind::CONTROL);
    // A tap on a row names its sensor.
    for (auto &x : s.hits) if (x.kind == HitKind::ROW) { const Hit *got = hit(s, x.x + 2, x.y + 2); CHECK(got && got->entity == x.entity); }
    // Asking for a second tap shows the words, on the button asked.
    Scene ask = build(d, m, w, h, 0, 2);
    bool found = false;
    for (auto &t : ask.texts) if (t.s == d.confirm) found = true;
    CHECK(found);
    // A big tile: the picture, with a part for each sensor.
    const int bw = int(b.dpi * 3.2), bh = int(b.dpi * 2.4);
    Scene pic = build(d, m, bw, bh);
    CHECK(pic.ok && pic.schematic);
    CHECK(inside(pic, bw, bh));
    int parts = 0;
    for (auto &x : pic.hits) if (x.kind == HitKind::PART && !x.entity.empty()) ++parts;
    CHECK(parts == 6);
    // No data: nothing drawn.
    Data none;
    CHECK(!build(none, m, w, h).ok);
    // Too small for anything.
    Scene tiny = build(d, m, 30, 20);
    CHECK(tiny.ok);
  }
  CHECK(number(NAN, 0, ".") == "–");
  CHECK(number(1.5f, 1, ",") == "1,5");
  CHECK(paint_of(0) == Paint::kMuted && paint_of(7) == Paint::kImmersion && paint_of(99) == Paint::kMuted);
  std::printf("plant card: %d checks passed\n", checks);
  return 0;
}
