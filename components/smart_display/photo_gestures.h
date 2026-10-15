#pragma once
#include <algorithm>
#include <cstdlib>
#include "screen_input.h"

// Recognition only. The caller receives the controller's filtered reports, applies
// rotation once, and keeps the existing TouchGuard as the authority for actions.
namespace photo_gestures {
enum class Action { none, next, home };
enum class Direction { none, left, right, up, down };
struct Result {
  Action action = Action::none;
  Direction direction = Direction::none;
};
inline const char *direction_name(Direction value) {
  switch (value) {
    case Direction::left: return "left";
    case Direction::right: return "right";
    case Direction::up: return "up";
    case Direction::down: return "down";
    default: return "none";
  }
}
class Contact {
 public:
  void begin(bool active, int x, int y, int contact, int swipe_distance, int tap_drift) {
    active_ = active;
    x_ = last_x_ = x; y_ = last_y_ = y; contact_ = contact;
    swipe_distance_ = std::max(1, swipe_distance);
    tap_drift_ = std::max(0, tap_drift);
    farthest_ = 0;
    direction_ = Direction::none;
  }
  bool active() const { return active_; }
  void update(int x, int y, int contact) {
    if (!active_ || contact != contact_ || (x == 0 && y == 0)) return;
    last_x_ = x; last_y_ = y;
    const int dx = x - x_, dy = y - y_;
    farthest_ = std::max(farthest_, std::max(std::abs(dx), std::abs(dy)));
    // Remember a completed swipe even when the finger returns to its start.
    if (direction_ == Direction::none && farthest_ >= swipe_distance_)
      direction_ = std::abs(dx) >= std::abs(dy)
          ? (dx < 0 ? Direction::left : Direction::right)
          : (dy < 0 ? Direction::up : Direction::down);
  }
  Result finish(uint32_t now, screen_input::TouchGuard &guard) {
    if (!active_) return {};
    active_ = false;
    if (direction_ != Direction::none) {
      // Slider acceptance applies duration and one-event guards, without rejecting
      // the movement which is the purpose of a swipe. No real tile is addressed.
      if (!guard.accept_slider(now, GUARD_KEY)) return {};
      guard.consume();
      return {direction_ == Direction::up ? Action::home : Action::none, direction_};
    }
    if (farthest_ > tap_drift_ || !guard.accept_repeat(now, GUARD_KEY)) {
      guard.consume();
      return {};
    }
    guard.consume();
    return {Action::next, Direction::none};
  }
  void cancel() { active_ = false; }
 private:
  static constexpr int GUARD_KEY = -110;
  bool active_ = false;
  int x_ = 0, y_ = 0, last_x_ = 0, last_y_ = 0, contact_ = 0;
  int swipe_distance_ = 1, tap_drift_ = 0, farthest_ = 0;
  Direction direction_ = Direction::none;
};
}  // namespace photo_gestures
