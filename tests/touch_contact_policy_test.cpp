#include "touch_contact_policy.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

using exlink::TouchContactPolicy;

static void point(TouchContactPolicy &p, uint32_t sample, uint32_t now,
                  uint16_t x = 200, uint16_t y = 220) {
  p.update(true, false, false, x, y, sample, now);
}

static void release(TouchContactPolicy &p, uint32_t sample, uint32_t now) {
  p.update(false, true, false, 0, 0, sample, now);
}

static void short_pair() {
  TouchContactPolicy p;
  // A quick down/up queued between two LVGL polls must retain both edges.
  point(p, 100, 140, 111, 222);
  assert(p.down() && p.x() == 111 && p.y() == 222 && p.sampledMs() == 100);
  assert(!p.takeCancellation());
  release(p, 120, 140);
  assert(!p.down() && !p.blocked() && !p.takeCancellation());
  point(p, 150, 150);
  assert(p.down());
}

static void stale_contact() {
  TouchContactPolicy p;
  point(p, 100, 100);
  p.expire(174);
  assert(p.down() && !p.takeCancellation());
  p.expire(175);
  assert(!p.down() && p.blocked() && p.takeCancellation());
  assert(!p.takeCancellation()); // Cancellation delivered once.
  // Bus recovery while the same finger is held cannot click or press again.
  point(p, 180, 180);
  assert(!p.down() && p.blocked());
  p.update(false, false, false, 0, 0, 190, 190);
  assert(p.blocked()); // An empty mailbox is not proof of finger-up.
  release(p, 120, 200); // Out-of-order release cannot rearm.
  assert(p.blocked());
  release(p, 200, 280); // Stale but in-order release cannot rearm either.
  assert(p.blocked());
  release(p, 290, 290);
  assert(!p.blocked() && !p.down());
  point(p, 300, 300);
  assert(p.down());
}

static void error_and_palm() {
  TouchContactPolicy p;
  point(p, 100, 100);
  p.update(false, false, true, 0, 0, 110, 110);
  assert(!p.down() && p.blocked() && p.takeCancellation());
  point(p, 120, 120);
  assert(!p.down());
  release(p, 130, 130);
  point(p, 140, 140);
  assert(p.down());
  // Neutral/invalid frames neither release nor keep the contact alive.
  p.update(false, false, false, 0, 0, 170, 170);
  assert(p.down() && p.sampledMs() == 140);
  p.update(false, false, false, 0, 0, 215, 215);
  assert(!p.down() && p.blocked() && p.takeCancellation());
}

static void delayed_reports_and_wrap() {
  TouchContactPolicy p;
  point(p, 100, 175); // Too old even if this is the first received report.
  assert(!p.down() && p.blocked() && p.takeCancellation());
  point(p, 180, 180);
  assert(!p.down());
  release(p, 190, 190);
  point(p, 210, 210, 100, 200);
  point(p, 200, 211, 350, 400); // Older sample may not move the point.
  assert(p.x() == 100 && p.y() == 200 && p.sampledMs() == 210);
  release(p, 210, 212); // Same tick down/up is legitimate.
  assert(!p.down() && !p.blocked());

  p.reset();
  const uint32_t start = UINT32_MAX - 40U;
  point(p, start, start);
  p.expire(start + 74U);
  assert(p.down());
  p.expire(start + 75U);
  assert(!p.down() && p.blocked() && p.takeCancellation());
  release(p, start + 80U, start + 80U);
  point(p, start + 90U, start + 90U);
  assert(p.down() && !p.blocked());
}

static void explicit_block() {
  TouchContactPolicy p;
  p.blockUntilRelease();
  assert(p.blocked() && p.takeCancellation());
  point(p, 20, 20);
  assert(!p.down());
  release(p, 30, 30);
  point(p, 40, 40);
  assert(p.down());
  p.reset();
  assert(!p.down() && !p.blocked() && !p.takeCancellation());
}

static void observation_clock_precedes_report() {
  TouchContactPolicy p;
  point(p, 101, 102);
  p.expire(100); // A queued worker report was newer than the caller's clock.
  assert(p.down() && !p.blocked() && !p.takeCancellation());
  p.expire(175);
  assert(p.down());
  p.expire(176);
  assert(!p.down() && p.blocked() && p.takeCancellation());
}

int main() {
  short_pair();
  stale_contact();
  error_and_palm();
  delayed_reports_and_wrap();
  explicit_block();
  observation_clock_precedes_report();
  puts("Touch contact policy passed: quick taps, stale/error cancellation, held-finger quarantine, explicit release, queue age/order, millis wrap");
}
