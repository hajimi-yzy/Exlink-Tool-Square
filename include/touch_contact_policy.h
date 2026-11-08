#pragma once

#include <stdint.h>

namespace exlink {

// Cache-only input policy. A missing/bad sample cancels a contact; it is never
// a finger-up event that may click a widget. A cancelled finger must actually
// lift before a later point is allowed to start another contact.
class TouchContactPolicy {
 public:
  static constexpr uint32_t MaxAgeMs = 75;

  void reset() {
    down_ = blocked_ = cancel_pending_ = have_event_ = false;
    x_ = y_ = 0;
    sampled_ms_ = event_ms_ = 0;
  }

  void blockUntilRelease() {
    down_ = false;
    blocked_ = true;
    cancel_pending_ = true;
  }

  void expire(uint32_t now) {
    // A caller may have captured its clock just before a newer worker report.
    // That small negative age is not an expired contact; preserve millis wrap.
    if (down_ && int32_t(now - sampled_ms_) >= int32_t(MaxAgeMs))
      blockUntilRelease();
  }

  void update(bool down, bool released, bool cancelled, uint16_t x,
              uint16_t y, uint32_t sampled_ms, uint32_t now) {
    expire(now);
    // Timestamp differences, like millis(), are valid across uint32 wrap.
    // Old queued reports must not lift or replace a more recent contact.
    if (have_event_ && int32_t(sampled_ms - event_ms_) < 0) return;
    have_event_ = true;
    event_ms_ = sampled_ms;

    if (cancelled) {
      blockUntilRelease();
      return;
    }
    if (uint32_t(now - sampled_ms) >= MaxAgeMs) {
      // Even an old queued down with no current contact is unsafe: do not
      // turn the tail of that held finger into a new press when I2C recovers.
      if (down) blockUntilRelease();
      return;
    }
    if (released) {
      down_ = false;
      blocked_ = false;
      return;
    }
    if (down && !blocked_) {
      down_ = true;
      x_ = x;
      y_ = y;
      sampled_ms_ = sampled_ms;
    }
    // down=false/released=false is an empty/invalid report, not a release.
  }

  bool down() const { return down_; }
  bool blocked() const { return blocked_; }
  uint16_t x() const { return x_; }
  uint16_t y() const { return y_; }
  uint32_t sampledMs() const { return sampled_ms_; }

  bool takeCancellation() {
    const bool pending = cancel_pending_;
    cancel_pending_ = false;
    return pending;
  }

 private:
  bool down_ = false, blocked_ = false, cancel_pending_ = false;
  bool have_event_ = false;
  uint16_t x_ = 0, y_ = 0;
  uint32_t sampled_ms_ = 0, event_ms_ = 0;
};

} // namespace exlink
