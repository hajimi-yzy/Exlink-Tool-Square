#pragma once
#include "fake_host.h"

class CST9220 {
 public:
  bool communication_ok = true, down = true;
  unsigned recoveries = 0;
  std::vector<uint32_t> reads;
  int interruptPin() const { return 7; }
  bool isReady() const { return communication_ok; }
  bool readTouch(uint16_t *x, uint16_t *y) {
    reads.push_back(host::runtime.now);
    host::advance(1);
    *x = 80; *y = 120;
    return down && communication_ok;
  }
  bool communicationOk() const { return communication_ok; }
  bool releaseReported() const { return !down && communication_ok; }
  bool recover() { ++recoveries; return communication_ok; }
};
