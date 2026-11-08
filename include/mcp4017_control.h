#pragma once

#include <stddef.h>
#include <stdint.h>

namespace exlink {

enum class Mcp4017Status {
  Ok, QueueFailed, TxFailed, ReadFailed, InvalidReadback, ReadbackMismatch
};

inline const char *mcp4017_status_name(Mcp4017Status status) {
  switch (status) {
    case Mcp4017Status::Ok: return "OK";
    case Mcp4017Status::QueueFailed: return "QUEUE_FAILED";
    case Mcp4017Status::TxFailed: return "TX_FAILED";
    case Mcp4017Status::ReadFailed: return "READ_FAILED";
    case Mcp4017Status::InvalidReadback: return "INVALID_READBACK";
    case Mcp4017Status::ReadbackMismatch: return "READBACK_MISMATCH";
  }
  return "UNKNOWN";
}

struct Mcp4017Result {
  Mcp4017Status status = Mcp4017Status::ReadFailed;
  int target = -1;
  int readback = -1;
  uint8_t tx_code = 255; // No write transaction attempted.
  size_t queued_bytes = 0;
  size_t received_bytes = 0;
};

// Microchip DS22147A, section 5.4: no register-address byte. Reads must
// request the wiper directly; writing a supposed pointer would change it.
template <class Bus>
class Mcp4017Controller {
 public:
  explicit Mcp4017Controller(Bus &bus) : bus_(bus) {}

  uint8_t probe() {
    bus_.beginTransmission(0x2F);
    return bus_.endTransmission();
  }

  Mcp4017Result read() {
    Mcp4017Result result;
    sample(result);
    known_ = result.status == Mcp4017Status::Ok;
    if (known_) value_ = result.readback;
    return result;
  }

  Mcp4017Result write(int target) {
    Mcp4017Result result;
    // Preserve the existing application's bounds, within the chip's 0..127.
    result.target = target < 1 ? 1 : (target > 126 ? 126 : target);
    // A write may reach the chip even if its acknowledgement/readback fails.
    // Never reuse the previous software value as a known hardware position.
    known_ = false;
    bus_.beginTransmission(0x2F);
    result.queued_bytes = bus_.write(static_cast<uint8_t>(result.target));
    // Always finish the transaction, including an enqueue failure, to release
    // Arduino Wire's transaction lock and leave the shared bus usable.
    result.tx_code = bus_.endTransmission();
    if (result.queued_bytes != 1) {
      result.status = Mcp4017Status::QueueFailed;
      return result;
    }
    if (result.tx_code != 0) {
      result.status = Mcp4017Status::TxFailed;
      return result;
    }
    sample(result);
    if (result.status != Mcp4017Status::Ok) return result;
    if (result.readback != result.target) {
      result.status = Mcp4017Status::ReadbackMismatch;
      return result;
    }
    value_ = result.readback;
    known_ = true;
    return result;
  }

  Mcp4017Result adjust(int delta) {
    // A peripheral reset can occur without an ESP32 reset. Base each manual
    // step on a fresh hardware read, not on an old requested value.
    Mcp4017Result current = read();
    if (current.status != Mcp4017Status::Ok) return current;
    // At a limit (including a genuine chip value of 0/127), do not clamp a
    // further outward step back in the opposite direction. It is a read-only
    // no-op, indicated by tx_code=255 and queued_bytes=0.
    if (delta == 0 || (delta < 0 && current.readback <= 1) ||
        (delta > 0 && current.readback >= 126)) {
      current.target = current.readback;
      return current;
    }
    return write(current.readback + delta);
  }

  bool known() const { return known_; }
  int value() const { return value_; }

 private:
  void sample(Mcp4017Result &result) {
    result.received_bytes = bus_.requestFrom(0x2F, 1);
    if (result.received_bytes != 1 || bus_.available() < 1) {
      result.status = Mcp4017Status::ReadFailed;
      return;
    }
    result.readback = bus_.read();
    if (result.readback < 0) {
      result.status = Mcp4017Status::ReadFailed;
    } else if (result.readback > 0x7F) {
      result.status = Mcp4017Status::InvalidReadback;
    } else {
      result.status = Mcp4017Status::Ok;
    }
  }

  Bus &bus_;
  bool known_ = false;
  int value_ = -1;
};

} // namespace exlink
