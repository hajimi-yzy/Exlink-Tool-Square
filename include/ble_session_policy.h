#pragma once
#include <stdint.h>

enum class BleSessionMode : uint8_t { Off, Phone, Bridge };

// loop owns the policy; BTC callbacks deliver events, never mutate it.
class BleSessionPolicy {
  BleSessionMode mode_ = BleSessionMode::Off;
  bool connected_ = false, busy_ = false;
  uint32_t advertisingAt_ = 0, activityAt_ = 0;
public:
  void start(BleSessionMode mode, uint32_t now) {
    mode_ = mode;
    connected_ = busy_ = false;
    advertisingAt_ = activityAt_ = now;
  }
  void stop() { mode_ = BleSessionMode::Off; connected_ = busy_ = false; }
  void connected(bool value, uint32_t now) {
    if (mode_ == BleSessionMode::Off) return;
    connected_ = value;
    advertisingAt_ = activityAt_ = now;
  }
  void activity(uint32_t now) {
    if (mode_ != BleSessionMode::Off && connected_) activityAt_ = now;
  }
  bool expired(uint32_t now, bool busy, uint32_t advertisingMs, uint32_t idleMs) {
    if (mode_ == BleSessionMode::Off) return false;
    if (busy || busy_) {
      // A transfer and its final response must not be cut off by an expired
      // timer. Give the restored UI/link a fresh idle interval afterwards.
      advertisingAt_ = activityAt_ = now;
      busy_ = busy;
      return false;
    }
    return connected_ ? uint32_t(now - activityAt_) >= idleMs :
                        uint32_t(now - advertisingAt_) >= advertisingMs;
  }
};
