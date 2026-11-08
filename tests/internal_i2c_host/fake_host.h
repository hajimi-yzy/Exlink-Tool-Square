#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace host {
struct StopWorker {};
struct BusEvent {
  uint32_t ms;
  uint8_t address;
  bool read;
  std::vector<uint8_t> bytes;
  size_t count;
};
struct Runtime {
  uint32_t now = 0, deadline = 0, notifications = 0;
  void (*entry)(void *) = nullptr;
  void *context = nullptr;
  std::function<void()> tick_hook;
  std::function<void(const BusEvent &)> bus_hook;
};
extern Runtime runtime;
inline void advance(uint32_t ms) { runtime.now += ms; }
}
