#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "touch_contact_policy.h"

#define APP_LOGI(...) ((void)0)
#define APP_LOGW(...) ((void)0)
#define EXLINK_TOUCH_ENABLED 1
enum { LOW, HIGH, OUTPUT, INPUT, LCD_CS = 15, LCD_VCI_EN = 41,
       LCD_RST = 10, LCD_TE = 45, INT_N_PIN = 17,
       RIGHT_PIN = 38, LEFT_PIN = 39, PUSH_PIN = 40,
       GPIO_INTR_LOW_LEVEL, GPIO_INTR_NEGEDGE, ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_TIMER,
       CO5300_C_DISPOFF, CO5300_C_SLPIN, CO5300_C_SLPOUT, CO5300_C_DISPON,
       INA_MODE_CONTINUOUS_BOTH };
using gpio_num_t = int;
using esp_err_t = int;
constexpr int ESP_OK = 0;
enum class BleSessionMode { Off };
struct SleepEvent { int error, cause; uint32_t duration; bool key; };
struct PinEvent { int pin, value; };
static uint32_t now;
static bool key_down, touch_contact_down, touch_input_enabled = true;
static bool suppress_touch_until_release, suppress_keys_until_release;
static exlink::WakeTouchGate wake_touch_gate;
static bool display_idle_active, display_resume_pending, touch_contact_cancelled;
static bool pwm_ledc_ready = true, servo_ledc_ready = true;
static bool deviceBleRadioRunning, ble_can_stop = true, runtime_busy;
static bool timer_enabled, fail_wake_config;
static bool irq_registered = true, irq_enabled, irq_sleep_window, fail_irq_disable;
static int irq_type;
static uint32_t idle_touch_poll_ms, touch_consumed_sequence, last_user_activity_ms;
static BleSessionMode requestedBleMode;
static int BluetoothSerial_flag, tim1, output_level;
static unsigned refresh_count, sleep_calls, cleanup_calls;
static unsigned pause_calls, pause_failure_call, resume_calls;
static uint32_t pwm_values[4];
static std::vector<SleepEvent> sleep_events;
static std::vector<int> panel_commands;
static std::vector<PinEvent> writes;

static uint32_t millis() { return now; }
static void delay(uint32_t ms) { now += ms; }
static int digitalRead(int pin) { return pin == 1 ? output_level : (key_down ? LOW : HIGH); }
static void digitalWrite(int pin, int value) {
  writes.push_back({pin, value});
  if (pin == 1) output_level = value;
}
static void pinMode(int, int) {}
static bool any_key_down() { return key_down; }
static void notify_user_activity() {
  last_user_activity_ms = millis();
  if (display_idle_active) display_resume_pending = true;
}
static void poll_idle_inputs(bool = false) {
  if (key_down) { suppress_keys_until_release = true; notify_user_activity(); }
}
static bool runtime_prevents_sleep() { return runtime_busy; }
static uint32_t ledcRead(unsigned channel) { return pwm_values[channel]; }
static void ledcWrite(unsigned channel, uint32_t value) { pwm_values[channel] = value; }
static bool buzzer_feedback_active;
static void clear_touch_contact() {
  touch_contact_down = false;
  suppress_touch_until_release = true;
  wake_touch_gate.block();
}
static void device_ble_task() { if (ble_can_stop) deviceBleRadioRunning = false; }
struct Bus {
  void sendCommand(int value) { panel_commands.push_back(value); }
} bus;
static Bus *co5300_bus = &bus;
struct Io {
  bool pause(bool) {
    if (irq_sleep_window) assert(!irq_enabled);
    return ++pause_calls != pause_failure_call;
  }
  void resume() {
    if (irq_sleep_window) assert(!irq_enabled);
    ++resume_calls;
  }
  bool touchPending() { return false; }
  bool suspendTouchInterruptForSleep() {
    if (!irq_registered || fail_irq_disable) return false;
    irq_enabled = false;
    irq_sleep_window = true;
    return true;
  }
  void restoreTouchInterruptAfterSleep() {
    assert(irq_sleep_window && !irq_enabled);
    irq_type = GPIO_INTR_NEGEDGE;
    irq_enabled = true;
    irq_sleep_window = false;
  }
} internal_io;
static void timerAlarmDisable(int) { timer_enabled = false; }
static void timerAlarmEnable(int) { timer_enabled = true; }
static int gpio_wakeup_enable(int pin, int type) {
  if (pin == INT_N_PIN) {
    assert(!irq_enabled);
    irq_type = type; // ESP-IDF GPIO wake setup changes the ordinary IRQ type.
  }
  return fail_wake_config ? 1 : ESP_OK;
}
static int gpio_wakeup_disable(int) { ++cleanup_calls; return ESP_OK; }
static int esp_sleep_enable_gpio_wakeup() { return ESP_OK; }
static int esp_sleep_enable_timer_wakeup(uint64_t value) { assert(value == 30000); return ESP_OK; }
static int esp_sleep_disable_wakeup_source(int) { ++cleanup_calls; return ESP_OK; }
static int esp_light_sleep_start() {
  assert(!irq_enabled && irq_type == GPIO_INTR_LOW_LEVEL);
  assert(sleep_calls < sleep_events.size());
  const auto &event = sleep_events[sleep_calls++];
  now += event.duration;
  key_down = event.key;
  return event.error;
}
static int esp_sleep_get_wakeup_cause() { return sleep_events[sleep_calls - 1].cause; }
static void apply_display_brightness() { panel_commands.push_back(-1); }
static int lv_scr_act() { return 0; }
static void lv_obj_invalidate(int) {}
static void lv_refr_now(void *) { ++refresh_count; }

