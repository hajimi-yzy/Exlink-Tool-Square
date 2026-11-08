#include "mpu6050_level.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

static void word(uint8_t *p, int16_t value) {
  p[0] = uint16_t(value) >> 8;
  p[1] = uint16_t(value) & 255;
}

static void gravity(uint8_t *p, float roll, float pitch) {
  memset(p, 0, 14);
  const float r = roll * 0.0174532925199f, t = pitch * 0.0174532925199f;
  word(p, int16_t(lroundf(-sinf(t) * 16384)));
  word(p + 2, int16_t(lroundf(sinf(r) * cosf(t) * 16384)));
  word(p + 4, int16_t(lroundf(cosf(r) * cosf(t) * 16384)));
}

static void near(float actual, float expected, float tolerance = 0.02f) {
  assert(fabsf(exlink::levelAngleDifference(actual, expected)) < tolerance);
}

struct FakeBus {
  uint8_t device_address = 0x68, id = 0x68;
  uint8_t regs[256] = {};
  uint8_t data[14] = {};
  uint8_t address = 0, reg = 0, index = 0;
  bool offline = false, short_read = false, stuck_reset = false;
  uint8_t ignored_write = 0;
  unsigned transactions = 0, bursts = 0, sleep_writes = 0, resets = 0, signal_resets = 0;
  std::vector<uint8_t> tx;

  FakeBus() { gravity(data, 0, 0); regs[0x6B] = 0x40; }
  void beginTransmission(uint8_t addr) { address = addr; tx.clear(); }
  size_t write(uint8_t value) { tx.push_back(value); return 1; }
  uint8_t endTransmission(bool stop) {
    ++transactions;
    if (offline || address != device_address) return 2;
    assert(tx.size() == 1 || tx.size() == 2);
    reg = tx[0];
    if (tx.size() == 1) assert(!stop); // Repeated START before a register read.
    else {
      assert(stop);
      if (reg == 0x6B && tx[1] == 0x80) {
        ++resets;
        memset(regs, 0, sizeof(regs));
        regs[0x6B] = stuck_reset ? 0x80 : 0x40;
        regs[0x1C] = 0x18; // Requires explicit range programming after reset.
      } else if (reg != ignored_write) regs[reg] = tx[1];
      if (reg == 0x68 && tx[1] == 0x07) {
        assert(resets > signal_resets);
        ++signal_resets;
      }
      if (reg == 0x6B && (tx[1] & 0x40)) ++sleep_writes;
    }
    return 0;
  }
  size_t requestFrom(uint8_t addr, size_t length, bool stop) {
    assert(addr == device_address && stop);
    index = 0;
    if (reg == 0x3B) {
      assert(length == 14 && signal_resets && regs[0x6B] == 1 && regs[0x6C] == 0);
      ++bursts;
    } else assert(length == 1);
    return short_read ? 0 : length;
  }
  int read() { return reg == 0x75 ? id : reg == 0x3B ? data[index++] : regs[reg]; }
};

static void filter_tests() {
  exlink::LevelFilter filter;
  exlink::LevelSample s;
  uint8_t bytes[14];
  gravity(bytes, 30, -20);
  assert(filter.update(bytes, 100, s));
  near(s.roll_deg, 30); near(s.pitch_deg, -20);
  assert(!s.stable);
  for (uint32_t now = 140; now <= 700; now += 40) assert(filter.update(bytes, now, s));
  assert(s.stable);
  // Movement invalidates calibration immediately, without integrating drift.
  word(bytes + 8, 1310); // 10 deg/s.
  assert(filter.update(bytes, 740, s)); assert(!s.stable);
  // A long bus stall cannot reuse an old stability window or smoothed angle.
  gravity(bytes, -45, 15);
  assert(filter.update(bytes, 1300, s)); assert(!s.stable);
  near(s.roll_deg, -45); near(s.pitch_deg, 15);
  // Invalid gravity is not displayed as zero. Recovery earns stability again.
  memset(bytes, 0, sizeof(bytes));
  assert(!filter.update(bytes, 1340, s)); assert(!s.available && !s.stable);
  assert(s.sampled_ms == 1340 && s.gravity_g == 0 && s.sample_sequence > 0);
  // Failed gravity still carries real raw samples and temperature for diagnosis.
  word(bytes, 32767); word(bytes + 6, 340); word(bytes + 8, -262);
  assert(!filter.update(bytes, 1350, s));
  assert(s.accel_raw[0] == 32767 && s.gyro_raw[0] == -262);
  assert(fabsf(s.temperature_c - 37.53f) < 0.01f && s.gravity_g > 1.9f);
  gravity(bytes, 0, 0);
  assert(filter.update(bytes, 1380, s)); near(s.roll_deg, 0); assert(!s.stable);
  // +/-180 wrap must follow the short path, not jump across zero degrees.
  filter.reset(); gravity(bytes, 179, 0); filter.update(bytes, 100, s);
  gravity(bytes, -179, 0); filter.update(bytes, 140, s);
  assert(fabsf(s.roll_deg) > 178);
  near(exlink::levelAngleDifference(-179, 179), 2);
  near(exlink::levelAngleDifference(179, -179), -2);
  // Timestamps and stability are valid across the millis() wrap.
  filter.reset(); gravity(bytes, 0, 0);
  uint32_t start = UINT32_MAX - 200U;
  for (uint32_t offset = 0; offset <= 640; offset += 40) filter.update(bytes, start + offset, s);
  assert(s.stable);
  filter.reset(); filter.update(bytes, 800, s); assert(!s.stable);
}

