#include "internal_i2c_service.h"

namespace exlink {

InternalI2cService::InternalI2cService(TwoWire &wire, CST9220 &touch,
                                     INA_Class &ina,
                                     Mcp4017Controller<TwoWire> &power)
    : wire_(wire), touch_(touch), ina_(ina), power_(power), level_(wire),
      environment_(wire) {}

bool InternalI2cService::begin(bool touchEnabled, uint8_t inaDevice) {
  if (task_ != nullptr) return true;
  touch_enabled_ = touchEnabled;
  ina_device_ = inaDevice;
  irq_pin_ = touchEnabled ? touch_.interruptPin() : -1;
  diagnostics_ = IoDiagnostics{};
  diagnostics_.touch_ready = touchEnabled && touch_.isReady();
  pause_requested_ = pause_power_down_ = pause_acknowledged_ = false;
  outstanding_requests_ = 0;
  generation_ = 0;
  requests_ = xQueueCreate(RequestCapacity, sizeof(Request));
  results_ = xQueueCreate(ResultCapacity, sizeof(IoResult));
  touches_ = xQueueCreate(TouchCapacity, sizeof(TouchEnvelope));
  if (!requests_ || !results_ || !touches_) {
    clearQueues();
    return false;
  }
  // This limits the requested transaction wait. The underlying IDF driver can
  // still spend longer recovering; only the worker can enter that path.
  wire_.setTimeOut(20);
  if (xTaskCreatePinnedToCore(taskEntry, "internal_i2c", 4096, this, 1,
                            &task_, 0) != pdPASS) {
    task_ = nullptr;
    clearQueues();
    return false;
  }
  portENTER_CRITICAL(&state_mux_);
  started_ = true;
  portEXIT_CRITICAL(&state_mux_);
  if (irq_pin_ >= 0) {
    attachInterruptArg(digitalPinToInterrupt(irq_pin_), irqEntry, this, FALLING);
  }
  return true;
}

void InternalI2cService::clearQueues() {
  if (requests_) vQueueDelete(requests_);
  if (results_) vQueueDelete(results_);
  if (touches_) vQueueDelete(touches_);
  requests_ = results_ = touches_ = nullptr;
}

bool InternalI2cService::submit(IoRequestKind kind, int value, uint32_t id) {
  bool permitted = false;
  portENTER_CRITICAL(&state_mux_);
  // Credits include both queued and completed-but-unconsumed requests. Thus
  // every accepted request, including a pause cancellation, has result space.
  if (started_ && !pause_requested_ &&
      outstanding_requests_ < RequestCapacity) {
    ++outstanding_requests_;
    permitted = true;
  }
  portEXIT_CRITICAL(&state_mux_);
  if (!permitted) return false;
  const Request request{kind, value, id};
  if (xQueueSend(requests_, &request, 0) != pdTRUE) {
    portENTER_CRITICAL(&state_mux_);
    --outstanding_requests_;
    portEXIT_CRITICAL(&state_mux_);
    return false;
  }
  xTaskNotify(task_, NotifyControl, eSetBits);
  return true;
}

bool InternalI2cService::takeResult(IoResult &result) {
  if (!results_ || xQueueReceive(results_, &result, 0) != pdTRUE) return false;
  portENTER_CRITICAL(&state_mux_);
  if (outstanding_requests_) --outstanding_requests_;
  portEXIT_CRITICAL(&state_mux_);
  return true;
}

bool InternalI2cService::takeTouch(TouchSample &sample) {
  if (!touches_) return false;
  TouchEnvelope envelope;
  while (xQueueReceive(touches_, &envelope, 0) == pdTRUE) {
    const Control state = control();
    if (!state.pause && envelope.generation == state.generation) {
      sample = envelope.sample;
      return true;
    }
  }
  return false;
}

bool InternalI2cService::touchPending() const {
  return touches_ && uxQueueMessagesWaiting(touches_) != 0;
}

IoDiagnostics InternalI2cService::diagnostics() const {
  portENTER_CRITICAL(&state_mux_);
  const IoDiagnostics result = diagnostics_;
  portEXIT_CRITICAL(&state_mux_);
  return result;
}

void InternalI2cService::setLevelActive(bool active) {
  portENTER_CRITICAL(&state_mux_);
  if (level_active_ != active) {
    level_active_ = active;
    ++level_generation_;
    level_sample_ = LevelSample{};
  }
  portEXIT_CRITICAL(&state_mux_);
  if (task_) xTaskNotify(task_, NotifyControl, eSetBits);
}

LevelSample InternalI2cService::levelSample() const {
  portENTER_CRITICAL(&state_mux_);
  LevelSample result = level_sample_;
  const float zero_roll = level_zero_roll_, zero_pitch = level_zero_pitch_;
  portEXIT_CRITICAL(&state_mux_);
  result.roll_deg = levelAngleDifference(result.roll_deg, zero_roll);
  result.pitch_deg -= zero_pitch;
  return result;
}

bool InternalI2cService::zeroLevel() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&state_mux_);
  const bool valid = started_ && !pause_requested_ && level_active_ &&
      level_sample_.configured && level_sample_.sample_sequence != 0 &&
      level_sample_.available && level_sample_.stable &&
      uint32_t(now - level_sample_.sampled_ms) <= 250U;
  if (valid) {
    level_zero_roll_ = level_sample_.roll_deg;
    level_zero_pitch_ = level_sample_.pitch_deg;
  }
  portEXIT_CRITICAL(&state_mux_);
  return valid;
}

