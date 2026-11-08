#include "internal_i2c_service.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace host { Runtime runtime; }

namespace {
void require(bool value, const char *message) {
  if (!value) throw std::runtime_error(message);
}
uint8_t crc(const uint8_t *bytes) {
  uint8_t value = 0xFF;
  for (unsigned i = 0; i < 6; ++i) {
    value ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      value = uint8_t((value << 1) ^ ((value & 0x80) ? 0x31 : 0));
  }
  return value;
}
void notifyBus(const host::BusEvent &event) {
  if (host::runtime.bus_hook) host::runtime.bus_hook(event);
}
}

uint8_t TwoWire::endTransmission(bool) {
  const host::BusEvent event{millis(), address_, false, tx_, tx_.size()};
  events.push_back(event);
  const bool failed = ((address_ == 0x68 || address_ == 0x69) && fail_mpu) ||
                      (address_ == 0x38 && fail_aht);
  if (failed) host::advance(timeout);
  if (!failed && address_ == 0x2F && !tx_.empty()) wiper = tx_[0];
  if (!failed && address_ == 0x68 && !tx_.empty()) {
    reg_ = tx_[0];
    if (tx_.size() == 2) {
      mpu[reg_] = tx_[1];
      if (reg_ == 0x6B && tx_[1] == 0x80) mpu[reg_] = 0;
    }
  }
  notifyBus(event);
  return failed ? 4 : 0;
}

size_t TwoWire::requestFrom(uint8_t address, size_t count, bool) {
  const host::BusEvent event{millis(), address, true, {reg_}, count};
  events.push_back(event);
  rx_.clear();
  const bool failed = ((address == 0x68 || address == 0x69) && fail_mpu) ||
                      (address == 0x38 && fail_aht);
  if (failed) {
    host::advance(timeout);
    notifyBus(event);
    return 0;
  }
  if (address == 0x2F) {
    rx_.push_back(wiper);
  } else if (address == 0x38) {
    uint8_t frame[] = {0x08, 0x80, 0x00, 0x06, 0x00, 0x00, 0};
    frame[6] = crc(frame) ^ (corrupt_aht_crc ? 1 : 0);
    for (size_t i = 0; i < count; ++i) rx_.push_back(frame[i]);
  } else if (address == 0x68 && reg_ == 0x3B && count == 14) {
    // Static 30-degree roll, zero pitch, 1 g, and stationary gyro.
    const uint8_t frame[] = {0, 0, 0x20, 0, 0x37, 0x6D, 0, 0, 0, 0, 0, 0, 0, 0};
    rx_.insert(rx_.end(), frame, frame + sizeof(frame));
  } else if (address == 0x68) {
    for (size_t i = 0; i < count; ++i) rx_.push_back(mpu[uint8_t(reg_ + i)]);
  } else {
    for (size_t i = 0; i < count; ++i) rx_.push_back(0);
  }
  // Hooks run after bytes arrive but before the real driver/service publishes.
  // This can reproduce a UI page closing during an in-flight transaction.
  notifyBus(event);
  return rx_.size();
}

