#pragma once
#include <cstring>
#include <cassert>
struct EEPROMClass {
  unsigned begins = 0, commits = 0, writes = 0;
  unsigned char bytes[512] = {};
  bool begin(unsigned) { ++begins; return true; }
  template<class T> void get(unsigned offset, T &value) {
    assert(offset + sizeof(T) <= sizeof(bytes));
    std::memcpy(&value, bytes + offset, sizeof(T));
  }
  template<class T> void put(unsigned offset, const T &value) {
    assert(offset + sizeof(T) <= sizeof(bytes));
    ++writes; std::memcpy(bytes + offset, &value, sizeof(T));
  }
  bool commit() { ++commits; return true; }
};
extern EEPROMClass EEPROM;
