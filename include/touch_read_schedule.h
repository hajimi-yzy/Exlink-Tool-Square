#pragma once

#include <stdint.h>

namespace exlink {

// The caller clears irq_pending immediately before each read. Only a new IRQ
// may bypass error backoff, at most once per 32 ms; a stuck LOW cannot do so.
inline bool touchReadDue(uint32_t now, uint32_t last_read_ms,
                         uint32_t retry_at_ms, bool enabled, bool irq_pending,
                         bool irq_low, bool contact_active) {
  const uint32_t elapsed = now - last_read_ms;
  if (!enabled || elapsed < 16U) return false;
  if (irq_pending && elapsed >= 32U) return true;
  return int32_t(now - retry_at_ms) >= 0 &&
      (irq_pending || irq_low || elapsed >= (contact_active ? 32U : 1000U));
}

} // namespace exlink
