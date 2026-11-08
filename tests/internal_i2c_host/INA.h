#pragma once
#include "fake_host.h"
#include <utility>

#define INA_MODE_POWER_DOWN 0
#define INA_MODE_CONTINUOUS_BOTH 7
class INA_Class {
 public:
  unsigned voltage_reads = 0, current_reads = 0, power_reads = 0;
  std::vector<std::pair<uint32_t, int>> modes;
  uint64_t getBusMilliVolts(uint8_t) { ++voltage_reads; return 5001; }
  int64_t getBusMicroAmps(uint8_t) { ++current_reads; return 123000; }
  int64_t getBusMicroWatts(uint8_t) { ++power_reads; return 615123; }
  void setMode(int mode, uint8_t) { modes.emplace_back(host::runtime.now, mode); }
};
