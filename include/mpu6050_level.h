#pragma once

#include <stddef.h>
#include <stdint.h>
#include <math.h>

namespace exlink {

struct LevelSample {
  bool available = false;
  bool stable = false;
  float roll_deg = 0;
  float pitch_deg = 0;
  uint32_t sampled_ms = 0;
  uint8_t address = 0;
  uint8_t who_am_i = 0;
  bool configured = false;
  int16_t accel_raw[3] = {}, gyro_raw[3] = {};
  float gravity_g = 0, temperature_c = 0;
  uint32_t sample_sequence = 0;
  uint8_t power_mgmt_1 = 0, power_mgmt_2 = 0;
  uint8_t accel_config = 0, gyro_config = 0;
  uint8_t error_register = 0, error_value = 0;
};

inline float levelAngleDifference(float angle, float reference) {
  float difference = angle - reference;
  while (difference > 180.0f) difference -= 360.0f;
  while (difference < -180.0f) difference += 360.0f;
  return difference;
}

// Static inclination comes from gravity, not integration of gyro rates (which
// would drift). Gyro rates and acceleration magnitude gate zero calibration.
class LevelFilter {
 public:
  void reset() { initialized_ = false; still_ms_ = 0; last_ms_ = 0; }

  bool update(const uint8_t *bytes, uint32_t now, LevelSample &sample) {
    for (unsigned axis = 0; axis < 3; ++axis) {
      sample.accel_raw[axis] = signedWord(bytes + axis * 2);
      sample.gyro_raw[axis] = signedWord(bytes + 8 + axis * 2);
    }
    const float x = sample.accel_raw[0] / 16384.0f;
    const float y = sample.accel_raw[1] / 16384.0f;
    const float z = sample.accel_raw[2] / 16384.0f;
    const float gx = sample.gyro_raw[0] / 131.0f;
    const float gy = sample.gyro_raw[1] / 131.0f;
    const float gz = sample.gyro_raw[2] / 131.0f;
    const float magnitude = sqrtf(x * x + y * y + z * z);
    const uint32_t elapsed = now - last_ms_;
    sample.gravity_g = magnitude;
    sample.temperature_c = signedWord(bytes + 6) / 340.0f + 36.53f;
    sample.sampled_ms = now;
    ++sample.sample_sequence;
    // All-zero/freefall/saturated data has no usable gravity direction.
    if (magnitude < 0.5f || magnitude > 1.5f) {
      reset();
      sample.available = sample.stable = false;
      return false;
    }
    const float raw_roll = atan2f(y, z) * RadToDeg;
    const float raw_pitch = atan2f(-x, sqrtf(y * y + z * z)) * RadToDeg;
    const bool continuous = initialized_ && elapsed <= 200U;
    const bool still = continuous && magnitude > 0.92f && magnitude < 1.08f &&
        fabsf(gx) < 4.0f && fabsf(gy) < 4.0f && fabsf(gz) < 4.0f &&
        fabsf(levelAngleDifference(raw_roll, previous_roll_)) < 0.7f &&
        fabsf(raw_pitch - previous_pitch_) < 0.7f;
    still_ms_ = still ? (still_ms_ < 1000U ? still_ms_ + elapsed : 1000U) : 0U;
    if (!continuous) {
      roll_ = raw_roll;
      pitch_ = raw_pitch;
    } else {
      // Time-based 160 ms low-pass, including wraparound at +/-180 degrees.
      const float alpha = float(elapsed) / (160.0f + float(elapsed));
      roll_ += alpha * levelAngleDifference(raw_roll, roll_);
      roll_ = levelAngleDifference(roll_, 0);
      pitch_ += alpha * (raw_pitch - pitch_);
    }
    previous_roll_ = raw_roll;
    previous_pitch_ = raw_pitch;
    initialized_ = true;
    last_ms_ = now;
    sample.available = true;
    // Also wait for the displayed gravity estimate to settle before zeroing.
    sample.stable = still_ms_ >= 600U &&
        fabsf(levelAngleDifference(raw_roll, roll_)) < 0.35f &&
        fabsf(raw_pitch - pitch_) < 0.35f;
    sample.roll_deg = roll_;
    sample.pitch_deg = pitch_;
    sample.sampled_ms = now;
    return true;
  }

