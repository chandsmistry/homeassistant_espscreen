#pragma once
#include <cstdint>
#include <string>
namespace photo_preload {
// One immutable reservation, plus the displayed frame. Busy taps join one intent.
struct State {
  bool enabled = true, waiting = false, ready = false, wanted = false;
  uint32_t session = 0, generation = 0, view = 0, shown_at = 0, tapped_at = 0;
  std::string frame, displayed, url;
  void reset() {
    if (++session == 0) ++session;
    generation = 0; view = shown_at = tapped_at = 0;
    frame.clear(); displayed.clear(); url.clear();
    enabled = true; waiting = ready = wanted = false;
  }
  std::string key() const { return "photo-next|" + std::to_string(session) + "|" + std::to_string(generation); }
  void begin() { waiting = true; ready = false; frame.clear(); url.clear(); ++generation; }
  bool answer(uint32_t number, const std::string &id, const std::string &link) {
    if (!waiting || number != view || id.empty() || link.empty()) return false;
    frame = id; url = link; return true;
  }
  bool tap(uint32_t now) { if (wanted) return false; wanted = true; tapped_at = now; return true; }
  bool due(uint32_t now, uint32_t interval) const { return displayed.empty() || wanted || now - shown_at >= interval; }
  void promoted(uint32_t now) { displayed = frame; shown_at = now; waiting = ready = wanted = false; }
};
} // namespace photo_preload
