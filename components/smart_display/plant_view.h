#pragma once
// The plant room card on the glass (firmware 0.55.0): plant_card.h places everything, plant_view.cpp paints it. One LVGL
// object per card draws the whole scene in its draw event, as the energy card does; the card has no timer of its own.
#include "lvgl.h"

namespace runtime_tiles {
struct Widgets;
struct Tile;
}

namespace plant_view {
// Draws the card into the slot's extra layer, `width` x `height` (the card's content).
void render(runtime_tiles::Widgets &w, const runtime_tiles::Tile &t, int width, int height);
// A tap at `point` (screen coordinates): a tab opens, a reading opens its history, a thermostat button asks for a second
// tap and then calls its service. True when the tap did something.
bool tap(runtime_tiles::Widgets &w, const runtime_tiles::Tile &t, const lv_point_t &point);
}  // namespace plant_view