 private:
  static int16_t signedWord(const uint8_t *p) {
    return static_cast<int16_t>((uint16_t(p[0]) << 8) | p[1]);
  }
  static constexpr float RadToDeg = 57.2957795131f;
  bool initialized_ = false;
  uint32_t last_ms_ = 0, still_ms_ = 0;
  float roll_ = 0, pitch_ = 0, previous_roll_ = 0, previous_pitch_ = 0;
};

// Wire-like transport is supplied by the sole internal I2C worker. No begin(),
// bus reconfiguration, blocking startup delays, DMP, or interrupt pin required.
// Register map/scales checked against Adafruit_MPU6050's definitions; see
// docs/LEVEL_PAGE.md for the MPU-6050 register-map reference.
template <class Bus> class Mpu6050Level {
 public:
  explicit Mpu6050Level(Bus &bus) : bus_(bus) {}
  const LevelSample &sample() const { return sample_; }
  void resetFilter() {
    filter_.reset();
    sample_.available = sample_.stable = false;
  }

  // Returns true when the published cache must change (including failures).
  bool step(bool enabled, uint32_t now) {
    if (!enabled) {
      if (!enabled_) return false;
      // Best effort: once per deactivation, never repeatedly hammer a bad bus.
      if (awake_) {
        address_ = awake_address_;
        (void)writeRegister(0x6B, 0x41);
      }
      enabled_ = awake_ = false;
      state_ = State::ProbeLow;
      sample_ = LevelSample{};
      filter_.reset();
      return true;
    }
    if (!enabled_) {
      enabled_ = true;
      sample_ = LevelSample{};
      state_ = State::ProbeLow;
      due_ms_ = now;
    }
    if (int32_t(now - due_ms_) < 0) return false;
    switch (state_) {
      case State::ProbeLow:
      case State::ProbeHigh: {
        address_ = state_ == State::ProbeLow ? 0x68 : 0x69;
        uint8_t id = 0;
        if (!readRegister(0x75, &id, 1) || id != 0x68) {
          if (state_ == State::ProbeLow) {
            state_ = State::ProbeHigh;
            return false;
          }
          return fail(now);
        }
        sample_.address = address_;
        sample_.who_am_i = id;
        sample_.error_register = 0;
        state_ = State::DeviceReset;
        return true;
      }
      case State::DeviceReset:
        // Same reset order as Adafruit_MPU6050: device reset, settle,
        // signal-path reset, settle, then wake. No blocking waits on this bus.
        if (!writeRegister(0x6B, 0x80)) return fail(now);
        awake_ = true;
        awake_address_ = address_;
        reset_deadline_ms_ = now + 500U;
        due_ms_ = now + 100U;
        state_ = State::WaitReset;
        return false;
      case State::WaitReset: {
        uint8_t power = 0;
        if (!readRegister(0x6B, &power, 1)) return fail(now);
        if (power & 0x80) {
          if (int32_t(now - reset_deadline_ms_) >= 0) {
            sample_.error_register = 0x6B;
            sample_.error_value = power;
            return fail(now);
          }
          due_ms_ = now + 10U;
          return false;
        }
        state_ = State::SignalReset;
        return false;
      }
      case State::SignalReset:
        if (!writeRegister(0x68, 0x07)) return fail(now);
        due_ms_ = now + 100U;
        state_ = State::Wake;
        return false;
      case State::Wake:
        if (!writeRegister(0x6B, 0x01)) return fail(now);
        awake_ = true;
        awake_address_ = address_;
        due_ms_ = now + 100U; // PLL/gyro startup without stalling shared bus.
        config_index_ = 0;
        state_ = State::Configure;
        return false;
      case State::Configure: {
        // Enable all axes, disable FIFO/interrupt/aux-master, 44 Hz DLPF,
        // 1 kHz / (19+1) = 50 Hz; +/-250 deg/s and +/-2g.
        if (!writeRegister(configReg(config_index_), configValue(config_index_))) return fail(now);
        state_ = State::Verify;
        return false;
      }
      case State::Verify: {
        // An I2C ACK alone does not prove that the sensor left sleep or that
        // its full-scale settings match the acceleration/gyro conversion.
        uint8_t value = 0;
        const uint8_t reg = configReg(config_index_);
        if (!readRegister(reg, &value, 1)) return fail(now);
        if (reg == 0x6B) sample_.power_mgmt_1 = value;
        if (reg == 0x6C) sample_.power_mgmt_2 = value;
        if (reg == 0x1B) sample_.gyro_config = value;
        if (reg == 0x1C) sample_.accel_config = value;
        if (value != configValue(config_index_)) {
          sample_.error_register = reg;
          sample_.error_value = value;
          return fail(now);
        }
        if (++config_index_ == ConfigCount) {
          filter_.reset();
          sample_.configured = true;
          state_ = State::Read;
          due_ms_ = now + 40U;
          return true;
        }
        state_ = State::Configure;
        return false;
      }
      case State::Read: {
        uint8_t bytes[14];
        if (!readRegister(0x3B, bytes, sizeof(bytes))) return fail(now);
        due_ms_ = now + 40U; // 25 Hz bus polling; UI refreshes at 20 Hz.
        (void)filter_.update(bytes, now, sample_);
        return true;
      }
    }
    return false;
  }

 private:
  enum class State : uint8_t {
    ProbeLow, ProbeHigh, DeviceReset, WaitReset, SignalReset, Wake, Configure, Verify, Read
  };
  static constexpr uint8_t ConfigCount = 9;
  static uint8_t configReg(uint8_t index) {
    static const uint8_t regs[] = {0x6C, 0x23, 0x38, 0x6A, 0x1A, 0x19, 0x1B, 0x1C, 0x6B};
    return regs[index];
  }
  static uint8_t configValue(uint8_t index) {
    static const uint8_t values[] = {0x00, 0x00, 0x00, 0x00, 0x03, 19, 0x00, 0x00, 0x01};
    return values[index];
  }
  bool readRegister(uint8_t reg, uint8_t *buffer, size_t length) {
    bus_.beginTransmission(address_);
    bus_.write(reg);
    if (bus_.endTransmission(false) != 0 ||
        bus_.requestFrom(address_, length, true) != length) {
      sample_.error_register = reg;
      sample_.error_value = 0xFF;
      return false;
    }
    for (size_t i = 0; i < length; ++i) buffer[i] = uint8_t(bus_.read());
    return true;
  }
  bool writeRegister(uint8_t reg, uint8_t value) {
    bus_.beginTransmission(address_);
    bus_.write(reg);
    bus_.write(value);
    if (bus_.endTransmission(true) == 0) return true;
    sample_.error_register = reg;
    sample_.error_value = 0xFF;
    return false;
  }
  bool fail(uint32_t now) {
    sample_.available = sample_.stable = false;
    sample_.configured = false;
    sample_.address = sample_.who_am_i = 0;
    state_ = State::ProbeLow;
    due_ms_ = now + 2000U;
    filter_.reset();
    return true;
  }
  Bus &bus_;
  LevelFilter filter_;
  LevelSample sample_;
  State state_ = State::ProbeLow;
  bool enabled_ = false, awake_ = false;
  uint8_t address_ = 0x68, awake_address_ = 0x68, config_index_ = 0;
  uint32_t due_ms_ = 0, reset_deadline_ms_ = 0;
};

} // namespace exlink