struct Ina {
  unsigned begin_calls = 0, found_on_call = 0, configured = 0;
  bool wrong_type = false;
  uint8_t begin(int amps, int resistance) {
    assert(amps == 10 && resistance == 10000);
    return ++begin_calls >= found_on_call && found_on_call ? 1 : 0;
  }
  const char *getDeviceName(unsigned) { return wrong_type ? "INA219" : "INA226"; }
  void reset(unsigned index) { assert(index == 0); ++configured; }
  void setAveraging(int value, unsigned index) { assert(value == 4 && !index); ++configured; }
  void setBusConversion(int value, unsigned index) { assert(value == 8244 && !index); ++configured; }
  void setShuntConversion(int value, unsigned index) { assert(value == 8244 && !index); ++configured; }
  void setMode(int value, unsigned index) { assert(value == INA_MODE_CONTINUOUS_BOTH && !index); ++configured; }
} INA;

#include "production_sleep.inc"

static void reset() {
  now = 0; key_down = touch_contact_down = false;
  touch_input_enabled = true;
  display_idle_active = true; display_resume_pending = false;
  wake_touch_gate.reset();
  suppress_touch_until_release = suppress_keys_until_release = false;
  deviceBleRadioRunning = runtime_busy = fail_wake_config = false;
  ble_can_stop = true; output_level = HIGH;
  irq_registered = irq_enabled = true;
  irq_sleep_window = fail_irq_disable = false;
  irq_type = GPIO_INTR_NEGEDGE;
  refresh_count = sleep_calls = cleanup_calls = pause_calls = pause_failure_call = resume_calls = 0;
  timer_enabled = true;
  panel_commands.clear(); writes.clear(); sleep_events.clear();
  pwm_values[2] = 12; pwm_values[3] = 34;
  INA = Ina{};
}
static void restored() {
  assert(!display_idle_active && !display_resume_pending);
  assert(timer_enabled && refresh_count == 1);
  assert(irq_enabled == irq_registered && !irq_sleep_window);
  if (irq_registered) assert(irq_type == GPIO_INTR_NEGEDGE);
  assert(output_level == HIGH && pwm_values[2] == 12 && pwm_values[3] == 34);
}

