// plant_scene.h on a PC: the schematic builds from the add-on's data and every shape is where the card puts it.
#include <cstdio>
#include <cstdlib>
#include "../components/smart_display/plant_scene.h"
namespace ps = plant_scene;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static ps::Data sample(uint32_t flags) {
  ps::Data d;
  d.any = true;
  d.flags = flags;
  d.said.assign(ps::T_COUNT, ps::Said{"x", 1});
  d.entity.assign(ps::E_COUNT, "sensor.x");
  d.hue = 134;
  return d;
}
static uint32_t bit(ps::Flag f) { return 1u << f; }

int main() {
  static_assert(ps::T_CIRCUIT == 21 && ps::T_MANAGER == 22 && ps::T_IMM_STATE == 23 && ps::T_SALT == 32,
                "existing Plant Room wire slots must not move");
  static_assert(ps::T_CYL_SUB == 33 && ps::T_SALT_DAYS == 34 && ps::T_COUNT == 35,
                "new Plant Room text belongs at the end of the wire payload");
  static_assert(ps::E_SALT == 20 && ps::E_SALT_2 == 21 && ps::E_SALT_REPLACED == 22,
                "salt entities append without moving existing wire positions");
  // Idle: the fixed furniture only.
  {
    ps::Scene s = ps::build(sample(0));
    CHECK(s.ok);
    CHECK(!s.order.empty());
    CHECK(s.order.size() == s.rects.size() + s.polys.size() + s.blobs.size() + s.dots.size() + s.txts.size());
    for (auto &p : s.polys) for (auto &q : p.p) CHECK(q.x >= -1 && q.x <= 1001 && q.y >= -1 && q.y <= 621);
    for (auto &r : s.rects) CHECK(r.x >= -1 && r.x + r.w <= 1001 && r.y >= -1 && r.y + r.h <= 621);
    for (auto &t : s.txts) { CHECK(t.x >= 0 && t.x <= 1000 && t.y >= 0 && t.y <= 620); CHECK(!t.runs.empty()); }
  }
  // Everything lit adds shapes and never removes the furniture.
  {
    const uint32_t all = bit(ps::F_HEAT_CALL) | bit(ps::F_HW_CALL) | bit(ps::F_HEAT_PATH) | bit(ps::F_DHW_PATH) | bit(ps::F_ANY_PRIMARY) |
                         bit(ps::F_ACTIVE) | bit(ps::F_DHW_FLOW) | bit(ps::F_MAINS) | bit(ps::F_TANK_COLD) | bit(ps::F_HOUSE_COLD) |
                         bit(ps::F_WATER) | bit(ps::F_HOT_DRAW) | bit(ps::F_FIRING) | bit(ps::F_IMMERSION) | bit(ps::F_CYL);
    ps::Scene idle = ps::build(sample(0)), lit = ps::build(sample(all));
    CHECK(lit.order.size() > idle.order.size());
    CHECK(lit.polys.size() > idle.polys.size());
    CHECK(lit.blobs.size() > idle.blobs.size());  // the flame, the arrow heads
    // The state of the zone valves moves the valve glyphs only.
    CHECK(lit.hits.size() == idle.hits.size());
  }
  // Taps: the four thermostat buttons and the readings.
  {
    ps::Scene s = ps::build(sample(0));
    const ps::Hit *off = ps::hit(s, 870, 123), *heat = ps::hit(s, 935, 123), *none = ps::hit(s, 870, 159), *eco = ps::hit(s, 935, 159);
    CHECK(off && off->kind == 1 && off->index == 0);
    CHECK(heat && heat->kind == 1 && heat->index == 1);
    CHECK(none && none->kind == 1 && none->index == 2);
    CHECK(eco && eco->kind == 1 && eco->index == 3);
    const ps::Hit *pressure = ps::hit(s, 100, 300);
    CHECK(pressure && pressure->kind == 0 && pressure->index == ps::E_PRESSURE);
    const ps::Hit *cyl = ps::hit(s, 487, 230);
    CHECK(cyl && cyl->index == ps::E_CYL);
    CHECK(!ps::hit(s, 5, 5));
    const ps::Hit *nest = ps::hit(s, 860, 270);
    CHECK(nest && nest->index == ps::E_THERMOSTAT);
  }
  // A button that waits for its second tap says so.
  {
    ps::Scene asking = ps::build(sample(0), 1);
    bool sure = false;
    for (auto &t : asking.txts) for (auto &r : t.runs) if (r.s == "SURE?") sure = true;
    CHECK(sure);
  }
  // The Kinetico section has three left-aligned lines and a guarded replacement hit.
  {
    ps::Data d = sample(bit(ps::F_SALT_1));
    d.said[ps::T_SALT_DAYS] = {"About 22 days remaining", 0};
    d.said[ps::T_SALT] = {"Salt low · top up", 2};
    ps::Scene s = ps::build(d);
    const ps::Hit *salt = ps::hit(s, 380, 580);
    CHECK(salt && salt->kind == 2 && salt->index == ps::E_SALT_REPLACED);
    bool title = false, days = false, status = false;
    for (auto &t : s.txts) for (auto &r : t.runs) {
      if (r.s == "Kinetico 2020c") title = t.x == 338 && t.anchor == 0;
      if (r.s == "About 22 days remaining") days = t.x == 338 && t.anchor == 0;
      if (r.s == "Salt low · top up") status = t.x == 338 && t.anchor == 0;
    }
    CHECK(title && days && status);
    ps::Scene asking = ps::build(d, 4);
    bool confirm = false;
    for (auto &t : asking.txts) for (auto &r : t.runs) if (r.s == "Tap again to mark replaced") confirm = true;
    CHECK(confirm);
  }
  // The coil's gradient: solid when the flags say so, banded otherwise.
  {
    ps::Data a = sample(bit(ps::F_CYL));
    a.mix_y10 = 2840; a.coil_hot = 100; a.coil_cold = 220;
    ps::Scene banded = ps::build(a);
    a.flags |= bit(ps::F_COIL_FLOW);
    ps::Scene solid = ps::build(a);
    CHECK(banded.polys.size() > solid.polys.size());
  }
  // The v2.5.22 tank rows keep labels left, values right, and make BULK prominent.
  {
    ps::Data d = sample(bit(ps::F_CYL));
    d.said[ps::T_CYL_SUB] = {"BULK|38.1 °C", 1};
    d.said[ps::T_CIRCUIT] = {"Auto", 1};
    d.said[ps::T_MANAGER] = {"Running", 1};
    ps::Scene s = ps::build(d);
    bool bulk_label = false, bulk_value = false, circuit_label = false, circuit_value = false;
    for (auto &t : s.txts) for (auto &r : t.runs) {
      if (r.s == "BULK") bulk_label = t.x == 438 && t.anchor == 0;
      if (r.s == "38.1 °C") bulk_value = t.x == 536 && t.anchor == 2 && r.weight >= 7;
      if (r.s == "CIRCUIT") circuit_label = t.x == 438 && t.anchor == 0;
      if (r.s == "Auto") circuit_value = t.x == 536 && t.anchor == 2;
    }
    CHECK(bulk_label && bulk_value && circuit_label && circuit_value);
  }
  // The path reader: arcs end where they say, semicircles bulge the right way.
  {
    auto p = ps::path("M523,194 A29,29 0 0 1 523,252");
    CHECK(p.size() == 1);
    CHECK(std::fabs(p[0].p.back().x - 523) < 0.01f && std::fabs(p[0].p.back().y - 252) < 0.01f);
    float maxx = 0;
    for (auto &q : p[0].p) maxx = std::max(maxx, q.x);
    CHECK(std::fabs(maxx - 552) < 0.5f);   // clockwise from the top to the bottom swings out to the right
    auto left = ps::path("M433,252 A11,11 0 0 0 433,274");
    float minx = 1e9f;
    for (auto &q : left[0].p) minx = std::min(minx, q.x);
    CHECK(std::fabs(minx - 422) < 0.5f);
    auto q = ps::path("M230,104 H288 Q300,104 300,92 V52 H790");
    CHECK(std::fabs(q[0].p.back().x - 790) < 0.01f && std::fabs(q[0].p.back().y - 52) < 0.01f);
    auto z = ps::path("M0 -28C12 -15 17 -6 17 5a17 17 0 0 1-34 0C-17 -5 -7 -12 0 -28z");
    CHECK(z[0].closed && std::fabs(z[0].p.back().x) < 0.1f && std::fabs(z[0].p.back().y + 28) < 0.1f);
  }
  std::printf(failures ? "%d failures\n" : "plant_scene: all checks pass\n", failures);
  return failures ? 1 : 0;
}
