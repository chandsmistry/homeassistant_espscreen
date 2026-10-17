// The plant room card on the glass (firmware 0.55.0): the scene plant_card.h builds, painted in one object's draw event.
//
// Compiled on its own, as energy_view.cpp is: every header of this component lands in main.cpp, and on Xtensa the
// literal pool of one object file must stay within reach of all of its code (page_receiver.cpp says how that broke).
#include "esphome/core/log.h"
#include "runtime_tiles.h"

namespace plant_view {
using runtime_tiles::Tile;
using runtime_tiles::Widgets;
namespace rt = runtime_tiles;
namespace pc = plant_card;

constexpr uint32_t ASK_MS = 4000;  // how long a thermostat button waits for its second tap

struct View {
  std::shared_ptr<const pc::Data> data;
  pc::Scene scene;
  int width = 0, height = 0;
  int tab = 0;
  int pending = -1;  // the button that asked for a second tap
  lv_timer_t *timer = nullptr;
};

static lv_color_t color(pc::Paint p) {
  switch (p) {
    case pc::Paint::OK: return theme::color(theme::PLANT_OK);
    case pc::Paint::HEAT: return theme::color(theme::PLANT_HEAT);
    case pc::Paint::HOT: return theme::color(theme::PLANT_HOT);
    case pc::Paint::COLD: return theme::color(theme::PLANT_COLD);
    case pc::Paint::WARN: return theme::color(theme::PLANT_WARN);
    case pc::Paint::BAD: return theme::color(theme::PLANT_BAD);
    case pc::Paint::IMMERSION: return theme::color(theme::PLANT_IMMERSION);
    case pc::Paint::GAS: return theme::color(theme::PLANT_GAS);
    case pc::Paint::LINE: return theme::color(theme::LINE);
    case pc::Paint::ACCENT: return theme::color(theme::ACCENT);
    case pc::Paint::INK: return theme::color(theme::INK);
    case pc::Paint::MUTED: default: return theme::color(theme::MUTED);
  }
}

static const lv_font_t *font(pc::Face f) { return f == pc::BODY ? rt::control_font : rt::small_font; }

static View *view_of(lv_obj_t *canvas) { return canvas ? static_cast<View *>(lv_obj_get_user_data(canvas)) : nullptr; }

static pc::Measure measure() {
  pc::Measure m;
  for (int f = 0; f < pc::FACES; ++f) m.line[f] = font(pc::Face(f)) ? lv_font_get_line_height(font(pc::Face(f))) : 0;
  m.width = [](pc::Face f, const std::string &s) { return font(f) ? rt::text_width(s, font(f)) : 1 << 20; };
  m.schematic_w = ui::mm(66);
  m.schematic_h = ui::mm(50);
  m.touch = ui::touch_min();
  m.decimal = screen_text::decimal_mark();
  return m;
}

static void rebuild(lv_obj_t *canvas) {
  View *view = view_of(canvas);
  if (!view || !view->data) return;
  view->scene = pc::build(*view->data, measure(), view->width, view->height, view->tab, view->pending);
  lv_obj_invalidate(canvas);
}

static bool meets(const lv_area_t &clip, int x1, int y1, int x2, int y2) {
  return x1 <= clip.x2 && x2 >= clip.x1 && y1 <= clip.y2 && y2 >= clip.y1;
}

static void draw(lv_event_t *e) {
  auto *canvas = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  View *view = view_of(canvas);
  if (!view || !view->scene.ok) return;
  auto *layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(canvas, &a);
  const lv_area_t clip = layer->_clip_area;
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
      if (auto *v = view_of(c)) { if (v->timer) lv_timer_delete(v->timer); delete v; }
    }, LV_EVENT_DELETE, nullptr);
  }
  View &view = *view_of(canvas);
  view.data = t.extra().plant;
  view.width = width; view.height = height;
  // A new moment may no longer have the button that asked.
  if (!view.data || view.pending >= int(view.data->controls.size())) stop_asking(view);
  lv_obj_set_pos(canvas, 0, 0);
  lv_obj_set_size(canvas, width, height);
  rebuild(canvas);
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

bool tap(Widgets &w, const Tile &t, const lv_point_t &point) {
  auto *canvas = w.parts[0];
  View *view = view_of(canvas);
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
