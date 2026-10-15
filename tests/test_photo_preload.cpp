#include "../components/smart_display/photo_preload.h"
#include <cassert>
int main() {
  photo_preload::State s; s.reset(); s.begin(); s.view=10;
  assert(!s.answer(9,"stale","url"));
  assert(!s.answer(10,"","url"));
  assert(s.answer(10,"first","url1")); s.ready=true;
  assert(s.due(100,5000)); s.promoted(100);
  assert(s.displayed=="first" && !s.due(5099,5000));
  auto first=s.key(); s.begin(); s.view=11; assert(s.key()!=first);
  assert(s.tap(200)); assert(!s.tap(300)); assert(s.tapped_at==200);
  assert(!s.answer(10,"old","old")); assert(s.answer(11,"next","url2"));
  s.ready=true; assert(s.due(400,5000)); s.promoted(400);
  assert(s.displayed=="next" && !s.wanted && s.shown_at==400);
  s.begin(); auto session=s.session; s.reset(); assert(s.session!=session && s.displayed.empty() && !s.ready);
  s.begin(); s.view=42; assert(s.answer(42,"retry","immutable"));
  auto key=s.key(); assert(s.tap(100)); assert(!s.tap(101)); assert(s.key()==key && s.url=="immutable");
  s.reset();
  s.session=UINT32_MAX; s.reset(); assert(s.session==1);
}
