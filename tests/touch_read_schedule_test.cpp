#include "touch_read_schedule.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

using exlink::touchReadDue;

static void normal_reads() {
  assert(!touchReadDue(115, 100, 100, true, true, true, true));
  assert(touchReadDue(116, 100, 100, true, true, false, true));
  assert(touchReadDue(116, 100, 100, true, false, true, true));
  assert(!touchReadDue(131, 100, 100, true, false, false, true));
  assert(touchReadDue(132, 100, 100, true, false, false, true));
  assert(!touchReadDue(1099, 100, 100, true, false, false, false));
  assert(touchReadDue(1100, 100, 100, true, false, false, false));
  assert(!touchReadDue(2000, 100, 100, false, true, true, true));
}

static void short_tap_during_idle_backoff() {
  // An idle read failed at t=0. A t=20..60 tap must be read before t=1600.
  uint32_t last_read = 0, read_at[2] = {};
  unsigned reads = 0;
  bool pending = false, active = false;
  for (uint32_t now = 1; now <= 100; ++now) {
    if (now == 20 || now == 60) pending = true;
    if (!touchReadDue(now, last_read, 1600, true, pending, false, active)) continue;
    assert(reads < 2);
    pending = false; // Match service's consumption of one notification.
    last_read = now;
    read_at[reads++] = now;
    active = now < 60;
  }
  assert(reads == 2 && read_at[0] == 32 && read_at[1] == 64 && !active);
}

static void storm_and_stuck_low() {
  uint32_t last_read = 0;
  unsigned reads = 0;
  for (uint32_t now = 1; now <= 320; ++now) {
    // A new edge arrives every millisecond while errors keep backoff active.
    if (!touchReadDue(now, last_read, 10000, true, true, true, false)) continue;
    assert(now - last_read == 32);
    last_read = now;
    ++reads;
  }
  assert(reads == 10);
  // The consumed notification cannot become repeated retries via irq_low.
  assert(!touchReadDue(352, last_read, 10000, true, false, true, false));
  assert(!touchReadDue(1000, 0, 1600, true, false, true, false));
  assert(!touchReadDue(1599, 0, 1600, true, false, true, false));
  assert(touchReadDue(1600, 0, 1600, true, false, true, false));
  assert(!touchReadDue(1632, 1600, 3200, true, false, true, true));
}

static void millis_wrap() {
  const uint32_t last = UINT32_MAX - 15U;
  assert(!touchReadDue(UINT32_MAX, last, last, true, true, false, true));
  assert(touchReadDue(0, last, last, true, true, false, true));
  assert(!touchReadDue(15, last, 100, true, true, true, true));
  assert(touchReadDue(16, last, 100, true, true, false, true));
  assert(!touchReadDue(16, last, 100, true, false, true, true));
  assert(touchReadDue(100, last, 100, true, false, true, true));
  assert(!touchReadDue(983, last, last, true, false, false, false));
  assert(touchReadDue(984, last, last, true, false, false, false));
}

int main() {
  normal_reads();
  short_tap_during_idle_backoff();
  storm_and_stuck_low();
  millis_wrap();
  puts("Touch schedule passed: 16ms minimum, active/idle fallback, backoff short tap, 32ms IRQ storm limit, stuck LOW, millis wrap");
}
