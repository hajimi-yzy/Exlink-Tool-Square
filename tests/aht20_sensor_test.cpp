#include "aht20_sensor.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

// Model only documented wire behavior, including Spider's 0x71 status fallback.
// Readiness is a one-byte transfer, separate from seven-byte conversion data.
struct FakeAhtBus {
  std::vector<uint8_t> tx;
  uint8_t status = 0x18;
  // Independently fixed CRC vector: 50% RH, 25 C (raw 0x80000, 0x60000).
  uint8_t frame[7] = {0x08, 0x80, 0x00, 0x06, 0x00, 0x00, 0xA6};
  uint8_t index = 0, response_status = 0;
  bool offline = false, short_read = false, busy = false, bad_crc = false;
  bool calibration_works = true, require_status_command = false;
  bool status_command_ready = false, measurement_pending = false;
  bool ready_status_after_trigger = false;
  unsigned transfers = 0, triggers = 0, resets = 0, initializations = 0;
  unsigned status_commands = 0, direct_status_failures = 0, result_reads = 0;
  unsigned conversion_busy_polls = 0;
  uint32_t now = 0, trigger_at = 0, reset_at = 0, result_at = 0;
  size_t rx_size = 0;

  void beginTransmission(uint8_t address) { assert(address == 0x38); tx.clear(); }
  size_t write(uint8_t value) { tx.push_back(value); return 1; }
  uint8_t endTransmission(bool stop) {
    ++transfers;
    assert(stop && !tx.empty());
    if (offline) return 2;
    switch (tx[0]) {
      case 0x71:
        assert(tx.size() == 1);
        ++status_commands;
        status_command_ready = true;
        break;
      case 0xBE:
        assert(tx.size() == 3 && tx[1] == 8 && tx[2] == 0);
        if (resets) assert(uint32_t(now - reset_at) >= 200U);
        ++initializations;
        if (calibration_works) status |= 8;
        break;
      case 0xBA:
        assert(tx.size() == 1);
        ++resets;
        reset_at = now;
        measurement_pending = false;
        ready_status_after_trigger = false;
        break;
      case 0xAC:
        assert(tx.size() == 3 && tx[1] == 0x33 && tx[2] == 0);
        ++triggers;
        trigger_at = now;
        measurement_pending = true;
        ready_status_after_trigger = false;
        break;
      default: assert(false);
    }
    return 0;
  }
  size_t requestFrom(uint8_t address, size_t count, bool stop) {
    ++transfers;
    assert(address == 0x38 && stop && (count == 1 || count == 7));
    index = 0;
    rx_size = count;
    if (offline) return 0;
    if (count == 1) {
      if (require_status_command && !status_command_ready) {
        ++direct_status_failures;
        return 0;
      }
      status_command_ready = false;
      response_status = status | (busy ? 0x80 : 0);
      if (measurement_pending) {
        assert(uint32_t(now - trigger_at) >= 80U);
        if (conversion_busy_polls) {
          --conversion_busy_polls;
          response_status |= 0x80;
        }
        if (!(response_status & 0x80)) ready_status_after_trigger = true;
      }
    } else {
      ++result_reads;
      assert(measurement_pending && ready_status_after_trigger);
      assert(uint32_t(now - trigger_at) >= 80U);
      result_at = now;
      measurement_pending = false;
      if (short_read) return 0;
    }
    return count;
  }
  int read() {
    if (rx_size == 1) return response_status;
    const uint8_t at = index++;
    return at == 6 && bad_crc ? frame[6] ^ 1 : frame[at];
  }
};

typedef exlink::Aht20Sensor<FakeAhtBus> Sensor;

static void tick(Sensor &sensor, FakeAhtBus &bus, uint32_t now,
                 bool enabled = true) {
  bus.now = now;
  const unsigned before = bus.transfers;
  sensor.step(enabled, now);
  assert(bus.transfers - before <= 1); // Protect the shared touch worker budget.
}

static void until(Sensor &sensor, FakeAhtBus &bus, uint32_t end) {
  while (int32_t(end - bus.now) >= 2) tick(sensor, bus, bus.now + 2U);
}

static void expectRaw(const Sensor &sensor, const uint8_t *expected) {
  assert(memcmp(sensor.sample().raw, expected, 7) == 0);
}