static void driver_tests(uint8_t address) {
  FakeBus bus; bus.device_address = address;
  exlink::Mpu6050Level<FakeBus> driver(bus);
  for (uint32_t now = 0; now < 100; now += 2) driver.step(false, now);
  assert(bus.transactions == 0); // No home-page bus load.
  for (uint32_t now = 100; now < 400; now += 2) driver.step(true, now);
  assert(!driver.sample().available && bus.bursts == 0);
  for (uint32_t now = 400; now <= 1200; now += 2) driver.step(true, now);
  assert(driver.sample().available && driver.sample().stable);
  assert(driver.sample().configured && bus.resets == 1 && bus.signal_resets == 1);
  assert(driver.sample().power_mgmt_1 == 1 && driver.sample().power_mgmt_2 == 0);
  assert(driver.sample().accel_config == 0 && driver.sample().gyro_config == 0);
  assert(driver.sample().who_am_i == 0x68 && driver.sample().address == address);
  assert(bus.regs[0x1A] == 3 && bus.regs[0x19] == 19);
  assert(bus.regs[0x1B] == 0 && bus.regs[0x1C] == 0);
  assert(bus.bursts >= 15 && bus.bursts <= 25);
  driver.resetFilter(); assert(!driver.sample().stable && !driver.sample().available);
  for (uint32_t now = 1202; now <= 1300; now += 2) driver.step(true, now);
  assert(driver.sample().available && !driver.sample().stable);
  driver.step(false, 1302); assert(bus.sleep_writes == 1);
  const unsigned stopped = bus.transactions;
  for (uint32_t now = 1304; now <= 1400; now += 2) driver.step(false, now);
  assert(bus.transactions == stopped && !driver.sample().available);
  for (uint32_t now = 1402; now <= 1900; now += 2) driver.step(true, now);
  assert(driver.sample().available && !driver.sample().stable);
  assert(bus.resets == 2);
  bus.short_read = true;
  for (uint32_t now = 1902; now <= 2000; now += 2) driver.step(true, now);
  assert(!driver.sample().available && driver.sample().who_am_i == 0);
  const unsigned failed = bus.transactions;
  for (uint32_t now = 2002; now <= 2800; now += 2) driver.step(true, now);
  assert(bus.transactions == failed); // Fault backoff, not continuous probing.
  bus.short_read = false;
  for (uint32_t now = 2802; now <= 4800; now += 2) driver.step(true, now);
  assert(driver.sample().available);
  bus.offline = true;
  for (uint32_t now = 4802; now <= 4900; now += 2) driver.step(true, now);
  assert(!driver.sample().available);
}

static void startup_fault_tests() {
  FakeBus stuck; stuck.stuck_reset = true;
  exlink::Mpu6050Level<FakeBus> reset_timeout(stuck);
  for (uint32_t now = 0; now <= 800; now += 2) reset_timeout.step(true, now);
  assert(!reset_timeout.sample().configured && stuck.bursts == 0 && stuck.resets == 1);
  assert(reset_timeout.sample().error_register == 0x6B);
  const unsigned count = stuck.transactions;
  for (uint32_t now = 802; now <= 1800; now += 2) reset_timeout.step(true, now);
  assert(stuck.transactions == count); // Bounded reset poll then backoff.

  FakeBus ignored; ignored.ignored_write = 0x1C;
  exlink::Mpu6050Level<FakeBus> bad_config(ignored);
  for (uint32_t now = 0; now <= 900; now += 2) bad_config.step(true, now);
  assert(!bad_config.sample().configured && ignored.bursts == 0);
  assert(bad_config.sample().error_register == 0x1C && bad_config.sample().error_value == 0x18);
}

int main() {
  filter_tests();
  driver_tests(0x68);
  driver_tests(0x69);
  startup_fault_tests();
  FakeBus bus; bus.id = 0x70; // An unrelated IMU must not be called MPU6050.
  exlink::Mpu6050Level<FakeBus> wrong(bus);
  for (uint32_t now = 0; now < 1000; now += 2) wrong.step(true, now);
  assert(!wrong.sample().available && bus.bursts == 0);
  puts("MPU6050 level tests passed: math, raw diagnostics, stability, wrap, reset, config readback, protocol, address, sleep, backoff");
}
