#include <INA.h>
#include <EEPROM.h>
#include <Wire.h>
#include <assert.h>
#include <stdio.h>
TwoWire Wire;
EEPROMClass EEPROM;

static void ramAndSleepCycles() {
  Wire = TwoWire{}; EEPROM = EEPROMClass{};
  Wire.second_device = true;
  INA_Class ina(1);
  assert(ina.begin(10, 10000) == 1);
  assert(ina.getDeviceAddress(0) == 0x40);
  // A bounded array keeps its device count at one, rather than modulo one.
  assert(ina.begin(10, 10000) == 1);
  for (unsigned i = 0; i < 1000; ++i) {
    ina.setMode(INA_MODE_POWER_DOWN, 0);
    ina.setMode(INA_MODE_CONTINUOUS_BOTH, 0);
  }
  assert(EEPROM.begins == 0 && EEPROM.writes == 0 && EEPROM.commits == 0);
  assert(ina.getBusMilliVolts(0) == 5000 && ina.lastReadOk());
  puts("PASS real INA RAM capacity and 1000 sleep cycles without NVS writes");
}

static void transportFailures() {
  Wire = TwoWire{}; EEPROM = EEPROMClass{};
  INA_Class ina(1);
  assert(ina.begin(10, 10000) == 1);
  Wire.nack = true;
  const unsigned before = Wire.requests;
  assert(ina.getBusMilliVolts(0) == 0 && !ina.lastReadOk());
  assert(Wire.requests == before);
  Wire.nack = false; Wire.short_read = true;
  assert(ina.getBusMilliVolts(0) == 0 && !ina.lastReadOk());
  Wire.short_read = false; Wire.empty_read = true;
  assert(ina.getBusMilliVolts(0) == 0 && !ina.lastReadOk());
  Wire.empty_read = false;
  assert(ina.getBusMilliVolts(0) == 5000 && ina.lastReadOk());
  puts("PASS real INA NACK, truncated/empty read, and later recovery");
}

static void failedEnumeration() {
  Wire = TwoWire{}; EEPROM = EEPROMClass{};
  Wire.fail_id = true;
  INA_Class ina(1);
  for (unsigned i = 0; i < 100; ++i) assert(ina.begin(10, 10000) == 0);
  assert(EEPROM.begins == 0 && EEPROM.commits == 0);
  Wire.fail_id = false;
  assert(ina.begin(10, 10000) == 1);
  assert(ina.getDeviceAddress(0) == 0x40);
  // INA3221 needs three cache slots and cannot overrun a one-device array.
  Wire = TwoWire{}; Wire.reset_config = 0x7127;
  INA_Class too_small(1);
  assert(too_small.begin(10, 10000) == 0);
  puts("PASS failed ID does not misclassify a device, RAM retries do not open NVS");
}

int main() {
  ramAndSleepCycles(); transportFailures(); failedEnumeration();
  puts("All real INA transport tests passed.");
}