void InternalI2cService::setEnvironmentActive(bool active) {
  portENTER_CRITICAL(&state_mux_);
  if (environment_active_ != active) {
    environment_active_ = active;
    ++environment_generation_;
    environment_sample_ = Aht20Sample{};
  }
  portEXIT_CRITICAL(&state_mux_);
  if (task_) xTaskNotify(task_, NotifyControl, eSetBits);
}

Aht20Sample InternalI2cService::environmentSample() const {
  portENTER_CRITICAL(&state_mux_);
  const Aht20Sample result = environment_sample_;
  portEXIT_CRITICAL(&state_mux_);
  return result;
}

InternalI2cService::Control InternalI2cService::control() const {
  portENTER_CRITICAL(&state_mux_);
  const Control result{pause_requested_, pause_power_down_, generation_,
                       level_active_, environment_active_, level_generation_,
                       environment_generation_};
  portEXIT_CRITICAL(&state_mux_);
  return result;
}

bool InternalI2cService::pause(bool powerDownIna, uint32_t timeout_ms) {
  portENTER_CRITICAL(&state_mux_);
  if (!started_) {
    portEXIT_CRITICAL(&state_mux_);
    return false;
  }
  if (pause_requested_ && pause_power_down_ == powerDownIna &&
      pause_acknowledged_) {
    portEXIT_CRITICAL(&state_mux_);
    return true;
  }
  pause_requested_ = true;
  pause_power_down_ = powerDownIna;
  pause_acknowledged_ = false;
  ++generation_;
  ++level_generation_;
  ++environment_generation_;
  level_sample_ = LevelSample{};
  environment_sample_ = Aht20Sample{};
  portEXIT_CRITICAL(&state_mux_);
  xQueueReset(touches_);
  xTaskNotify(task_, NotifyControl, eSetBits);
  const uint32_t started = millis();
  do {
    portENTER_CRITICAL(&state_mux_);
    const bool acknowledged = pause_acknowledged_;
    portEXIT_CRITICAL(&state_mux_);
    if (acknowledged) return true;
    if (uint32_t(millis() - started) >= timeout_ms) break;
    vTaskDelay(1);
  } while (true);
  return false;
}

void InternalI2cService::resume() {
  if (!touches_) return;
  // Clear while the worker is still paused, then advance the generation. An
  // old in-flight completion is also rejected by takeTouch's generation test.
  xQueueReset(touches_);
  portENTER_CRITICAL(&state_mux_);
  pause_requested_ = false;
  pause_power_down_ = false;
  pause_acknowledged_ = false;
  ++generation_;
  ++level_generation_;
  ++environment_generation_;
  level_sample_ = LevelSample{};
  environment_sample_ = Aht20Sample{};
  portEXIT_CRITICAL(&state_mux_);
  if (task_) xTaskNotify(task_, NotifyControl, eSetBits);
}

void InternalI2cService::taskEntry(void *context) {
  static_cast<InternalI2cService *>(context)->run();
}