int main() {
  {
    FakeAhtBus bus;
    Sensor sensor(bus);
    tick(sensor, bus, 0);
    tick(sensor, bus, 39); assert(bus.transfers == 0);
    tick(sensor, bus, 40); assert(bus.resets == 1 && !sensor.sample().calibrated);
    tick(sensor, bus, 239); assert(bus.initializations == 0);
    tick(sensor, bus, 240); assert(bus.initializations == 1);
    tick(sensor, bus, 259); assert(!sensor.sample().calibrated);
    tick(sensor, bus, 260); assert(sensor.sample().calibrated);
    tick(sensor, bus, 262); assert(bus.triggers == 1);
    until(sensor, bus, 340);
    assert(bus.transfers == 4); // No I/O during the 80 ms conversion.
    until(sensor, bus, 360);
    assert(sensor.sample().available && sensor.sample().sample_sequence == 1);
    assert(sensor.sample().temperature_c == 25.0f && sensor.sample().humidity_rh == 50.0f);
    assert(bus.result_reads == 1);
    expectRaw(sensor, bus.frame);
    const uint32_t first_result = bus.result_at;
    until(sensor, bus, first_result + 998U); assert(bus.triggers == 1);
    until(sensor, bus, first_result + 1000U); assert(bus.triggers == 2);
    bus.bad_crc = true;
    until(sensor, bus, first_result + 1100U);
    assert(!sensor.sample().available && sensor.sample().error == sensor.Crc);
    uint8_t damaged[7]; memcpy(damaged, bus.frame, 7); damaged[6] ^= 1;
    expectRaw(sensor, damaged);
    const unsigned count = bus.transfers;
    until(sensor, bus, first_result + 3000U); assert(bus.transfers == count);
    bus.bad_crc = false;
    until(sensor, bus, first_result + 3250U);
    assert(sensor.sample().available && sensor.sample().sample_sequence == 2);
    tick(sensor, bus, bus.now + 2U, false);
    assert(!sensor.sample().available && sensor.sample().sample_sequence == 0);
    const unsigned stopped = bus.transfers;
    tick(sensor, bus, bus.now + 20000U, false); assert(bus.transfers == stopped);
    puts("PASS: one activation reset, 200 ms settling, CRC frame, cadence, retry and stop");
  }
  {
    FakeAhtBus bus;
    // AHT2x calibration is bit 3, not an AHT1x bits 5/6 mode gate.
    bus.status = 0x68;
    bus.require_status_command = true;
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 500);
    assert(sensor.sample().available && bus.initializations == 1 && bus.resets == 1);
    assert(bus.direct_status_failures >= 2 && bus.status_commands >= 2);
    assert(bus.result_reads == 1 && sensor.sample().status_method == 2);
    puts("PASS: calibration bit and direct-status failure use bounded 0x71 fallback");
  }
  {
    FakeAhtBus bus;
    // Fixed independent CRC vector: shared byte 0x96 exercises both nibbles.
    const uint8_t nonuniform[] = {0x18, 0xA5, 0xC3, 0x96, 0xB2, 0xD4, 0x63};
    memcpy(bus.frame, nonuniform, 7);
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 500);
    assert(sensor.sample().available);
    assert(fabsf(sensor.sample().humidity_rh - 64.7515297f) < 0.0001f);
    assert(fabsf(sensor.sample().temperature_c - 33.7318420f) < 0.0001f);
    expectRaw(sensor, nonuniform);
    puts("PASS: asymmetric humidity/temperature packing and retained wire bytes");
  }
  {
    FakeAhtBus bus;
    // Zero RH is valid protocol data. Do not fabricate replacement humidity.
    const uint8_t zero_humidity[] = {0x18, 0x00, 0x00, 0x06, 0xE6, 0x17, 0x68};
    memcpy(bus.frame, zero_humidity, 7);
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 2500);
    assert(sensor.sample().available && sensor.sample().humidity_rh == 0.0f);
    assert(fabsf(sensor.sample().temperature_c - 36.2348557f) < 0.0001f);
    assert(bus.resets == 1 && bus.initializations == 1);
    assert(sensor.sample().sample_sequence >= 2); // Zero RH cannot cause a reset loop.
    expectRaw(sensor, zero_humidity);
    puts("PASS: captured zero-RH frame stays visible and cannot trigger repeated resets");
  }
  {
    FakeAhtBus bus;
    bus.offline = true;
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 2000);
    assert(!sensor.sample().available && sensor.sample().error == sensor.BusError);
    assert(bus.transfers <= 5);
    bus.offline = false;
    until(sensor, bus, 2500);
    assert(sensor.sample().available);
    bus.short_read = true;
    until(sensor, bus, 3700);
    assert(!sensor.sample().available && sensor.sample().error == sensor.BusError);
    puts("PASS: absent/reconnected sensor, bounded backoff and short results");
  }
  {
    FakeAhtBus bus;
    bus.calibration_works = false;
    bus.status = 0;
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 900);
    assert(sensor.sample().error == sensor.Calibration && bus.resets == 1);
    assert(bus.initializations == 1);
    // Fake enforces >=200 ms between BA and the following BE.
    puts("PASS: one reset, 200 ms reset settling and finite calibration retries");
  }
  {
    FakeAhtBus bus;
    bus.conversion_busy_polls = 3;
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 500);
    assert(sensor.sample().available && bus.result_reads == 1);
    assert(bus.conversion_busy_polls == 0);
    puts("PASS: busy conversion is status-polled before the result transfer");
  }
  {
    FakeAhtBus bus;
    Sensor sensor(bus);
    tick(sensor, bus, 0); until(sensor, bus, 300);
    bus.busy = true;
    until(sensor, bus, 600);
    assert(!sensor.sample().available && sensor.sample().error == sensor.BusyTimeout);
    assert(bus.result_reads == 0 && bus.transfers < 40);
    puts("PASS: stuck conversion busy terminates without a false result");
  }
  {
    const uint8_t extreme_frames[][7] = {
      {0x18, 0x80, 0x00, 0x0F, 0x00, 0x00, 0x37}, // +137.5 C
      {0x18, 0x80, 0x00, 0x00, 0x00, 0x00, 0x86}  // -50 C
    };
    for (const auto &frame : extreme_frames) {
      FakeAhtBus bus;
      memcpy(bus.frame, frame, 7);
      Sensor sensor(bus);
      tick(sensor, bus, 0); until(sensor, bus, 500);
      assert(!sensor.sample().available && sensor.sample().error != sensor.None);
      expectRaw(sensor, frame);
    }
    puts("PASS: Spider -40..85 C range rejects valid-CRC temperature extremes");
  }
  {
    FakeAhtBus bus;
    bus.status = 0;
    Sensor sensor(bus);
    const uint32_t start = UINT32_MAX - 20;
    for (uint32_t elapsed = 0; elapsed < 500; elapsed += 2)
      tick(sensor, bus, start + elapsed);
    assert(sensor.sample().available && sensor.sample().sample_sequence == 1);
    puts("PASS: calibration and conversion deadlines across millis rollover");
  }
}
