#pragma once
#include "lvgl.h"
namespace runtime_tiles { struct Widgets; struct Tile; }
namespace pd_view {
// Hit regions reuse the accepted draw geometry. Gaps and compact summaries are not actions.
int action_key(const runtime_tiles::Widgets &w,const lv_point_t &point);
void render(runtime_tiles::Widgets &w, const runtime_tiles::Tile &t, int width, int height);
}