namespace {
using namespace exlink;
struct Fixture {
  TwoWire wire;
  CST9220 touch;
  INA_Class ina;
  Mcp4017Controller<TwoWire> power;
  InternalI2cService service;
  Fixture() : power(wire), service(wire, touch, ina, power) {
    host::runtime = host::Runtime{};
    require(service.begin(true, 0), "begin worker");
    require(wire.timeout == 20, "shared Wire timeout remains bounded");
  }
  void run(uint32_t deadline, const std::function<void()> &hook = {}) {
    host::runtime.deadline = deadline;
    host::runtime.tick_hook = hook;
    try {
      host::runtime.entry(host::runtime.context);
      require(false, "worker unexpectedly returned");
    } catch (const host::StopWorker &) {}
    host::runtime.tick_hook = {};
    host::runtime.bus_hook = {};
  }
  unsigned count(uint8_t address, bool read, size_t length = 0) const {
    unsigned result = 0;
    for (const auto &event : wire.events)
      if (event.address == address && event.read == read &&
          (!length || event.count == length)) ++result;
    return result;
  }
  unsigned sleeps() const {
    unsigned result = 0;
    for (const auto &event : wire.events)
      if (event.address == 0x68 && !event.read &&
          event.bytes == std::vector<uint8_t>({0x6B, 0x41})) ++result;
    return result;
  }
};

void checkTouchCadence(const Fixture &fixture, uint32_t maximum_gap) {
  require(fixture.touch.reads.size() > 30, "touch continues polling");
  for (size_t i = 1; i < fixture.touch.reads.size(); ++i) {
    const uint32_t gap = fixture.touch.reads[i] - fixture.touch.reads[i - 1];
    require(gap >= 16, "touch reads do not exceed established 16 ms cadence");
    require(gap <= maximum_gap, "sensor transactions do not starve touch");
  }
}

void fairnessAndExistingRequests() {
  Fixture fixture;
  auto &service = fixture.service;
  service.setLevelActive(true);
  service.setEnvironmentActive(true);
  require(service.submit(IoRequestKind::PowerWrite, 70, 11), "queue write");
  require(service.submit(IoRequestKind::PowerAdjust, 3, 12), "queue adjust");
  require(service.submit(IoRequestKind::PowerRead, 0, 13), "queue read");
  require(service.submit(IoRequestKind::Measurement, 0, 14), "queue INA");
  fixture.run(4000);
  checkTouchCadence(fixture, 18);
  require(fixture.touch.reads.size() >= 220, "continuous touch reports serviced");
  const auto level = service.levelSample();
  const auto environment = service.environmentSample();
  require(level.available && level.configured && level.stable, "MPU receives stable samples under continuous touch");
  require(level.sample_sequence >= 70, "continuous touch cannot starve MPU");
  require(environment.available && environment.sample_sequence >= 3, "continuous touch cannot starve AHT");
  require(std::fabs(environment.temperature_c - 25) < 0.01f &&
          std::fabs(environment.humidity_rh - 50) < 0.01f, "real AHT parser receives known valid frame");
  IoResult result;
  for (uint32_t id = 11; id <= 14; ++id) {
    require(service.takeResult(result), "every accepted legacy request completes");
    require(result.id == id && !result.cancelled, "legacy request ordering/identity");
    if (id < 14) {
      require(result.power.status == Mcp4017Status::Ok, "real MCP controller success");
      require(result.power.readback == (id == 11 ? 70 : 73), "power write/adjust/read values");
    } else {
      require(result.millivolts == 5001 && result.microamps == 123000 &&
              result.microwatts == 615123, "INA result preserved");
    }
  }
  require(!service.takeResult(result), "no duplicate request completion");
  require(fixture.ina.voltage_reads == 1 && fixture.ina.current_reads == 1 &&
          fixture.ina.power_reads == 1, "all INA channels read once");
  std::cout << "PASS continuous touch, real sensor steps, MCP/INA requests\n";
}

void absentSensorsBackOff() {
  Fixture fixture;
  fixture.wire.fail_mpu = fixture.wire.fail_aht = true;
  fixture.service.setLevelActive(true);
  fixture.service.setEnvironmentActive(true);
  fixture.run(6500);
  checkTouchCadence(fixture, 43);
  require(!fixture.service.levelSample().available &&
          !fixture.service.environmentSample().available, "absent sensors never publish valid data");
  require(fixture.count(0x68, false) <= 4 && fixture.count(0x69, false) <= 4,
          "absent MPU probes use two-second backoff");
  require(fixture.count(0x38, true) <= 4, "absent AHT uses two-second backoff");
  require(fixture.service.diagnostics().touch_errors == 0, "sensor faults do not corrupt touch state");
  std::cout << "PASS sensor timeout/backoff preserves touch cadence\n";
}

void pageDeactivation() {
  Fixture fixture;
  fixture.service.setLevelActive(true);
  fixture.service.setEnvironmentActive(true);
  bool disabled = false, quiet_marked = false;
  size_t quiet_count = 0;
  fixture.run(3000, [&] {
    if (!disabled && millis() >= 1500) {
      require(fixture.service.levelSample().available, "MPU available before page close");
      require(fixture.service.environmentSample().available, "AHT available before page close");
      fixture.service.setLevelActive(false);
      fixture.service.setEnvironmentActive(false);
      require(!fixture.service.levelSample().available &&
              !fixture.service.environmentSample().available, "closing pages immediately invalidates caches");
      require(!fixture.service.zeroLevel(), "inactive page rejects zero");
      disabled = true;
    }
    if (!quiet_marked && millis() >= 1550) {
      quiet_count = fixture.wire.events.size(); quiet_marked = true;
    }
    if (quiet_marked)
      require(fixture.wire.events.size() == quiet_count, "inactive pages cause no continued sensor polling");
  });
  require(fixture.sleeps() == 1, "MPU sleeps exactly once on page deactivation");
  checkTouchCadence(fixture, 18);
  std::cout << "PASS page close clears caches, stops polling, sleeps MPU\n";
}

void pauseResumeAndOta() {
  Fixture fixture;
  auto &service = fixture.service;
  service.setLevelActive(true);
  service.setEnvironmentActive(true);
  unsigned phase = 0;
  size_t quiet_count = 0;
  fixture.run(4000, [&] {
    if (phase == 0 && millis() >= 1500) {
      require(service.submit(IoRequestKind::PowerWrite, 90, 90), "request accepted before pause");
      require(!service.pause(true, 0), "pause initially waits for worker quiescence");
      require(!service.levelSample().available && !service.environmentSample().available,
              "pause immediately invalidates both caches");
      require(!service.zeroLevel(), "paused worker rejects zero");
      require(!service.submit(IoRequestKind::PowerRead, 0, 91), "pause rejects new requests");
      phase = 1;
    } else if (phase == 1 && millis() >= 1550) {
      require(service.pause(true, 0), "pause acknowledges after sensor stop steps");
      require(fixture.sleeps() == 1, "MPU slept before pause acknowledged");
      require(fixture.ina.modes.size() == 1 && fixture.ina.modes[0].second == INA_MODE_POWER_DOWN,
              "sleep pause powers INA down");
      IoResult result;
      require(service.takeResult(result) && result.id == 90 && result.cancelled,
              "pause cancels queued request with completion");
      quiet_count = fixture.wire.events.size(); phase = 2;
    } else if (phase == 2 && millis() < 2000) {
      require(fixture.wire.events.size() == quiet_count, "paused worker does not access sensor bus");
    } else if (phase == 2 && millis() >= 2000) {
      service.resume();
      require(!service.levelSample().available && !service.environmentSample().available,
              "resume begins with empty sensor caches");
      phase = 3;
    } else if (phase == 3 && millis() >= 3400) {
      require(service.levelSample().available && service.environmentSample().available,
              "resume restarts active sensors with fresh samples");
      require(service.levelSample().sampled_ms > 2000 && service.environmentSample().sampled_ms > 2000,
              "resume never exposes pre-pause samples");
      require(fixture.ina.modes.size() == 2 && fixture.ina.modes[1].second == INA_MODE_CONTINUOUS_BOTH,
              "resume restores INA mode");
      require(!service.pause(false, 0), "OTA pause requests quiescence");
      phase = 4;
    } else if (phase == 4 && millis() >= 3450) {
      require(service.pause(false, 0), "OTA pause acknowledged");
      require(fixture.sleeps() == 2, "OTA pause also sleeps MPU");
      require(fixture.ina.modes.size() == 2, "OTA pause retains INA mode");
      require(!service.levelSample().available && !service.environmentSample().available,
              "OTA invalidates sensor caches");
      quiet_count = fixture.wire.events.size(); phase = 5;
    } else if (phase == 5) {
      require(fixture.wire.events.size() == quiet_count, "OTA pause keeps sensor bus quiet");
    }
  });
  require(phase == 5, "complete pause/resume/OTA lifecycle");
  std::cout << "PASS pause/resume/OTA cache invalidation, MPU sleep, INA modes\n";
}

void inFlightGenerationChanges() {
  Fixture fixture;
  auto &service = fixture.service;
  service.setLevelActive(true);
  service.setEnvironmentActive(true);
  bool level_changed = false, environment_changed = false;
  bool check_level = false, check_environment = false;
  uint32_t level_changed_ms = 0, environment_changed_ms = 0;
  host::runtime.bus_hook = [&](const host::BusEvent &event) {
    if (!level_changed && event.address == 0x68 && event.read && event.count == 14) {
      service.setLevelActive(false); service.setLevelActive(true);
      level_changed_ms = millis(); level_changed = check_level = true;
    }
    if (!environment_changed && event.address == 0x38 && event.read && event.count == 7) {
      service.setEnvironmentActive(false); service.setEnvironmentActive(true);
      environment_changed_ms = millis(); environment_changed = check_environment = true;
    }
  };
  fixture.run(2000, [&] {
    if (check_level) {
      require(!service.levelSample().available && service.levelSample().sample_sequence == 0,
              "in-flight old MPU completion rejected after rapid reopen");
      check_level = false;
    }
    if (check_environment) {
      require(!service.environmentSample().available && service.environmentSample().sample_sequence == 0,
              "in-flight old AHT completion rejected after rapid reopen");
      check_environment = false;
    }
  });
  require(level_changed && environment_changed, "both in-flight race hooks exercised");
  require(service.levelSample().available && service.levelSample().sampled_ms > level_changed_ms,
          "new MPU page eventually receives fresh sample");
  require(service.environmentSample().available && service.environmentSample().sampled_ms > environment_changed_ms,
          "new AHT page eventually receives fresh sample");
  std::cout << "PASS actual in-flight MPU/AHT completions reject stale generations\n";
}

void zeroFreshness() {
  Fixture fixture;
  auto &service = fixture.service;
  service.setLevelActive(true);
  require(!service.zeroLevel(), "zero rejects uninitialized cache");
  bool unstable_checked = false, zeroed = false, stale_checked = false;
  fixture.run(1800, [&] {
    const auto sample = service.levelSample();
    if (!unstable_checked && sample.available && !sample.stable) {
      require(!service.zeroLevel(), "zero rejects fresh but moving/unsettled estimate");
      unstable_checked = true;
    }
    if (!zeroed && sample.available && sample.stable) {
      require(std::fabs(sample.roll_deg) > 20, "tilted input exercises nontrivial zero offset");
      require(service.zeroLevel(), "fresh stable configured sample can be zeroed");
      require(std::fabs(service.levelSample().roll_deg) < 0.01f &&
              std::fabs(service.levelSample().pitch_deg) < 0.01f, "zero applies cached relative angles");
      require(service.zeroLevel() && std::fabs(service.levelSample().roll_deg) < 0.01f,
              "repeated zero stores raw cache instead of compounding relative offset");
      zeroed = true;
      // Simulate a delayed application task without mutating the service's
      // private cache. The next worker read has not occurred at this point.
      host::advance(251);
      require(!service.zeroLevel(), "zero rejects a stable cache older than 250 ms");
      stale_checked = true;
    }
  });
  require(unstable_checked && zeroed && stale_checked, "zero stability and age gates exercised");
  std::cout << "PASS zero requires fresh stable data and preserves raw reference\n";
}

void corruptAhtFrame() {
  Fixture fixture;
  fixture.wire.corrupt_aht_crc = true;
  fixture.service.setEnvironmentActive(true);
  fixture.run(1800);
  require(!fixture.service.environmentSample().available &&
          fixture.service.environmentSample().error == Aht20Sensor<TwoWire>::Crc,
          "service publishes CRC failure instead of accepting corrupt AHT frame");
  require(fixture.count(0x38, true, 7) == 1, "CRC failure backs off repeated result reads");
  checkTouchCadence(fixture, 18);
  std::cout << "PASS bad AHT CRC remains unavailable and backs off\n";
}
}

int main() {
  try {
    fairnessAndExistingRequests();
    absentSensorsBackOff();
    pageDeactivation();
    pauseResumeAndOta();
    inFlightGenerationChanges();
    zeroFreshness();
    corruptAhtFrame();
    std::cout << "All real InternalI2cService integration tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL at simulated " << millis() << " ms: " << error.what() << '\n';
    return 1;
  }
}
