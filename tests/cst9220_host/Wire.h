#pragma once
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <deque>
#include <vector>

class TwoWire {
 public:
  enum Firmware { Mailbox, Legacy, Unknown } firmware = Mailbox;
  bool online = true;
  uint8_t activeAddress = 0x5A;
  unsigned beginCalls = 0;
  unsigned ackWrites = 0;
  unsigned ackFailures = 0;
  unsigned requestCalls = 0;
  unsigned lastWriteStop = 0;
  unsigned lastReadStop = 0;
  bool failRead = false;
  bool shortBuffer = false;
  size_t queueCapacity = 32;
  std::vector<uint8_t> probes;
  std::vector<uint8_t> packet = std::vector<uint8_t>(15, 0);

  bool begin(int, int, uint32_t) { ++beginCalls; return true; }
  void beginTransmission(uint8_t addr) {
    assert(!pending_);
    pending_ = true;
    repeated_ = false;
    address_ = addr;
    tx_.clear();
  }
  size_t write(uint8_t value) {
    assert(pending_);
    if (tx_.size() >= queueCapacity) return 0;
    tx_.push_back(value);
    return 1;
  }
  uint8_t endTransmission(bool stop = true) {
    assert(pending_);
    lastWriteStop = stop;
    if (!stop) { repeated_ = true; return 0; }
    pending_ = false;
    if (tx_.empty()) probes.push_back(address_);
    if (!online || address_ != activeAddress) return 2;
    if (tx_.size() == 3 && tx_[0] == 0xD0 && tx_[1] == 0 && tx_[2] == 0xAB) {
      ++ackWrites;
      if (ackFailures) { --ackFailures; return 2; }
    }
    if (tx_.size() >= 2 && tx_[0] == 0xD1 && firmware != Mailbox) return 2;
    return 0;
  }
  size_t requestFrom(int addr, int count, int stop) {
    ++requestCalls;
    lastReadStop = stop;
    assert(stop);
    if (repeated_) {
      assert(pending_ && addr == address_);
      pending_ = false;
      repeated_ = false;
    } else assert(!pending_);
    rx_.clear();
    if (!online || addr != activeAddress || failRead) return 0;
    std::vector<uint8_t> value;
    const unsigned reg = tx_.size() == 2 ? unsigned(tx_[0]) * 256 + tx_[1]
                                          : (tx_.empty() ? 0 : tx_[0]);
    if (firmware == Mailbox) {
      if (reg == 0xD1FC) value = {0, 0, 0xCA, 0xCA};
      if (reg == 0xD1F8) value = {0xE0, 1, 0xE0, 1};
      if (reg == 0xD204) value = {0, 0, 0x20, 0x92};
      if (reg == 0xD000) value = packet;
    } else if (firmware == Legacy) {
      if (reg == 0xA9) value = {0x8D};
      if (reg == 0) value = std::vector<uint8_t>(packet.begin(), packet.begin() + 7);
    }
    if (value.size() != size_t(count)) return 0;
    for (size_t i = 0; i < value.size() - (shortBuffer ? 1 : 0); ++i)
      rx_.push_back(value[i]);
    return value.size();
  }
  int available() { return int(rx_.size()); }
  int read() {
    if (rx_.empty()) return -1;
    const int value = rx_.front();
    rx_.pop_front();
    return value;
  }
 private:
  bool pending_ = false;
  bool repeated_ = false;
  uint8_t address_ = 0;
  std::vector<uint8_t> tx_;
  std::deque<int> rx_;
};