int main() {
  reset();
  co5300_power_sequence();
  const auto select = std::find_if(writes.begin(), writes.end(), [](PinEvent e) {
    return e.pin == LCD_CS && e.value == HIGH;
  });
  const auto enable = std::find_if(writes.begin(), writes.end(), [](PinEvent e) {
    return e.pin == LCD_VCI_EN && e.value == HIGH;
  });
  assert(select != writes.end() && select < enable);
  assert(now == 172);
  puts("PASS panel CS inactive before VCI enable, reset timing preserved");

  reset();
  sleep_events.push_back({ESP_OK, ESP_SLEEP_WAKEUP_GPIO, 100000, false});
  enter_display_sleep(); restored();
  assert(sleep_calls == 1 && cleanup_calls == 6);
  assert(panel_commands == std::vector<int>({CO5300_C_DISPOFF, CO5300_C_SLPIN,
           CO5300_C_SLPOUT, -1, CO5300_C_DISPON}));
  puts("PASS short GPIO wake with released key and no touch/I2C report");

  reset();
  now = UINT32_MAX - 1000U;
  for (unsigned i = 0; i < 1000; ++i)
    sleep_events.push_back({ESP_OK, ESP_SLEEP_WAKEUP_TIMER, 3600000, false});
  sleep_events.push_back({ESP_OK, ESP_SLEEP_WAKEUP_GPIO, 30, true});
  enter_display_sleep(); restored();
  assert(sleep_calls == 1001 && suppress_keys_until_release);
  puts("PASS prolonged timer-only sleep stays dark, GPIO wakes across millis wrap");

  reset();
  sleep_events.push_back({1, ESP_SLEEP_WAKEUP_GPIO, 0, false});
  enter_display_sleep(); restored();
  assert(sleep_calls == 1);
  puts("PASS rejected sleep restores UI without an unbounded dark retry loop");

  reset();
  sleep_events.push_back({ESP_OK, ESP_SLEEP_WAKEUP_TIMER, 30, false});
  pause_failure_call = 2;
  enter_display_sleep(); restored();
  assert(sleep_calls == 1);
  puts("PASS wake-time I2C pause timeout restores UI");

  reset(); pause_failure_call = 1;
  enter_display_sleep(); restored();
  assert(!sleep_calls && panel_commands.empty());
  reset(); deviceBleRadioRunning = true; ble_can_stop = false;
  enter_display_sleep(); restored();
  assert(!sleep_calls && panel_commands.empty());
  reset(); fail_wake_config = true;
  enter_display_sleep(); restored();
  assert(!sleep_calls && cleanup_calls == 6);
  puts("PASS I2C/BLE/wake-configuration failures restore outputs and UI");

  reset(); fail_irq_disable = true;
  enter_display_sleep(); restored();
  assert(!sleep_calls);
  puts("PASS GPIO wake never changes live touch ISR type when IRQ suspension fails");

  reset(); irq_registered = irq_enabled = false;
  enter_display_sleep(); restored();
  assert(!sleep_calls);
  puts("PASS absent worker IRQ is not enabled by sleep cleanup");

  reset(); runtime_busy = true;
  enter_display_sleep();
  assert(!sleep_calls && !pause_calls && !display_idle_active);
  puts("PASS running hardware function prevents sleep");

  reset();
  assert(initialize_ina_at_boot() == UINT8_MAX);
  assert(INA.begin_calls == 3 && !INA.configured && now == 200);
  reset(); INA.found_on_call = 2;
  assert(initialize_ina_at_boot() == 0);
  assert(INA.begin_calls == 2 && INA.configured == 5 && now == 100);
  reset(); INA.found_on_call = 1; INA.wrong_type = true;
  assert(initialize_ina_at_boot() == UINT8_MAX);
  assert(INA.begin_calls == 1 && !INA.configured && now == 0);
  puts("PASS missing/wrong INA is bounded, transient discovery configures INA226");
}
