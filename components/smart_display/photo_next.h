#pragma once
#include <cstdint>

namespace photo_gestures {
// Busy retaps join the same request. They never restart it or queue another advance.
struct ManualNext {
  bool pending = false;
  uint32_t serial = 0, tapped_at = 0;
  bool tap(uint32_t now) {
    if (pending) return false;
    pending = true; tapped_at = now;
    if (++serial == 0) ++serial;
    return true;
  }
  static constexpr uint32_t MAX_PENDING_MS = 60000;
  bool expire(uint32_t now) {
    if (!pending || now - tapped_at < MAX_PENDING_MS) return false;
    cancel(); return true;
  }
  void cancel() { pending = false; tapped_at = 0; }
};
}  // namespace photo_gestures
