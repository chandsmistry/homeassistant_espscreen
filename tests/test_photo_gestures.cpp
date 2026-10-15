#include "screen_text_en.h"
#include "../components/smart_display/photo_gestures.h"
#include <cassert>
int main() {
  using namespace photo_gestures;
  screen_input::TouchGuard guard;
  guard.configure(0, 20);  // capacitive board
  Contact contact;
  auto begin = [&](uint32_t now, bool active = true) {
    guard.begin(now, 200, 200, 7);
    contact.begin(active, 200, 200, 7, 40, 14);
  };
  begin(1000);
  assert(contact.finish(1050, guard).action == Action::next);
  assert(!guard.accept(1051, 0));  // cannot also wake or run an underlying tile
  assert(contact.finish(1052, guard).action == Action::none);
  begin(1200, false);
  assert(contact.finish(1250, guard).action == Action::none);
  // Four directions and no edge requirement.
  int xs[] = {100, 300, 200, 200}, ys[] = {200, 200, 100, 300};
  Direction dirs[] = {Direction::left, Direction::right, Direction::up, Direction::down};
  for (int n = 0; n < 4; ++n) {
    begin(2000 + n * 1000);
    guard.update(xs[n], ys[n], 7); contact.update(xs[n], ys[n], 7);
    auto result = contact.finish(2100 + n * 1000, guard);
    assert(result.action == (dirs[n] == Direction::up ? Action::home : Action::none) && result.direction == dirs[n]);
    assert(!guard.accept(2150 + n * 1000, 0));
  }
  begin(7000); contact.update(200, 100, 8);  // other finger is ignored
  assert(contact.finish(7050, guard).action == Action::next);
  begin(8000); contact.update(0, 0, 7);  // ghost does not create a swipe
  assert(contact.finish(8050, guard).action == Action::next);
  begin(9000); contact.update(230, 200, 7);  // neither swipe nor clean tap
  assert(contact.finish(9050, guard).action == Action::none);
  begin(10000);
  assert(contact.finish(10001, guard).action == Action::none);  // duration guard
  begin(11000); contact.update(200, 100, 7); contact.update(200, 200, 7);
  assert(contact.finish(11100, guard).action == Action::home);
  begin(12000); contact.cancel();
  assert(contact.finish(12050, guard).action == Action::none);
  // A consumed event must not be reinterpreted as either gesture.
  begin(13000); guard.consume();
  assert(contact.finish(13050, guard).action == Action::none);
  // Resistive movement rejection still applies to taps; swipes use guarded release.
  guard.configure(18, 60);
  begin(14000); guard.update(240, 200, 7); contact.update(240, 200, 7);
  assert(contact.finish(14080, guard).action == Action::none);
}
