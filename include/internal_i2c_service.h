#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "CST9220.h"
#include "INA.h"
#include "aht20_sensor.h"
#include "mcp4017_control.h"
#include "mpu6050_level.h"

namespace exlink {

enum class IoRequestKind : uint8_t {
  PowerWrite, PowerAdjust, PowerRead, Measurement
};

struct IoResult {
  IoRequestKind kind = IoRequestKind::PowerRead;
  uint32_t id = 0;
  bool cancelled = false;
  Mcp4017Result power;
  uint64_t millivolts = 0;
  uint64_t microamps = 0;
  uint64_t microwatts = 0;
};

struct TouchSample {
  bool down = false;
  bool released = false;
  bool cancelled = false; // Missing reports end a contact without a click.
  bool irq = false; // A new hardware IRQ preceded this read, not a timer poll.
  uint16_t x = 0;
  uint16_t y = 0;
  uint32_t sampled_ms = 0;
  uint32_t sequence = 0;
};

struct IoDiagnostics {
  uint32_t touch_reads = 0;
  uint32_t touch_errors = 0;
  uint32_t touch_recoveries = 0;
  uint32_t last_touch_ms = 0;
  uint32_t max_transaction_ms = 0;
  bool touch_ready = false;
  bool ina_ready = false;
  uint8_t ina_device = UINT8_MAX;
};

// After begin(), the worker exclusively owns the internal Wire bus and its
// peripherals. Application/UI code exchanges bounded queues and never waits
// for an ordinary I2C transaction. Control methods belong to the app task.
class InternalI2cService {
 public:
  InternalI2cService(TwoWire &wire, CST9220 &touch, INA_Class &ina,
                     Mcp4017Controller<TwoWire> &power);

  // Call after all startup Wire/CST/INA/MCP initialization has finished.
  bool begin(bool touchEnabled, uint8_t inaDevice);
  bool submit(IoRequestKind kind, int value, uint32_t id);
  bool takeResult(IoResult &result);
  bool takeTouch(TouchSample &sample);
  bool touchPending() const;
  IoDiagnostics diagnostics() const;

  // These methods only change requests or copy caches; I2C remains confined
  // to the worker. A page transition immediately invalidates its old cache.
  void setLevelActive(bool active);
  LevelSample levelSample() const;
  bool zeroLevel();
  void setEnvironmentActive(bool active);
  Aht20Sample environmentSample() const;

  // A timeout leaves a pause requested. Call resume() when abandoning sleep
  // or OTA. Never suspend the worker while it might hold a driver lock.
  bool pause(bool powerDownIna, uint32_t timeout_ms = 1800);
  void resume();
  // GPIO light-sleep wake changes the interrupt type on the touch IRQ pin.
  // Disable the app ISR throughout the sleep loop, then restore its edge.
  bool suspendTouchInterruptForSleep();
  void restoreTouchInterruptAfterSleep();

 private:
  struct Request {
    IoRequestKind kind;
    int value;
    uint32_t id;
  };
  struct TouchEnvelope {
    TouchSample sample;
    uint32_t generation;
  };
  struct Control {
    bool pause;
    bool power_down;
    uint32_t generation;
    bool level_active;
    bool environment_active;
    uint32_t level_generation;
    uint32_t environment_generation;
  };

  static constexpr unsigned RequestCapacity = 8;
  static constexpr unsigned ResultCapacity = 16;
  static constexpr unsigned TouchCapacity = 16;
  static constexpr uint32_t NotifyIrq = 1U;
  static constexpr uint32_t NotifyControl = 2U;
  static void taskEntry(void *context);
  static void IRAM_ATTR irqEntry(void *context);
  void run();
  Control control() const;
  void recordDuration(uint32_t started);
  void finishRequest(const Request &request, bool cancelled);
  void cancelQueuedRequests();
  void publishTouch(TouchSample sample, uint32_t generation);
  void publishLevel(const LevelSample &sample, const Control &state);
  void publishEnvironment(const Aht20Sample &sample, const Control &state);
  void serviceSensors(const Control &state);
  bool serviceInaRecovery(bool contact_active);
  // Stops at most one sensor per call. True means a stop step was performed;
  // pause acknowledgement must wait for a later call returning false.
  bool quiesceSensors();
  void clearQueues();

  TwoWire &wire_;
  CST9220 &touch_;
  INA_Class &ina_;
  Mcp4017Controller<TwoWire> &power_;
  Mpu6050Level<TwoWire> level_;
  Aht20Sensor<TwoWire> environment_;
  QueueHandle_t requests_ = nullptr;
  QueueHandle_t results_ = nullptr;
  QueueHandle_t touches_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool touch_enabled_ = false;
  uint8_t ina_device_ = UINT8_MAX;
  int irq_pin_ = -1;
  mutable portMUX_TYPE state_mux_ = portMUX_INITIALIZER_UNLOCKED;
  bool started_ = false;
  bool pause_requested_ = false;
  bool pause_power_down_ = false;
  bool pause_acknowledged_ = false;
  uint32_t generation_ = 0;
  bool level_active_ = false, environment_active_ = false;
  uint32_t level_generation_ = 0, environment_generation_ = 0;
  LevelSample level_sample_;
  Aht20Sample environment_sample_;
  float level_zero_roll_ = 0, level_zero_pitch_ = 0;
  // The following scheduler/driver fields belong exclusively to the worker.
  bool level_worker_enabled_ = false, environment_worker_enabled_ = false;
  uint32_t level_worker_generation_ = 0, environment_worker_generation_ = 0;
  bool environment_turn_ = false;
  uint32_t ina_retry_at_ms_ = 0;
  uint8_t ina_probe_address_ = 0x40, ina_setup_step_ = 0;
  uint8_t ina_candidate_ = UINT8_MAX;
  unsigned outstanding_requests_ = 0;
  IoDiagnostics diagnostics_;
};

} // namespace exlink