void IRAM_ATTR InternalI2cService::irqEntry(void *context) {
  auto *self = static_cast<InternalI2cService *>(context);
  BaseType_t woken = pdFALSE;
  xTaskNotifyFromISR(self->task_, NotifyIrq, eSetBits, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void InternalI2cService::recordDuration(uint32_t started) {
  const uint32_t duration = millis() - started;
  portENTER_CRITICAL(&state_mux_);
  if (duration > diagnostics_.max_transaction_ms)
    diagnostics_.max_transaction_ms = duration;
  portEXIT_CRITICAL(&state_mux_);
}

void InternalI2cService::finishRequest(const Request &request, bool cancelled) {
  IoResult result;
  result.kind = request.kind;
  result.id = request.id;
  result.cancelled = cancelled;
  if (!cancelled) {
    const uint32_t started = millis();
    switch (request.kind) {
      case IoRequestKind::PowerWrite:
        result.power = power_.write(request.value);
        break;
      case IoRequestKind::PowerAdjust:
        result.power = power_.adjust(request.value);
        break;
      case IoRequestKind::PowerRead:
        result.power = power_.read();
        break;
      case IoRequestKind::Measurement:
        if (ina_device_ == UINT8_MAX) {
          result.cancelled = true;
        } else {
          result.millivolts = ina_.getBusMilliVolts(ina_device_);
          result.microamps = static_cast<uint64_t>(ina_.getBusMicroAmps(ina_device_));
          result.microwatts = static_cast<uint64_t>(ina_.getBusMicroWatts(ina_device_));
        }
        break;
    }
    recordDuration(started);
  }
  // ResultCapacity exceeds the maximum outstanding request credits, including
  // the operation currently running. This send cannot wait behind the UI.
  (void)xQueueSend(results_, &result, 0);
}

void InternalI2cService::cancelQueuedRequests() {
  Request request;
  while (xQueueReceive(requests_, &request, 0) == pdTRUE)
    finishRequest(request, true);
}

void InternalI2cService::publishTouch(TouchSample sample, uint32_t generation) {
  const Control state = control();
  if (state.pause || state.generation != generation) return;
  const TouchEnvelope envelope{sample, generation};
  if (xQueueSend(touches_, &envelope, 0) != pdTRUE) {
    // Bounded overflow: retain recent complete samples, including the newest
    // release, rather than silently leaving an old press at the queue tail.
    TouchEnvelope discarded;
    (void)xQueueReceive(touches_, &discarded, 0);
    (void)xQueueSend(touches_, &envelope, 0);
  }
}

void InternalI2cService::publishLevel(const LevelSample &sample,
                                     const Control &state) {
  portENTER_CRITICAL(&state_mux_);
  if (!pause_requested_ && generation_ == state.generation && level_active_ &&
      level_generation_ == state.level_generation)
    level_sample_ = sample;
  portEXIT_CRITICAL(&state_mux_);
}

void InternalI2cService::publishEnvironment(const Aht20Sample &sample,
                                           const Control &state) {
  portENTER_CRITICAL(&state_mux_);
  if (!pause_requested_ && generation_ == state.generation &&
      environment_active_ &&
      environment_generation_ == state.environment_generation)
    environment_sample_ = sample;
  portEXIT_CRITICAL(&state_mux_);
}

bool InternalI2cService::quiesceSensors() {
  const uint32_t started = millis();
  if (level_worker_enabled_) {
    (void)level_.step(false, started);
    level_worker_enabled_ = false;
    recordDuration(started);
    return true;
  }
  if (environment_worker_enabled_) {
    (void)environment_.step(false, started);
    environment_worker_enabled_ = false;
    recordDuration(started);
    return true;
  }
  return false;
}

void InternalI2cService::serviceSensors(const Control &state) {
  if (state.pause) return;
  const uint32_t started = millis();
  // Finish an old page generation before starting another. Each loop performs
  // only one sensor step, including the best-effort MPU sleep transaction.
  if (level_worker_enabled_ && (!state.level_active ||
      level_worker_generation_ != state.level_generation)) {
    (void)level_.step(false, started);
    level_worker_enabled_ = false;
    recordDuration(started);
    return;
  }
  if (environment_worker_enabled_ && (!state.environment_active ||
      environment_worker_generation_ != state.environment_generation)) {
    (void)environment_.step(false, started);
    environment_worker_enabled_ = false;
    recordDuration(started);
    return;
  }
  if (!state.level_active && !state.environment_active) return;
  const bool environment_turn = state.environment_active &&
      (!state.level_active || environment_turn_);
  environment_turn_ = !environment_turn;
  if (environment_turn) {
    environment_worker_enabled_ = true;
    environment_worker_generation_ = state.environment_generation;
    if (environment_.step(true, started))
      publishEnvironment(environment_.sample(), state);
  } else {
    level_worker_enabled_ = true;
    level_worker_generation_ = state.level_generation;
    if (level_.step(true, started)) publishLevel(level_.sample(), state);
  }
  recordDuration(started);
}

void InternalI2cService::run() {
  bool was_paused = false;
  bool ina_sleeping = false;
  bool touch_down = false;
  bool irq_pending = false;
  bool recovered_once = false;
  uint32_t observed_generation = UINT32_MAX;
  uint32_t last_read_ms = millis() - 1000U;
  uint32_t last_contact_ms = 0;
  uint32_t last_recovery_ms = 0;
  uint32_t retry_at_ms = 0;
  uint32_t sequence = 0;
  uint8_t failures = 0;
  uint16_t last_x = 0, last_y = 0;
  for (;;) {
    uint32_t notifications = 0;
    (void)xTaskNotifyWait(0, UINT32_MAX, &notifications,
                         pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
    if (notifications & NotifyIrq) irq_pending = true;
    const Control state = control();
    if (state.pause) {
      cancelQueuedRequests();
      // Do not acknowledge while a sensor still needs its stop/sleep step.
      // Keep sensor stops separate from each other and from INA transactions.
      if (quiesceSensors()) continue;
      if (ina_device_ != UINT8_MAX && state.power_down != ina_sleeping) {
        const uint32_t started = millis();
        ina_.setMode(state.power_down ? INA_MODE_POWER_DOWN :
                     INA_MODE_CONTINUOUS_BOTH, ina_device_);
        ina_sleeping = state.power_down;
        recordDuration(started);
      }
      if (!was_paused) xQueueReset(touches_);
      was_paused = true;
      touch_down = false;
      irq_pending = false;
      portENTER_CRITICAL(&state_mux_);
      if (pause_requested_ && generation_ == state.generation &&
          pause_power_down_ == state.power_down)
        pause_acknowledged_ = true;
      portEXIT_CRITICAL(&state_mux_);
      continue;
    }
    if (was_paused || observed_generation != state.generation) {
      if (ina_sleeping && ina_device_ != UINT8_MAX) {
        const uint32_t started = millis();
        ina_.setMode(INA_MODE_CONTINUOUS_BOTH, ina_device_);
        ina_sleeping = false;
        recordDuration(started);
      }
      // Force a fresh sample even when a wake pulse has already returned HIGH.
      last_read_ms = millis() - 1000U;
      retry_at_ms = millis();
      irq_pending = true;
      touch_down = false;
      failures = 0;
      observed_generation = state.generation;
      was_paused = false;
    }
    if (control().pause) continue;
    const uint32_t now = millis();
    const bool irq_low = irq_pin_ >= 0 && digitalRead(irq_pin_) == LOW;
    const uint32_t fallback = touch_down ? 32U : 1000U;
    // Preserve the established touch timing/backoff and report handling. Due
    // touch reads precede both queued controls and optional sensor polling.
    const bool touch_due = touch_enabled_ &&
        int32_t(now - retry_at_ms) >= 0 &&
        uint32_t(now - last_read_ms) >= 16U &&
        (irq_pending || irq_low || uint32_t(now - last_read_ms) >= fallback);
    if (touch_due) {
      irq_pending = false;
      last_read_ms = now;
      uint16_t x = 0, y = 0;
      const uint32_t read_generation = control().generation;
      const bool down = touch_.readTouch(&x, &y);
      const bool communication_ok = touch_.communicationOk();
      const bool released = touch_.releaseReported();
      const uint32_t sampled = millis();
      recordDuration(now);
      portENTER_CRITICAL(&state_mux_);
      ++diagnostics_.touch_reads;
      diagnostics_.touch_ready = communication_ok && touch_.isReady();
      if (communication_ok) diagnostics_.last_touch_ms = sampled;
      else ++diagnostics_.touch_errors;
      portEXIT_CRITICAL(&state_mux_);
      if (!communication_ok) {
        if (failures < 255) ++failures;
        const uint8_t shift = failures > 5 ? 4 : failures - 1;
        retry_at_ms = sampled + (100U << shift);
        if (failures >= 3 && !control().pause &&
            (!recovered_once || uint32_t(sampled - last_recovery_ms) >= 5000U)) {
          last_recovery_ms = sampled;
          recovered_once = true;
          const uint32_t recovery_started = millis();
          const bool recovered = touch_.recover();
          recordDuration(recovery_started);
          portENTER_CRITICAL(&state_mux_);
          diagnostics_.touch_ready = recovered && touch_.isReady();
          if (recovered) ++diagnostics_.touch_recoveries;
          portEXIT_CRITICAL(&state_mux_);
        }
        continue;
      }
      failures = 0;
      retry_at_ms = sampled;
      TouchSample sample;
      sample.down = down;
      sample.released = released;
      sample.x = down ? x : last_x;
      sample.y = down ? y : last_y;
      sample.sampled_ms = sampled;
      sample.sequence = ++sequence;
      if (down) {
        touch_down = true;
        last_contact_ms = sampled;
        last_x = x;
        last_y = y;
      } else if (released) {
        touch_down = false;
      } else if (touch_down && uint32_t(sampled - last_contact_ms) >= 75U) {
        sample.cancelled = true;
        touch_down = false;
      }
      publishTouch(sample, read_generation);
    }
    if (control().pause) continue;
    Request request;
    if (xQueueReceive(requests_, &request, 0) == pdTRUE) {
      finishRequest(request, control().pause);
      // A control operation may contain several I2C transfers. Give the next
      // iteration a fresh touch deadline before starting another device.
      continue;
    }
    serviceSensors(control());
  }
}

} // namespace exlink
