#include "../components/smart_display/photo_next.h"
#include <cassert>
int main() {
  photo_gestures::ManualNext n;
  assert(n.tap(100)); assert(n.pending && n.serial == 1 && n.tapped_at == 100);
  for (unsigned t = 200; t < 1000; t += 100) assert(!n.tap(t));
  assert(n.serial == 1 && n.tapped_at == 100);
  n.cancel(); assert(!n.pending && n.serial == 1);
  assert(n.tap(2000) && n.serial == 2); n.cancel();
  assert(n.tap(4000)); assert(!n.tap(4100));
  assert(!n.expire(63999)); assert(n.expire(64000));
  assert(!n.pending && n.tap(65000));
  n.cancel(); n.serial = UINT32_MAX; assert(n.tap(70000)); assert(n.serial == 1);
}
