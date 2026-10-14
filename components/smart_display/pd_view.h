#pragma once
namespace runtime_tiles { struct Widgets; struct Tile; }
namespace pd_view {
void render(runtime_tiles::Widgets &w, const runtime_tiles::Tile &t, int width, int height);
}
