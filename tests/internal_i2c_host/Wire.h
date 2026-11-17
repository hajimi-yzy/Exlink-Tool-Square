#pragma once
#include "fake_host.h"
#include <array>

class TwoWire {
 public:
  uint16_t timeout = 0;
  bool fail_mpu = false, fail_aht = false, corrupt_aht_crc = false;
  bool fail_ina = false;
  uint8_t wiper = 50;
  std::array<uint8_t, 256> mpu{};
  std::vector<host::BusEvent> events;
  TwoWire() { mpu[0x75] = 0x68; }
  void setTimeOut(uint16_t value) { timeout = value; }
  void beginTransmission(uint8_t address) { address_ = address; tx_.clear(); }
  size_t write(uint8_t value) { tx_.push_back(value); return 1; }
  uint8_t endTransmission(bool = true);
  size_t requestFrom(uint8_t address, size_t count, bool = true);
  int read() {
    if (rx_.empty()) return -1;
    int value = rx_.front(); rx_.pop_front(); return value;
  }
  int available() const { return static_cast<int>(rx_.size()); }
 private:
  uint8_t address_ = 0, reg_ = 0;
  std::vector<uint8_t> tx_;
  std::deque<uint8_t> rx_;
};
