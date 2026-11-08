#pragma once

#include <stddef.h>
#include <stdint.h>

namespace exlink {

struct Aht20Sample {
  bool available = false;
  bool calibrated = false;
  float temperature_c = 0, humidity_rh = 0;
  uint32_t sampled_ms = 0, sample_sequence = 0;
  uint8_t status = 0, error = 0, status_method = 0;
  uint8_t raw[7] = {};
};

// Protocol and 20-bit conversion adapted from the user's HaJi Spider project:
// tmp/android_app_publish_20260731/H7_onboard/MDK-ARM/USER/APP/env_sensors.c.
// Status fallback, busy polling, reset settling, CRC and conversion follow that
// newer driver; blocking waits are split into deadlines for the shared worker.
// On activation, reset once before calibration (Aosong soft-reset command;
// also used by Adafruit_AHTX0::begin). Do not loop resets for a zero RH value.
// Only the internal I2C worker may call step(); no begin(), delay(), or retries
// inside a transaction. AHT20 has no register prefix for status/result reads.
template <class Bus> class Aht20Sensor {
 public:
  enum Error : uint8_t { None, BusError, BusyTimeout, Calibration, Crc, Range };
  explicit Aht20Sensor(Bus &bus) : bus_(bus) {}
  const Aht20Sample &sample() const { return sample_; }

  bool step(bool enabled, uint32_t now) {
    if (!enabled) {
      if (!enabled_) return false;
      enabled_ = false;
      sample_ = Aht20Sample{};
      return true; // AHT20 returns to idle automatically; no sleep transaction.
    }
    if (!enabled_) {
      enabled_ = true;
      sample_ = Aht20Sample{};
      reset_attempted_ = false;
      state_ = State::Reset;
      due_ms_ = now + 40U;
      busy_deadline_ms_ = now + 240U;
    }
    if (int32_t(now - due_ms_) < 0) return false;
    switch (state_) {
      case State::Status:
      case State::Verify:
      case State::Ready: {
        uint8_t status = 0;
        status_resume_ = state_;
        if (!read(&status, 1)) {
          state_ = State::StatusCommand;
          return false;
        }
        sample_.status_method = 1; // Spider's direct-read status mode.
        return acceptStatus(status, status_resume_, now);
      }
      case State::StatusCommand: {
        const uint8_t command = 0x71;
        if (!write(&command, 1)) return statusFailed(now);
        state_ = State::StatusReply;
        return false;
      }
      case State::StatusReply: {
        uint8_t status = 0;
        if (!read(&status, 1)) return statusFailed(now);
        sample_.status_method = 2;
        return acceptStatus(status, status_resume_, now);
      }
      case State::Initialize: {
        const uint8_t command[] = {0xBE, 0x08, 0x00};
        if (!write(command, sizeof(command))) return fail(now, BusError);
        state_ = State::Verify;
        due_ms_ = now + 20U;
        busy_deadline_ms_ = now + 200U;
        return false;
      }
      case State::Reset: {
        const uint8_t command = 0xBA;
        if (!write(&command, 1)) return fail(now, BusError);
        reset_attempted_ = true;
        state_ = State::Initialize;
        due_ms_ = now + 200U;
        return false;
      }
      case State::Trigger: {
        const uint8_t command[] = {0xAC, 0x33, 0x00};
        if (!write(command, sizeof(command))) return fail(now, BusError);
        state_ = State::Ready;
        due_ms_ = now + 80U;
        busy_deadline_ms_ = now + 100U; // 80 ms conversion + 20 ms busy polling.
        return false;
      }
      case State::Result: {
        uint8_t data[7];
        if (!read(data, sizeof(data))) return fail(now, BusError);
        for (size_t i = 0; i < sizeof(data); ++i) sample_.raw[i] = data[i];
        sample_.status = data[0];
        if (data[0] & 0x80) return fail(now, BusyTimeout);
        if (!(data[0] & 0x08)) return fail(now, Calibration);
        if (checksum(data) != data[6]) return fail(now, Crc);
        const uint32_t hum = (uint32_t(data[1]) << 12) |
            (uint32_t(data[2]) << 4) | (data[3] >> 4);
        const uint32_t temp = (uint32_t(data[3] & 0x0F) << 16) |
            (uint32_t(data[4]) << 8) | data[5];
        const float humidity = float(hum) * 100.0f / 1048576.0f;
        const float temperature = float(temp) * 200.0f / 1048576.0f - 50.0f;
        if (humidity < 0 || humidity > 100 || temperature < -40 || temperature > 85)
          return fail(now, Range);
        sample_.humidity_rh = humidity;
        sample_.temperature_c = temperature;
        sample_.available = sample_.calibrated = true;
        sample_.error = None;
        sample_.sampled_ms = now;
        ++sample_.sample_sequence;
        state_ = State::Trigger;
        due_ms_ = now + 1000U;
        return true;
      }
    }
    return false;
  }

 private:
  enum class State : uint8_t {
    Status, Initialize, Verify, Reset, Trigger, Ready, Result,
    StatusCommand, StatusReply
  };
  bool acceptStatus(uint8_t status, State phase, uint32_t now) {
    sample_.status = status;
    state_ = phase;
    if (status & 0x80) {
      if (int32_t(now - busy_deadline_ms_) >= 0) return fail(now, BusyTimeout);
      due_ms_ = now + (phase == State::Ready ? 1U : 10U);
      return false;
    }
    if (phase == State::Ready) {
      state_ = State::Result;
      due_ms_ = now;
      return false;
    }
    if (!(status & 0x08)) {
      if (phase == State::Verify && reset_attempted_) return fail(now, Calibration);
      state_ = phase == State::Verify ? State::Reset : State::Initialize;
      due_ms_ = now;
      return false;
    }
    sample_.calibrated = true;
    sample_.error = None;
    state_ = State::Trigger;
    due_ms_ = now;
    return true;
  }
  bool statusFailed(uint32_t now) {
    if (status_resume_ != State::Ready && !reset_attempted_) {
      state_ = State::Reset;
      due_ms_ = now;
      return false;
    }
    return fail(now, BusError);
  }
  static uint8_t checksum(const uint8_t *bytes) {
    uint8_t crc = 0xFF;
    for (unsigned i = 0; i < 6; ++i) {
      crc ^= bytes[i];
      for (unsigned bit = 0; bit < 8; ++bit)
        crc = uint8_t((crc << 1) ^ ((crc & 0x80) ? 0x31 : 0));
    }
    return crc;
  }
  bool write(const uint8_t *bytes, size_t count) {
    bus_.beginTransmission(uint8_t(0x38));
    for (size_t i = 0; i < count; ++i) bus_.write(bytes[i]);
    return bus_.endTransmission(true) == 0;
  }
  bool read(uint8_t *bytes, size_t count) {
    if (bus_.requestFrom(uint8_t(0x38), count, true) != count) return false;
    for (size_t i = 0; i < count; ++i) {
      const int value = bus_.read();
      if (value < 0) return false;
      bytes[i] = uint8_t(value);
    }
    return true;
  }
  bool fail(uint32_t now, Error error) {
    sample_.available = false;
    sample_.error = error;
    if (error == Calibration) sample_.calibrated = false;
    state_ = State::Status;
    reset_attempted_ = false;
    due_ms_ = now + 2000U;
    busy_deadline_ms_ = due_ms_ + 200U;
    return true;
  }
  Bus &bus_;
  Aht20Sample sample_;
  State state_ = State::Status;
  State status_resume_ = State::Status;
  bool enabled_ = false, reset_attempted_ = false;
  uint32_t due_ms_ = 0, busy_deadline_ms_ = 0;
};

} // namespace exlink
