#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
struct TwoWire {
  bool online = true, nack = false, short_read = false, empty_read = false;
  bool fail_id = false, second_device = false;
  uint16_t reset_config = 0x4127;
  uint8_t address = 0, reg = 0;
  size_t rx_index = 0;
  std::vector<uint8_t> tx, rx;
  uint16_t registers[256] = {};
  unsigned requests = 0;
  TwoWire() { registers[0] = 0x4127; registers[0xFF] = 0x2260; registers[2] = 4000; }
  void begin() {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t value) { address = value; tx.clear(); }
  size_t write(uint8_t value) { tx.push_back(value); return 1; }
  uint8_t endTransmission(bool = true) {
    if (!online || nack || (address != 0x40 && !(second_device && address == 0x41))) return 2;
    if (!tx.empty()) reg = tx[0];
    if (tx.size() == 3) {
      uint16_t value = uint16_t(tx[1]) << 8 | tx[2];
      registers[reg] = reg == 0 && value == 0x8000 ? reset_config : value;
    }
    return 0;
  }
  size_t requestFrom(uint8_t, uint8_t) {
    ++requests; rx.clear(); rx_index = 0;
    if (!online || short_read || (fail_id && reg == 0xFF)) return 0;
    rx.push_back(uint8_t(registers[reg] >> 8));
    rx.push_back(uint8_t(registers[reg]));
    return 2;
  }
  int read() { return empty_read || rx_index >= rx.size() ? -1 : rx[rx_index++]; }
};
extern TwoWire Wire;
