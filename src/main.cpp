/*
 * Exlink Tool
 * 原作者：expert电子实验室
 * 二次开发与适配：一只羊蒸鱼
 * 保留原项目及第三方代码各自的许可证和版权声明。
 */
#include <Arduino.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include "display_geometry.h"
#include <lvgl.h>
#include <Wire.h>
#include "CST9220.h"
#include "interaction_policy.h"
#include "emote_assets.h"
#include "app_log.h"
#include "ble_update_service.h"
#include "ble_session_policy.h"
#include "bridge_text_format.h"
#include "mcp4017_control.h"
#include "internal_i2c_service.h"
#include "touch_lvgl_cancel.h"
#include "level_page.h"
#include "environment_page.h"
#include "scope_capture.h"
#include "screen_asset.h"
#include "ui.h"
#include "INA.h"
#include "stdio.h"
#include "driver/ledc.h"
#include "driver/pcnt.h"
#include "driver/gpio.h"
#include "soc/pcnt_struct.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_ota_ops.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <Preferences.h>
#include <freertos/queue.h>
#include <cstring>
extern "C" const char *exlink_i2c_timeout_backport(void);
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#include "HWCDC.h"
#endif
#define I2C_SDA 18 // CST9220 TP-SDA
#define I2C_SCL 21 // CST9220 TP-SCL
#ifndef EXLINK_INTERNAL_I2C_HZ
#define EXLINK_INTERNAL_I2C_HZ 100000UL
#endif
static constexpr uint32_t INTERNAL_I2C_HZ = EXLINK_INTERNAL_I2C_HZ;
#define RST_N_PIN 16 // CST9220 TP-RST
#define INT_N_PIN 17 // CST9220 TP-INT
#ifndef EXLINK_TOUCH_ENABLED
#define EXLINK_TOUCH_ENABLED 1
#endif
#ifndef EXLINK_POWER_DIAGNOSTICS
#define EXLINK_POWER_DIAGNOSTICS 0
#endif
static constexpr bool touch_input_enabled = EXLINK_TOUCH_ENABLED != 0;
#define LCD_WIDTH EXLINK_DISPLAY_WIDTH
#define LCD_HEIGHT EXLINK_DISPLAY_HEIGHT
// Layout, animations, dirty rectangles and touch use the same native pixels.
// The display callback must never rescale an already rendered dirty area.
#define LVGL_WIDTH LCD_WIDTH
#define LVGL_HEIGHT LCD_HEIGHT
#define LCD_CS 15 // XTAL_32K_P alternate function
#define LCD_SCLK 9
#define LCD_SDIO0 8
#define LCD_SDIO1 14
#define LCD_SDIO2 47 // SPICLK_P
#define LCD_SDIO3 48 // SPICLK_N
#define LCD_RST 10
#define LCD_TE 45
#define LCD_VCI_EN 41 // MTDI
#define RIGHT_PIN 38
#define LEFT_PIN 39
#define PUSH_PIN 40
#define SERVO_REPEAT_INITIAL_DELAY_MS 250
#define SERVO_REPEAT_INTERVAL_MS 70
#define SERVO_REPEAT_MIN_INTERVAL_MS 20
#define SPI_RAW_FLUSH_IDLE_MS 20
#define SPI_INTERBYTE_DELAY_US 5
#define SERVO_REPEAT_ACCEL_WINDOW_MS 1200
#define BUZZER_FEEDBACK_DURATION_MS 100
#define BUZZER_SUPPLY_VOLTAGE 3.3f
#define BUZZER_RATED_VOLTAGE 3.0f
#define BUZZER_PWM_MAX_DUTY 255.0f
#define PCNT_UNIT PCNT_UNIT_0                                         // 使用PCNT单元0
#define LONG_PRESS_THRESHOLD 300                                      // 定义长按时间阈值
#define SHORT_PRESS_THRESHOLD 10                                      // 定义短按时间阈值
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"           // 服务UUID
#define CHARACTERISTIC_UUID_RX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // 接收特征UUID
#define CHARACTERISTIC_UUID_TX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // 发送特征UUID

volatile int pulseCount = 0;
float frequency = 0.0;
char freqencyStr[20];
int16_t count = 0;

unsigned long previousMillis = 0; // 记录上次读取计数的时间

const long interval = 125; // 设置脉冲读取间隔为125毫秒

INA_Class INA;
int8_t request_index = 0;
char voltageStr[20], currentStr[20], powerStr[20], mAHStr[20]; // 存储ina226测量数据
uint64_t vol, cur, wat;
double v, a, w;
volatile uint8_t deviceNumber = UINT8_MAX;
volatile uint64_t sumBusMillVolts = 0;
volatile int64_t sumBusMicroAmps = 0;
volatile uint8_t readings = 0;
volatile int64_t mAH = 0;

int ina266_flag = 0;
bool ina266_sample_valid = false;
int mcp4107_flag = 0;
int pwm_flag = 0;
int servo_flag = 0;
int i2cscan_flag = 0;
int spi_bridge_flag = 0;
int uart_helper_flag = 0;
int DSO_flag = 0;
int BluetoothSerial_flag = 0;
int FREcount_flag = 0;

int pwm_Freq = 0;
int pwm_Duty = 0;
int servo_angle = 90;

int new_pwm_Freq = 0;
int new_pwm_Duty = 0;
int new_servo_angle = -1;
bool buzzer_ledc_ready = false;
bool pwm_ledc_ready = false;
bool servo_ledc_ready = false;
bool buzzer_feedback_active = false;
uint8_t servo_resolution_bits = 0;
float buzzer_volume_percent = 0.5f;
unsigned long buzzer_feedback_end_ms = 0;
int power_preset_11v = 0x10;
int power_preset_5v = 0x2C;
int power_preset_3v3 = 0x56;
int power_boot_preset = 0x56;
int mcp4107_target_value = 0x56;

int mcp4107_value = -1;
static exlink::Mcp4017Controller<TwoWire> power_wiper(Wire);
static exlink::Mcp4017Result power_last_result;
static uint32_t power_request_count = 0;
long currentBaudRate = 115200; // 定义初始波特率
lv_indev_t *indev_keypad;
byte error, address;
int nDevices;
// 在这里设置屏幕尺寸
static const uint32_t screenWidth = LVGL_WIDTH;
static const uint32_t screenHeight = LVGL_HEIGHT;
// lvgl显示存储数组
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *draw_buf_pixels = nullptr;
static size_t draw_buf_pixel_count = 0;
static size_t draw_buf_size_bytes = 0;
static bool log_config_enabled = true;
// COM5 is the ESP32-S3 native USB interface (GPIO19/GPIO20), not UART0.
static app_log_channel_t log_config_channel = APP_LOG_CHANNEL_USB;
static app_log_level_t log_config_level = APP_LOG_LEVEL_INFO;
static bool ble_usb_passthrough_enabled = false;
static Arduino_DataBus *co5300_bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_GFX *tft = new Arduino_CO5300(
    co5300_bus, GFX_NOT_DEFINED, 0, false, LCD_WIDTH, LCD_HEIGHT);
static bool co5300_ready = false;
static uint8_t display_brightness_percent = 100;
static CST9220 cst9220(Wire, I2C_SDA, I2C_SCL, RST_N_PIN, INT_N_PIN);
// After setup, this task is the sole owner of the internal I2C bus. LVGL and
// the application loop only exchange requests/results with it.
static exlink::InternalI2cService internal_io(Wire, cst9220, INA, power_wiper);
static uint32_t io_request_id = 0;
static uint32_t power_pending_id = 0, power_diag_pending_id = 0, ina_pending_id = 0;
static int power_pending_action = 0;
static bool power_diag_sample_ready = false, ina_sample_ready = false;
static exlink::IoResult power_diag_sample, ina_sample;
static bool internal_io_started = false;
static bool level_page_active = false, environment_page_active = false;

// Page callbacks only request work or copy caches. The established worker
// remains the sole owner of Wire, including sensor startup and calibration.
extern "C" void level_sensor_set_active(bool active) {
  level_page_active = active;
  internal_io.setLevelActive(active);
  if (active) APP_LOGI("level", "MPU6050 page active; SDA=GPIO18 SCL=GPIO21; polling, INT not required\r\n");
}

extern "C" bool level_sensor_read(level_reading_t *reading) {
  if (!reading) return false;
  *reading = {};
  if (!internal_io_started || !level_page_active) return false;
  const exlink::LevelSample sample = internal_io.levelSample();
  reading->available = sample.available;
  reading->stable = sample.stable;
  reading->roll_deg = sample.roll_deg;
  reading->pitch_deg = sample.pitch_deg;
  reading->address = sample.address;
  reading->who_am_i = sample.who_am_i;
  reading->configured = sample.configured;
  reading->has_sample = sample.sample_sequence != 0;
  return !sample.configured || sample.sample_sequence == 0 ||
         uint32_t(millis() - sample.sampled_ms) <= 250U;
}

extern "C" bool level_sensor_zero(void) {
  return internal_io_started && internal_io.zeroLevel();
}

extern "C" void environment_sensor_set_active(bool active) {
  environment_page_active = active;
  internal_io.setEnvironmentActive(active);
  if (active) APP_LOGI("environment", "AHT20 page active; address=0x38; shared I2C worker\r\n");
}

extern "C" bool environment_sensor_read(environment_reading_t *reading) {
  if (!reading) return false;
  *reading = {};
  if (!internal_io_started || !environment_page_active) return false;
  const exlink::Aht20Sample sample = internal_io.environmentSample();
  reading->available = sample.available;
  reading->calibrated = sample.calibrated;
  reading->temperature_c = sample.temperature_c;
  reading->humidity_rh = sample.humidity_rh;
  if (sample.error) {
    reading->error = sample.error == exlink::Aht20Sensor<TwoWire>::BusError &&
        !sample.calibrated && !sample.sample_sequence ?
        ENVIRONMENT_ERROR_MISSING : ENVIRONMENT_ERROR_READ_FAILED;
  } else {
    reading->error = sample.available ? ENVIRONMENT_ERROR_NONE : ENVIRONMENT_ERROR_INITIALIZING;
  }
  return !sample.available || uint32_t(millis() - sample.sampled_ms) <= 3000U;
}
static uint32_t ui_loop_last_ms = 0, ui_loop_max_gap_ms = 0;
bool screen_sleep_enabled = true;
uint32_t screen_idle_timeout_s = 30;
uint32_t screen_photo_duration_s = 5;
static uint32_t last_user_activity_ms = 0;
static bool suppress_touch_until_release = false;
static bool suppress_keys_until_release = false;
static bool touch_contact_down = false;
static bool touch_contact_cancelled = false;
static uint16_t touch_contact_x = 0, touch_contact_y = 0;
static uint32_t touch_contact_sample_ms = 0;
static uint32_t touch_consumed_sequence = 0;
static EdgeBackGesture edge_back_gesture;
static bool display_idle_active = false;
static bool display_resume_pending = false;
static uint32_t idle_play_started_ms = 0;
static int idle_last_expression = -1;
static uint16_t idle_last_frame = UINT16_MAX;
static uint32_t idle_touch_poll_ms = 0;
static uint16_t idle_source_row[125];
static uint16_t idle_custom_source_row[LCD_WIDTH];
static bool idle_custom_active = false;
static uint16_t lcd_transfer_rows[LCD_WIDTH * 4];

static bool any_key_down()
{
  return digitalRead(RIGHT_PIN) == LOW || digitalRead(LEFT_PIN) == LOW ||
         digitalRead(PUSH_PIN) == LOW;
}

static void notify_user_activity()
{
  last_user_activity_ms = millis();
  if (display_idle_active) display_resume_pending = true;
}
hw_timer_t *tim1 = NULL;
Preferences prefs;
extern "C" void save_persistent_settings(void);

static void apply_configured_log_state(void);
static void discard_serial_input(void);
static void co5300_power_sequence(void)
{
  // The PCB exposes VCI_EN on GPIO41. Keep the panel disabled while the
  // controller and the QSPI pins settle, then apply the vendor reset timing.
  pinMode(LCD_VCI_EN, OUTPUT);
  digitalWrite(LCD_VCI_EN, LOW);
  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);
  pinMode(LCD_TE, INPUT); // TE is an optional panel output; no pull resistor.
  delay(10);

  digitalWrite(LCD_VCI_EN, HIGH);
  delay(10);

  digitalWrite(LCD_RST, HIGH);
  delay(2);
  digitalWrite(LCD_RST, LOW);
  delay(20);
  digitalWrite(LCD_RST, HIGH);
  delay(130);
}
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
static bool uartHelperUsbStarted = false;
static bool uartHelperBridgeRunning = false;
static bool uartHelperUsbConnected = false;
static long uartHelperBridgeBaud = 0;
static bool uartHelperBridgeStatusShown = false;
static bool i2cHelperBridgeRunning = false;
static bool i2cHelperBridgeStatusShown = false;
static bool bleUsbPassthroughActive = false;

static float get_buzzer_safe_drive_ratio()
{
  return constrain(BUZZER_RATED_VOLTAGE / BUZZER_SUPPLY_VOLTAGE, 0.0f, 1.0f);
}

static uint32_t buzzer_percent_to_pwm_duty(float volume_percent)
{
  float clamped_percent = constrain(volume_percent, 0.0f, 100.0f);
  float drive_ratio = (clamped_percent / 100.0f) * get_buzzer_safe_drive_ratio();
  return (uint32_t)(drive_ratio * BUZZER_PWM_MAX_DUTY + 0.5f);
}
static String i2cHelperCommandBuffer;
static bool i2cHelperRawMode = false;
static int i2cHelperCurrentAddress = -1;
static uint8_t i2cHelperRawBuffer[64];
static size_t i2cHelperRawBufferLen = 0;
static unsigned long i2cHelperRawLastByteMs = 0;
static bool spiHelperBridgeRunning = false;
static bool spiHelperBridgeStatusShown = false;
static String spiHelperCommandBuffer;
static bool spiHelperRawMode = false;
static uint8_t spiHelperRawBuffer[64];
static size_t spiHelperRawBufferLen = 0;
static unsigned long spiHelperRawLastByteMs = 0;
static uint32_t spiHelperFrequency = 1000000;
static uint8_t spiHelperMode = SPI_MODE0;
static SPIClass spiBridge(HSPI);
static bool spiBridgeStarted = false;
static bool bleBridgeRunning = false;
static lv_obj_t *i2cHelperOutputTextArea = NULL;
static lv_obj_t *spiHelperOutputTextArea = NULL;
static portMUX_TYPE bleRxMux = portMUX_INITIALIZER_UNLOCKED;
static volatile size_t bleRxHead = 0;
static volatile size_t bleRxTail = 0;
static uint8_t bleRxQueue[512];

static constexpr int EXTERNAL_I2C_SDA = 6;
static constexpr int EXTERNAL_I2C_SCL = 7;
static constexpr int INTERNAL_I2C_SDA = 18;
static constexpr int INTERNAL_I2C_SCL = 21;
static constexpr int SPI_BRIDGE_CS = 6;    // UI pin 8
static constexpr int SPI_BRIDGE_SCLK = 7;  // UI pin 7
static constexpr int SPI_BRIDGE_MISO = 44; // UI pin 12
static constexpr int SPI_BRIDGE_MOSI = 43; // UI pin 13
static constexpr size_t I2C_LISTEN_MAX_BYTES = 64;
static constexpr size_t I2C_LISTEN_QUEUE_DEPTH = 8;

enum class UpperTransport : uint8_t
{
  Usb,
  Ble
};

enum class BleProtocolMode : uint8_t
{
  Uart,
  I2cBridge,
  I2cListen,
  Spi
};

enum class I2cTransportMode : uint8_t
{
  None,
  Bridge,
  Listen
};

struct I2cSniffEvent
{
  uint8_t address = 0;
  bool read = false;
  bool anyNack = false;
  bool overflow = false;
  uint8_t dataLen = 0;
  uint8_t data[I2C_LISTEN_MAX_BYTES] = {0};
};

static I2cTransportMode i2cActiveMode = I2cTransportMode::None;
static portMUX_TYPE i2cListenMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool i2cListenEnabled = false;
static volatile bool i2cListenTransactionActive = false;
static volatile bool i2cListenAddressCaptured = false;
static volatile bool i2cListenRead = false;
static volatile bool i2cListenAnyNack = false;
static volatile bool i2cListenOverflow = false;
static volatile uint8_t i2cListenAddress = 0;
static volatile uint8_t i2cListenCurrentByte = 0;
static volatile uint8_t i2cListenBitCount = 0;
static volatile bool i2cListenAwaitingAck = false;
static volatile uint8_t i2cListenDataLen = 0;
static volatile uint8_t i2cListenData[I2C_LISTEN_MAX_BYTES] = {0};
static volatile bool i2cListenPrevSdaHigh = true;
static volatile uint8_t i2cListenQueueWrite = 0;
static volatile uint8_t i2cListenQueueRead = 0;
static volatile uint32_t i2cListenDroppedEvents = 0;
static I2cSniffEvent i2cListenQueue[I2C_LISTEN_QUEUE_DEPTH];
static bool i2cListenInterruptsAttached = false;
static bool logFilterInstalled = false;
static void begin_usb_logging_if_needed(void);

static void install_log_filter_once(void)
{
  if (logFilterInstalled)
  {
    return;
  }

  app_log_init(begin_usb_logging_if_needed);
  app_log_install_esp_sink();
  logFilterInstalled = true;
}

static void apply_configured_log_state(void)
{
  app_log_set_level(log_config_level);
  app_log_set_channel(log_config_channel);
  app_log_set_enabled(log_config_enabled);
}

static void begin_usb_logging_if_needed(void)
{
  if (!uartHelperUsbStarted)
  {
    USBSerial.setRxBufferSize(512);
    USBSerial.setTxTimeoutMs(0);
    USBSerial.begin();
    uartHelperUsbStarted = true;
    uartHelperUsbConnected = (bool)USBSerial;
  }
}

static void route_debug_output_to_uart0(void)
{
  // Do not stack an additional override when setup() switches from the early
  // boot route to the normal runtime route.
  app_log_resume_all();
  app_log_state_t state = app_log_get_effective_state();
  state.enabled = true;
  state.channel = APP_LOG_CHANNEL_UART0;
  app_log_push_override(state);
}

static void route_boot_output_to_usb(void)
{
  app_log_resume_all();
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  begin_usb_logging_if_needed();
#endif
  app_log_state_t state = app_log_get_effective_state();
  state.enabled = true;
  state.channel = APP_LOG_CHANNEL_USB;
#if EXLINK_POWER_DIAGNOSTICS
  state.level = APP_LOG_LEVEL_INFO;
#endif
  app_log_push_override(state);
}

static void route_debug_output_to_usb(void)
{
  app_log_resume_all();
  apply_configured_log_state();
}

static app_log_channel_t sanitize_log_channel(int raw_channel)
{
  switch (raw_channel)
  {
  case APP_LOG_CHANNEL_NONE:
  case APP_LOG_CHANNEL_UART0:
  case APP_LOG_CHANNEL_USB:
    return (app_log_channel_t)raw_channel;
  default:
    return APP_LOG_CHANNEL_USB;
  }
}

static app_log_level_t sanitize_log_level(int raw_level)
{
  switch (raw_level)
  {
  case APP_LOG_LEVEL_ERROR:
  case APP_LOG_LEVEL_WARN:
  case APP_LOG_LEVEL_INFO:
  case APP_LOG_LEVEL_DEBUG:
    return (app_log_level_t)raw_level;
  default:
    return APP_LOG_LEVEL_INFO;
  }
}

extern "C" bool get_app_log_enabled(void)
{
  return log_config_enabled;
}

extern "C" void set_app_log_enabled(bool enabled)
{
  log_config_enabled = enabled;
  apply_configured_log_state();
}

extern "C" app_log_channel_t get_app_log_channel(void)
{
  return log_config_channel;
}

extern "C" void set_app_log_channel(app_log_channel_t channel)
{
  log_config_channel = sanitize_log_channel((int)channel);
  apply_configured_log_state();
}

extern "C" app_log_level_t get_app_log_level(void)
{
  return log_config_level;
}

extern "C" void set_app_log_level(app_log_level_t level)
{
  log_config_level = sanitize_log_level((int)level);
  apply_configured_log_state();
}

extern "C" bool get_ble_usb_passthrough_enabled(void)
{
  return ble_usb_passthrough_enabled;
}

extern "C" void set_ble_usb_passthrough_enabled(bool enabled)
{
  ble_usb_passthrough_enabled = enabled;
}

static bool allocate_lvgl_draw_buffer(void)
{
  // A partial native-resolution buffer keeps RAM use bounded while preserving
  // LVGL's normal dirty-area contract.
  const size_t buffer_lines[] = {32, 16, 8};
  for (size_t index = 0; index < sizeof(buffer_lines) / sizeof(buffer_lines[0]); ++index)
  {
    const size_t candidate_pixels = (size_t)screenWidth * buffer_lines[index];

    lv_color_t *candidate = (lv_color_t *)heap_caps_malloc(candidate_pixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (candidate != nullptr)
    {
      draw_buf_pixels = candidate;
      draw_buf_pixel_count = candidate_pixels;
      draw_buf_size_bytes = candidate_pixels * sizeof(lv_color_t);
      APP_LOGI("lvgl", "draw buffer %u bytes (%u pixels)\r\n",
               (unsigned)draw_buf_size_bytes,
               (unsigned)draw_buf_pixel_count);
      return true;
    }
  }

  APP_LOGE("lvgl", "draw buffer allocation failed\r\n");
  return false;
}

/*
 * CO5300 QSPI local writes are most reliable when the horizontal transfer
 * width is a multiple of four pixels.  Round the invalidated area before
 * LVGL renders it, so the extra pixels are rendered from the real scene
 * rather than copied from an unrelated source row in the flush callback.
 */
static void co5300_rounder(lv_disp_drv_t *disp, lv_area_t *area)
{
  LV_UNUSED(disp);
  area->x1 = max<int32_t>(0, area->x1 & ~3);
  area->x2 = min<int32_t>(LCD_WIDTH - 1, area->x2 | 3);
  // CO5300 RASET also requires paired rows. LVGL uses this callback when
  // computing buffer slices: a 35-row slice is rounded down to 32 rows rather
  // than emitting odd-height windows during settings-page scrolls.
  area->y1 = max<int32_t>(0, area->y1 & ~3);
  area->y2 = min<int32_t>(LCD_HEIGHT - 1, area->y2 | 3);
}

static void uart_helper_set_status(const char *message, bool clear_first)
{
  if (uart_extarea == NULL || !lv_obj_is_valid(uart_extarea))
  {
    return;
  }

  if (clear_first)
  {
    lv_textarea_set_text(uart_extarea, "");
  }

  lv_textarea_add_text(uart_extarea, message);

  size_t length = strlen(message);
  if (length == 0 || message[length - 1] != '\n')
  {
    lv_textarea_add_text(uart_extarea, "\n");
  }
}

static void i2c_helper_set_status(const char *message, bool clear_first)
{
  if (i2c_extarea == NULL || !lv_obj_is_valid(i2c_extarea))
  {
    return;
  }

  if (clear_first)
  {
    lv_textarea_set_text(i2c_extarea, "");
  }

  lv_textarea_add_text(i2c_extarea, message);

  size_t length = strlen(message);
  if (length == 0 || message[length - 1] != '\n')
  {
    lv_textarea_add_text(i2c_extarea, "\n");
  }
}

static String format_i2c_bytes(const uint8_t *data, size_t len);
static bool parse_i2c_value(const char *token, long &value);
static bool parse_spi_frequency_value(const char *token, uint32_t &value);
static void upper_transport_send_line(UpperTransport transport, const String &text);
static BleProtocolMode get_ble_protocol_mode(long *selectedBaud = nullptr);
static I2cTransportMode get_i2c_page_mode(void);
static void i2c_helper_reset_command_mode(void);
static void spi_helper_reset_command_mode(void);
static void i2c_bridge_process_bytes(const uint8_t *data, size_t len, UpperTransport transport, lv_obj_t *textarea);
static void spi_bridge_process_bytes(const uint8_t *data, size_t len, UpperTransport transport, lv_obj_t *textarea);
static void i2c_bridge_flush_idle(UpperTransport transport, lv_obj_t *textarea);
static void spi_bridge_flush_idle(UpperTransport transport, lv_obj_t *textarea);
static bool ble_uses_i2c_backend(void);
static bool ble_uses_spi_backend(void);
static void ensure_i2c_mode(I2cTransportMode mode);
static void stop_i2c_backend(void);
static void stop_spi_backend(bool restoreDebugOutput);
static void start_i2c_listener(void);
static void stop_i2c_listener(void);
static void drain_i2c_listener_events(UpperTransport transport, lv_obj_t *textarea);
static void restore_internal_i2c_bus(void);
static void begin_external_i2c_bus(void);
static void i2c_helper_reset_raw_mode(void);
static String i2c_helper_flush_raw_buffer(void);
static String spi_helper_flush_raw_buffer(void);
static void ble_queue_rx_bytes(const uint8_t *data, size_t len);
static size_t ble_dequeue_rx_bytes(uint8_t *data, size_t maxLen);
static void ble_clear_rx_queue(void);
static void discard_usb_serial_input(void);
static void discard_serial_input(void);
static String handle_i2c_command_batch(const String &command_batch);
static String handle_spi_command_batch(const String &command_batch);
static void stop_uart_bridge_backend(bool restoreDebugOutput);
static void apply_ble_transport_bytes(const uint8_t *data, size_t len, BleProtocolMode mode, lv_obj_t *textarea, UpperTransport transport);

extern "C" void release_transport_page_handles(void)
{
  // The screen will be deleted by the same loop/LVGL owner. Cached aliases
  // must not outlive it, even if the allocator later reuses the same address.
  i2cHelperOutputTextArea = nullptr;
  spiHelperOutputTextArea = nullptr;
}

static void append_directional_bytes_to_ui(lv_obj_t *textarea, const char *prefix, const uint8_t *data, size_t len)
{
  if (textarea == NULL || !lv_obj_is_valid(textarea) ||
      !lv_obj_check_type(textarea, &lv_textarea_class) || prefix == NULL || data == NULL || len == 0)
  {
    return;
  }

  app_log_capture_bridge(prefix, (const char *)data, len);
  char rendered[320];
  while (len) {
    const auto chunk = format_bridge_text(rendered, sizeof(rendered), prefix, data, len);
    if (!chunk.consumed) break;
    // max_length alone makes LVGL insert one character at a time and eventually
    // stops showing new data. Bound history before one append per serial chunk.
    const char *existing = lv_textarea_get_text(textarea);
    if (strlen(existing) + chunk.length > 4096)
      lv_textarea_set_text(textarea, "[older display history cleared]\n");
    lv_textarea_add_text(textarea, rendered);
    data += chunk.consumed;
    len -= chunk.consumed;
  }
}

static void i2c_helper_append_text(const char *prefix, const String &text)
{
  lv_obj_t *target = i2cHelperOutputTextArea != NULL ? i2cHelperOutputTextArea : i2c_extarea;
  append_directional_bytes_to_ui(target, prefix, (const uint8_t *)text.c_str(), text.length());
}

static void spi_helper_append_text(const char *prefix, const String &text)
{
  lv_obj_t *target = spiHelperOutputTextArea != NULL ? spiHelperOutputTextArea : spi_extarea;
  append_directional_bytes_to_ui(target, prefix, (const uint8_t *)text.c_str(), text.length());
}

static void spi_helper_append_bytes(const char *prefix, const uint8_t *data, size_t len)
{
  String text = format_i2c_bytes(data, len);
  lv_obj_t *target = spiHelperOutputTextArea != NULL ? spiHelperOutputTextArea : spi_extarea;
  append_directional_bytes_to_ui(target, prefix, (const uint8_t *)text.c_str(), text.length());
}

static void spi_bridge_begin_hardware(void)
{
  if (spiBridgeStarted)
  {
    return;
  }

  Serial.end();
  pinMode(SPI_BRIDGE_CS, OUTPUT);
  digitalWrite(SPI_BRIDGE_CS, HIGH);
  spiBridge.begin(SPI_BRIDGE_SCLK, SPI_BRIDGE_MISO, SPI_BRIDGE_MOSI, -1);
  spiBridgeStarted = true;
}

static void spi_bridge_end_hardware(void)
{
  if (!spiBridgeStarted)
  {
    return;
  }

  digitalWrite(SPI_BRIDGE_CS, HIGH);
  spiBridge.end();
  spiBridgeStarted = false;
  Serial.begin(currentBaudRate);
}

static void spi_bridge_transfer_bytes(const uint8_t *tx, uint8_t *rx, size_t len)
{
  if (!spiBridgeStarted || tx == NULL || rx == NULL || len == 0)
  {
    return;
  }

  SPISettings settings(spiHelperFrequency, MSBFIRST, spiHelperMode);
  spiBridge.beginTransaction(settings);
  digitalWrite(SPI_BRIDGE_CS, LOW);
  for (size_t i = 0; i < len; ++i)
  {
    rx[i] = spiBridge.transfer(tx[i]);
    if (i + 1 < len)
    {
      delayMicroseconds(SPI_INTERBYTE_DELAY_US);
    }
  }
  digitalWrite(SPI_BRIDGE_CS, HIGH);
  spiBridge.endTransaction();
}

static String spi_helper_flush_raw_buffer(void)
{
  if (spiHelperRawBufferLen == 0)
  {
    return String();
  }

  uint8_t rx[64];
  size_t len = spiHelperRawBufferLen;
  spi_helper_append_bytes("TX -> ", spiHelperRawBuffer, len);
  spi_bridge_transfer_bytes(spiHelperRawBuffer, rx, len);
  spiHelperRawBufferLen = 0;

  String response = format_i2c_bytes(rx, len);
  return response;
}

static String handle_spi_command_line(const String &command_line)
{
  char buffer[256];
  command_line.toCharArray(buffer, sizeof(buffer));

  char *argv[64];
  int argc = 0;
  char *token = strtok(buffer, " \t,");
  while (token != NULL && argc < (int)(sizeof(argv) / sizeof(argv[0])))
  {
    argv[argc++] = token;
    token = strtok(NULL, " \t,");
  }

  if (argc == 0)
  {
    return String("ERR empty command");
  }

  String op = argv[0];
  op.toUpperCase();

  if (op == "HELP")
  {
    return String("HELP | X <byte...> | MODE <0-3> | FREQ <value>[HZ|KHZ|MHZ] | RAW ON");
  }

  if (op == "MODE")
  {
    if (argc != 2)
    {
      return String("ERR usage: MODE <0-3>");
    }

    long mode_value = 0;
    if (!parse_i2c_value(argv[1], mode_value) || mode_value < 0 || mode_value > 3)
    {
      return String("ERR invalid mode");
    }
    static const uint8_t mode_map[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
    spiHelperMode = mode_map[mode_value];
    return String("OK");
  }

  if (op == "FREQ")
  {
    if (argc != 2)
    {
      return String("ERR usage: FREQ <value>[HZ|KHZ|MHZ]");
    }

    uint32_t freq_value = 0;
    if (!parse_spi_frequency_value(argv[1], freq_value))
    {
      return String("ERR invalid frequency");
    }
    spiHelperFrequency = freq_value;
    return String("OK");
  }

  if (op == "RAW")
  {
    if (argc != 2)
    {
      return String("ERR usage: RAW ON");
    }

    String arg1 = argv[1];
    arg1.toUpperCase();
    if (arg1 == "ON" || arg1 == "1")
    {
      spiHelperRawMode = true;
      spiHelperRawBufferLen = 0;
      return String("OK raw=on");
    }
    return String("ERR usage: RAW ON");
  }

  if (op == "X" || op == "TX")
  {
    if (argc < 2)
    {
      return String("ERR usage: X <byte...>");
    }

    uint8_t tx[64];
    uint8_t rx[64];
    size_t len = 0;
    for (int i = 1; i < argc && len < sizeof(tx); ++i)
    {
      long byte_value = 0;
      if (!parse_i2c_value(argv[i], byte_value) || byte_value < 0 || byte_value > 0xFF)
      {
        return String("ERR invalid byte");
      }
      tx[len++] = (uint8_t)byte_value;
    }

    spi_helper_append_bytes("TX -> ", tx, len);
    spi_bridge_transfer_bytes(tx, rx, len);
    return format_i2c_bytes(rx, len);
  }

  return String("ERR unknown command");
}

static String handle_spi_command_batch(const String &command_batch)
{
  String remaining = command_batch;
  String combined;

  while (remaining.length() > 0)
  {
    int separator = remaining.indexOf(';');
    String command;
    if (separator >= 0)
    {
      command = remaining.substring(0, separator);
      remaining = remaining.substring(separator + 1);
    }
    else
    {
      command = remaining;
      remaining = "";
    }

    command.trim();
    if (command.length() == 0)
    {
      continue;
    }

    spi_helper_append_text("TX -> ", command);
    String response = handle_spi_command_line(command);

    if (!combined.isEmpty())
    {
      combined += "\r\n";
    }
    combined += response;
    spi_helper_append_text("RX <- ", response);
  }

  if (combined.isEmpty())
  {
    return String("ERR empty command");
  }

  return combined;
}

static void restore_internal_i2c_bus(void)
{
  // Internal devices stay on Wire for the whole runtime.
}

static void begin_external_i2c_bus(void)
{
  Wire1.begin(EXTERNAL_I2C_SDA, EXTERNAL_I2C_SCL);
}

static bool parse_i2c_value(const char *token, long &value)
{
  if (token == NULL || *token == '\0')
  {
    return false;
  }

  char *end = nullptr;
  value = strtol(token, &end, 0);
  return end != token && *end == '\0';
}

static bool parse_spi_frequency_value(const char *token, uint32_t &value)
{
  if (token == NULL || *token == '\0')
  {
    return false;
  }

  String freq_token = token;
  freq_token.trim();
  if (freq_token.isEmpty())
  {
    return false;
  }

  String normalized = freq_token;
  normalized.toUpperCase();

  double multiplier = 1.0;
  if (normalized.endsWith("MHZ"))
  {
    normalized.remove(normalized.length() - 3);
    multiplier = 1000000.0;
  }
  else if (normalized.endsWith("KHZ"))
  {
    normalized.remove(normalized.length() - 3);
    multiplier = 1000.0;
  }
  else if (normalized.endsWith("HZ"))
  {
    normalized.remove(normalized.length() - 2);
  }

  normalized.trim();
  if (normalized.isEmpty())
  {
    return false;
  }

  char number_buffer[32];
  normalized.toCharArray(number_buffer, sizeof(number_buffer));
  char *end = nullptr;
  double parsed = strtod(number_buffer, &end);
  if (end == number_buffer || *end != '\0')
  {
    return false;
  }

  double result = parsed * multiplier;
  if (result < 1000.0 || result > 40000000.0)
  {
    return false;
  }

  value = (uint32_t)(result + 0.5);
  return true;
}

static String format_i2c_bytes(const uint8_t *data, size_t len)
{
  String result;
  for (size_t i = 0; i < len; ++i)
  {
    if (i > 0)
    {
      result += ' ';
    }

    char byte_text[6];
    snprintf(byte_text, sizeof(byte_text), "0x%02X", data[i]);
    result += byte_text;
  }
  return result;
}

static void i2c_helper_reset_command_mode(void)
{
  i2cHelperCommandBuffer = "";
  i2c_helper_reset_raw_mode();
}

static void spi_helper_reset_command_mode(void)
{
  spiHelperCommandBuffer = "";
  spiHelperRawMode = false;
  spiHelperRawBufferLen = 0;
  spiHelperRawLastByteMs = 0;
}

static String format_i2c_sniff_event(const I2cSniffEvent &event)
{
  String line = "I2C ";
  char addr_text[8];
  snprintf(addr_text, sizeof(addr_text), "0x%02X", event.address);
  line += addr_text;
  line += event.read ? " R" : " W";

  if (event.dataLen > 0)
  {
    line += ' ';
    line += format_i2c_bytes(event.data, event.dataLen);
  }

  if (event.anyNack)
  {
    line += " NACK";
  }
  if (event.overflow)
  {
    line += " ...";
  }

  return line;
}

static void IRAM_ATTR i2c_listen_reset_transaction_locked(void)
{
  i2cListenTransactionActive = false;
  i2cListenAddressCaptured = false;
  i2cListenRead = false;
  i2cListenAnyNack = false;
  i2cListenOverflow = false;
  i2cListenAddress = 0;
  i2cListenCurrentByte = 0;
  i2cListenBitCount = 0;
  i2cListenAwaitingAck = false;
  i2cListenDataLen = 0;
}

static void IRAM_ATTR i2c_listen_queue_transaction_locked(void)
{
  if (!i2cListenAddressCaptured)
  {
    i2c_listen_reset_transaction_locked();
    return;
  }

  uint8_t next = (uint8_t)((i2cListenQueueWrite + 1U) % I2C_LISTEN_QUEUE_DEPTH);
  if (next == i2cListenQueueRead)
  {
    i2cListenDroppedEvents++;
    i2c_listen_reset_transaction_locked();
    return;
  }

  I2cSniffEvent &event = i2cListenQueue[i2cListenQueueWrite];
  event.address = i2cListenAddress;
  event.read = i2cListenRead;
  event.anyNack = i2cListenAnyNack;
  event.overflow = i2cListenOverflow;
  event.dataLen = i2cListenDataLen;
  for (uint8_t i = 0; i < i2cListenDataLen; ++i)
  {
    event.data[i] = i2cListenData[i];
  }

  i2cListenQueueWrite = next;
  i2c_listen_reset_transaction_locked();
}

static void IRAM_ATTR i2c_sda_change_isr(void)
{
  if (!i2cListenEnabled)
  {
    return;
  }

  bool sdaHigh = gpio_get_level((gpio_num_t)EXTERNAL_I2C_SDA) != 0;
  bool sclHigh = gpio_get_level((gpio_num_t)EXTERNAL_I2C_SCL) != 0;

  portENTER_CRITICAL_ISR(&i2cListenMux);
  bool previous = i2cListenPrevSdaHigh;
  i2cListenPrevSdaHigh = sdaHigh;

  if (sclHigh && previous && !sdaHigh)
  {
    if (i2cListenTransactionActive && i2cListenAddressCaptured)
    {
      i2c_listen_queue_transaction_locked();
    }

    i2c_listen_reset_transaction_locked();
    i2cListenTransactionActive = true;
  }
  else if (sclHigh && !previous && sdaHigh)
  {
    if (i2cListenTransactionActive)
    {
      i2c_listen_queue_transaction_locked();
    }
  }
  portEXIT_CRITICAL_ISR(&i2cListenMux);
}

static void IRAM_ATTR i2c_scl_rising_isr(void)
{
  if (!i2cListenEnabled)
  {
    return;
  }

  bool sdaHigh = gpio_get_level((gpio_num_t)EXTERNAL_I2C_SDA) != 0;

  portENTER_CRITICAL_ISR(&i2cListenMux);
  if (!i2cListenTransactionActive)
  {
    portEXIT_CRITICAL_ISR(&i2cListenMux);
    return;
  }

  if (!i2cListenAwaitingAck)
  {
    i2cListenCurrentByte = (uint8_t)((i2cListenCurrentByte << 1) | (sdaHigh ? 1 : 0));
    i2cListenBitCount++;

    if (i2cListenBitCount >= 8)
    {
      if (!i2cListenAddressCaptured)
      {
        i2cListenAddress = (uint8_t)(i2cListenCurrentByte >> 1);
        i2cListenRead = (i2cListenCurrentByte & 0x01U) != 0;
        i2cListenAddressCaptured = true;
      }
      else if (i2cListenDataLen < I2C_LISTEN_MAX_BYTES)
      {
        i2cListenData[i2cListenDataLen++] = i2cListenCurrentByte;
      }
      else
      {
        i2cListenOverflow = true;
      }

      i2cListenCurrentByte = 0;
      i2cListenBitCount = 0;
      i2cListenAwaitingAck = true;
    }
  }
  else
  {
    if (sdaHigh)
    {
      i2cListenAnyNack = true;
    }
    i2cListenAwaitingAck = false;
  }

  portEXIT_CRITICAL_ISR(&i2cListenMux);
}

static void start_i2c_listener(void)
{
  if (i2cListenInterruptsAttached)
  {
    stop_i2c_listener();
  }
  Wire1.end();
  pinMode(EXTERNAL_I2C_SDA, INPUT_PULLUP);
  pinMode(EXTERNAL_I2C_SCL, INPUT_PULLUP);

  portENTER_CRITICAL(&i2cListenMux);
  i2cListenQueueWrite = 0;
  i2cListenQueueRead = 0;
  i2cListenDroppedEvents = 0;
  i2cListenPrevSdaHigh = gpio_get_level((gpio_num_t)EXTERNAL_I2C_SDA) != 0;
  i2c_listen_reset_transaction_locked();
  i2cListenEnabled = true;
  portEXIT_CRITICAL(&i2cListenMux);

  attachInterrupt(digitalPinToInterrupt(EXTERNAL_I2C_SDA), i2c_sda_change_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EXTERNAL_I2C_SCL), i2c_scl_rising_isr, RISING);
  i2cListenInterruptsAttached = true;
}

static void stop_i2c_listener(void)
{
  portENTER_CRITICAL(&i2cListenMux);
  i2cListenEnabled = false;
  i2c_listen_reset_transaction_locked();
  i2cListenQueueWrite = 0;
  i2cListenQueueRead = 0;
  portEXIT_CRITICAL(&i2cListenMux);

  if (i2cListenInterruptsAttached)
  {
    detachInterrupt(digitalPinToInterrupt(EXTERNAL_I2C_SDA));
    detachInterrupt(digitalPinToInterrupt(EXTERNAL_I2C_SCL));
    i2cListenInterruptsAttached = false;
  }
}

static void stop_i2c_backend(void)
{
  if (i2cActiveMode == I2cTransportMode::Listen)
  {
    stop_i2c_listener();
  }
  else if (i2cActiveMode == I2cTransportMode::Bridge)
  {
    Wire1.end();
    pinMode(EXTERNAL_I2C_SDA, INPUT_PULLUP);
    pinMode(EXTERNAL_I2C_SCL, INPUT_PULLUP);
    restore_internal_i2c_bus();
  }

  i2cActiveMode = I2cTransportMode::None;
  i2c_helper_reset_command_mode();
}

static void stop_spi_backend(bool restoreDebugOutput)
{
  if (!spiHelperBridgeRunning && !spiBridgeStarted)
  {
    return;
  }

  String raw_flush = spi_helper_flush_raw_buffer();
  if (!raw_flush.isEmpty())
  {
    spi_helper_append_text("RX <- ", raw_flush);
  }

  spiHelperBridgeRunning = false;
  spiHelperBridgeStatusShown = false;
  spi_helper_reset_command_mode();
  spi_bridge_end_hardware();
  pinMode(SPI_BRIDGE_CS, INPUT_PULLUP);
  pinMode(SPI_BRIDGE_SCLK, INPUT_PULLUP);
  pinMode(SPI_BRIDGE_MISO, INPUT_PULLUP);
  pinMode(SPI_BRIDGE_MOSI, INPUT_PULLUP);

  if (restoreDebugOutput)
  {
    route_debug_output_to_usb();
  }
}

static void ensure_i2c_mode(I2cTransportMode mode)
{
  if (mode == i2cActiveMode)
  {
    return;
  }

  stop_i2c_backend();
  if (mode == I2cTransportMode::Bridge)
  {
    begin_external_i2c_bus();
  }
  else if (mode == I2cTransportMode::Listen)
  {
    start_i2c_listener();
  }
  i2cActiveMode = mode;
}

static bool pop_i2c_listener_event(I2cSniffEvent &event)
{
  bool hasEvent = false;
  portENTER_CRITICAL(&i2cListenMux);
  if (i2cListenQueueRead != i2cListenQueueWrite)
  {
    event = i2cListenQueue[i2cListenQueueRead];
    i2cListenQueueRead = (uint8_t)((i2cListenQueueRead + 1U) % I2C_LISTEN_QUEUE_DEPTH);
    hasEvent = true;
  }
  portEXIT_CRITICAL(&i2cListenMux);
  return hasEvent;
}

static void drain_i2c_listener_events(UpperTransport transport, lv_obj_t *textarea)
{
  i2cHelperOutputTextArea = textarea;

  I2cSniffEvent event;
  while (pop_i2c_listener_event(event))
  {
    String line = format_i2c_sniff_event(event);
    i2c_helper_append_text("RX <- ", line);
    upper_transport_send_line(transport, line);
  }
}

static void i2c_bridge_flush_idle(UpperTransport transport, lv_obj_t *textarea)
{
  i2cHelperOutputTextArea = textarea;

  if (i2cHelperRawMode && i2cHelperRawBufferLen > 0 && millis() - i2cHelperRawLastByteMs >= 2)
  {
    String raw_flush = i2c_helper_flush_raw_buffer();
    if (!raw_flush.isEmpty())
    {
      i2c_helper_append_text("RX <- ", raw_flush);
      upper_transport_send_line(transport, raw_flush);
    }
  }
}

static void spi_bridge_flush_idle(UpperTransport transport, lv_obj_t *textarea)
{
  spiHelperOutputTextArea = textarea;

  if (spiHelperRawMode && spiHelperRawBufferLen > 0 && millis() - spiHelperRawLastByteMs >= SPI_RAW_FLUSH_IDLE_MS)
  {
    String response = spi_helper_flush_raw_buffer();
    if (!response.isEmpty())
    {
      spi_helper_append_text("RX <- ", response);
      upper_transport_send_line(transport, response);
    }
  }
}

static void i2c_bridge_process_bytes(const uint8_t *data, size_t len, UpperTransport transport, lv_obj_t *textarea)
{
  if (data == NULL || len == 0)
  {
    return;
  }

  i2cHelperOutputTextArea = textarea;

  for (size_t i = 0; i < len; ++i)
  {
    char c = (char)data[i];

    if (i2cHelperRawMode)
    {
      if (i2cHelperRawBufferLen < sizeof(i2cHelperRawBuffer))
      {
        i2cHelperRawBuffer[i2cHelperRawBufferLen++] = (uint8_t)c;
      }
      i2cHelperRawLastByteMs = millis();

      if (i2cHelperRawBufferLen >= sizeof(i2cHelperRawBuffer))
      {
        String raw_flush = i2c_helper_flush_raw_buffer();
        if (!raw_flush.isEmpty())
        {
          i2c_helper_append_text("RX <- ", raw_flush);
          upper_transport_send_line(transport, raw_flush);
        }
      }
      continue;
    }

    if (c == '\r')
    {
      continue;
    }

    if (c == '\n')
    {
      String command = i2cHelperCommandBuffer;
      i2cHelperCommandBuffer = "";
      command.trim();
      if (command.length() == 0)
      {
        continue;
      }

      String response = handle_i2c_command_batch(command);
      upper_transport_send_line(transport, response);
      continue;
    }

    if (i2cHelperCommandBuffer.length() < 240)
    {
      i2cHelperCommandBuffer += c;
    }
  }
}

static void spi_bridge_process_bytes(const uint8_t *data, size_t len, UpperTransport transport, lv_obj_t *textarea)
{
  if (data == NULL || len == 0)
  {
    return;
  }

  spiHelperOutputTextArea = textarea;

  for (size_t i = 0; i < len; ++i)
  {
    char c = (char)data[i];

    if (spiHelperRawMode)
    {
      if (spiHelperRawBufferLen < sizeof(spiHelperRawBuffer))
      {
        spiHelperRawBuffer[spiHelperRawBufferLen++] = (uint8_t)c;
      }
      spiHelperRawLastByteMs = millis();
      if (spiHelperRawBufferLen >= sizeof(spiHelperRawBuffer))
      {
        String response = spi_helper_flush_raw_buffer();
        if (!response.isEmpty())
        {
          spi_helper_append_text("RX <- ", response);
          upper_transport_send_line(transport, response);
        }
      }
      continue;
    }

    if (c == '\r')
    {
      continue;
    }

    if (c == '\n')
    {
      String command = spiHelperCommandBuffer;
      spiHelperCommandBuffer = "";
      command.trim();
      if (command.length() == 0)
      {
        continue;
      }

      String response = handle_spi_command_batch(command);
      upper_transport_send_line(transport, response);
      continue;
    }

    if (spiHelperCommandBuffer.length() < 240)
    {
      spiHelperCommandBuffer += c;
    }
  }
}

static void i2c_helper_append_bytes(const char *prefix, const uint8_t *data, size_t len)
{
  String text = format_i2c_bytes(data, len);
  lv_obj_t *target = i2cHelperOutputTextArea != NULL ? i2cHelperOutputTextArea : i2c_extarea;
  append_directional_bytes_to_ui(target, prefix, (const uint8_t *)text.c_str(), text.length());
}

static void i2c_helper_reset_raw_mode(void)
{
  i2cHelperRawMode = false;
  i2cHelperRawBufferLen = 0;
  i2cHelperRawLastByteMs = 0;
}

static String i2c_helper_flush_raw_buffer(void)
{
  if (i2cHelperRawBufferLen == 0)
  {
    return String();
  }

  if (i2cHelperCurrentAddress < 0 || i2cHelperCurrentAddress > 0x7F)
  {
    i2cHelperRawBufferLen = 0;
    return String("ERR current address not set");
  }

  i2c_helper_append_bytes("TX -> ", i2cHelperRawBuffer, i2cHelperRawBufferLen);

  Wire1.beginTransmission((uint8_t)i2cHelperCurrentAddress);
  Wire1.write(i2cHelperRawBuffer, i2cHelperRawBufferLen);
  uint8_t err = Wire1.endTransmission();
  i2cHelperRawBufferLen = 0;

  if (err == 0)
  {
    return String();
  }
  return String("ERR ") + String(err);
}

static String handle_i2c_command_line(const String &command_line)
{
  char buffer[256];
  command_line.toCharArray(buffer, sizeof(buffer));

  char *argv[64];
  int argc = 0;
  char *token = strtok(buffer, " \t,");
  while (token != NULL && argc < (int)(sizeof(argv) / sizeof(argv[0])))
  {
    argv[argc++] = token;
    token = strtok(NULL, " \t,");
  }

  if (argc == 0)
  {
    return String("ERR empty command");
  }

  String op = argv[0];
  op.toUpperCase();

  begin_external_i2c_bus();

  if (op == "SCAN")
  {
    String result;
    for (int addr = 1; addr < 127; ++addr)
    {
      Wire1.beginTransmission((uint8_t)addr);
      uint8_t err = Wire1.endTransmission();
      if (err == 0)
      {
        if (!result.isEmpty())
        {
          result += ' ';
        }
        char addr_text[6];
        snprintf(addr_text, sizeof(addr_text), "0x%02X", addr);
        result += addr_text;
      }
    }
    return result.isEmpty() ? String("NONE") : result;
  }

  if (op == "W")
  {
    if (argc < 3)
    {
      return String("ERR usage: W <addr> <byte...>");
    }

    long addr_value = 0;
    if (!parse_i2c_value(argv[1], addr_value) || addr_value < 0 || addr_value > 0x7F)
    {
      return String("ERR invalid address");
    }
    i2cHelperCurrentAddress = (int)addr_value;

    Wire1.beginTransmission((uint8_t)addr_value);
    for (int i = 2; i < argc; ++i)
    {
      long byte_value = 0;
      if (!parse_i2c_value(argv[i], byte_value) || byte_value < 0 || byte_value > 0xFF)
      {
        return String("ERR invalid byte");
      }
      Wire1.write((uint8_t)byte_value);
    }

    uint8_t err = Wire1.endTransmission();
    return err == 0 ? String("OK") : (String("ERR ") + String(err));
  }

  if (op == "R")
  {
    if (i2cHelperCurrentAddress < 0)
    {
      return String("ERR current address not set");
    }

    long reg_value = 0;
    long read_len = 1;

    if (argc == 2)
    {
      if (!parse_i2c_value(argv[1], reg_value) || reg_value < 0 || reg_value > 0xFF)
      {
        return String("ERR invalid register");
      }
    }
    else if (argc == 3)
    {
      if (!parse_i2c_value(argv[1], reg_value) || reg_value < 0 || reg_value > 0xFF)
      {
        return String("ERR invalid register");
      }
      if (!parse_i2c_value(argv[2], read_len) || read_len < 1 || read_len > 64)
      {
        return String("ERR invalid read length");
      }
    }
    else
    {
      return String("ERR usage: R <reg> [len]");
    }

    Wire1.beginTransmission((uint8_t)i2cHelperCurrentAddress);
    Wire1.write((uint8_t)reg_value);
    uint8_t err = Wire1.endTransmission(false);
    if (err != 0)
    {
      return String("ERR ") + String(err);
    }

    uint8_t data[64];
    size_t actual = Wire1.requestFrom((uint8_t)i2cHelperCurrentAddress, (size_t)read_len, true);
    for (size_t i = 0; i < actual && Wire1.available(); ++i)
    {
      data[i] = (uint8_t)Wire1.read();
    }

    return actual > 0 ? format_i2c_bytes(data, actual) : String("NONE");
  }

  if (op == "T")
  {
    if (argc < 3)
    {
      return String("ERR usage: T <addr> <read_len> [write_byte...]");
    }

    long addr_value = 0;
    long read_len = 0;
    if (!parse_i2c_value(argv[1], addr_value) || addr_value < 0 || addr_value > 0x7F)
    {
      return String("ERR invalid address");
    }
    i2cHelperCurrentAddress = (int)addr_value;
    if (!parse_i2c_value(argv[2], read_len) || read_len < 0 || read_len > 64)
    {
      return String("ERR invalid read length");
    }

    Wire1.beginTransmission((uint8_t)addr_value);
    for (int i = 3; i < argc; ++i)
    {
      long byte_value = 0;
      if (!parse_i2c_value(argv[i], byte_value) || byte_value < 0 || byte_value > 0xFF)
      {
        return String("ERR invalid byte");
      }
      Wire1.write((uint8_t)byte_value);
    }

    uint8_t err = Wire1.endTransmission(false);
    if (err != 0)
    {
      return String("ERR ") + String(err);
    }

    uint8_t data[64];
    size_t actual = Wire1.requestFrom((uint8_t)addr_value, (size_t)read_len, true);
    for (size_t i = 0; i < actual && Wire1.available(); ++i)
    {
      data[i] = (uint8_t)Wire1.read();
    }

    return actual > 0 ? format_i2c_bytes(data, actual) : String("NONE");
  }

  if (op == "HELP")
  {
    return String("HELP | A <dev_addr> | SCAN | W <dev_addr> <byte...> | R <reg> [len] | T <dev_addr> <read_len> [write_byte...] | RAW <dev_addr>|RAW ON|RAW OFF");
  }

  if (op == "A" || op == "ADDR")
  {
    if (argc != 2)
    {
      return String("ERR usage: A <addr>");
    }

    long addr_value = 0;
    if (!parse_i2c_value(argv[1], addr_value) || addr_value < 0 || addr_value > 0x7F)
    {
      return String("ERR invalid address");
    }
    i2cHelperCurrentAddress = (int)addr_value;
    Wire1.beginTransmission((uint8_t)i2cHelperCurrentAddress);
    uint8_t err = Wire1.endTransmission();
    char result[48];
    if (err == 0)
    {
      snprintf(result, sizeof(result), "OK addr=0x%02X ACK", i2cHelperCurrentAddress);
    }
    else if (err == 2)
    {
      snprintf(result, sizeof(result), "OK addr=0x%02X NOACK", i2cHelperCurrentAddress);
    }
    else
    {
      snprintf(result, sizeof(result), "OK addr=0x%02X ERR=%u", i2cHelperCurrentAddress, err);
    }
    return String(result);
  }

  if (op == "RAW")
  {
    if (argc == 2)
    {
      String arg1 = argv[1];
      arg1.toUpperCase();

      if (arg1 == "OFF" || arg1 == "0")
      {
        i2c_helper_reset_raw_mode();
        return String("OK raw=off");
      }
      if (arg1 == "ON" || arg1 == "1")
      {
        if (i2cHelperCurrentAddress < 0)
        {
          return String("ERR current address not set");
        }
        i2cHelperRawMode = true;
        i2cHelperRawBufferLen = 0;
        return String("OK raw=on");
      }

      long addr_value = 0;
      if (!parse_i2c_value(argv[1], addr_value) || addr_value < 0 || addr_value > 0x7F)
      {
        return String("ERR invalid address");
      }
      i2cHelperCurrentAddress = (int)addr_value;
      i2cHelperRawMode = true;
      i2cHelperRawBufferLen = 0;
      char result[32];
      snprintf(result, sizeof(result), "OK raw=on addr=0x%02X", i2cHelperCurrentAddress);
      return String(result);
    }

    if (argc == 3)
    {
      String arg1 = argv[1];
      arg1.toUpperCase();
      if (arg1 != "ON")
      {
        return String("ERR usage: RAW <addr> | RAW ON [addr] | RAW OFF");
      }

      long addr_value = 0;
      if (!parse_i2c_value(argv[2], addr_value) || addr_value < 0 || addr_value > 0x7F)
      {
        return String("ERR invalid address");
      }
      i2cHelperCurrentAddress = (int)addr_value;
      i2cHelperRawMode = true;
      i2cHelperRawBufferLen = 0;
      char result[32];
      snprintf(result, sizeof(result), "OK raw=on addr=0x%02X", i2cHelperCurrentAddress);
      return String(result);
    }

    return String("ERR usage: RAW <addr> | RAW ON [addr] | RAW OFF");
  }

  return String("ERR unknown command");
}

static String handle_i2c_command_batch(const String &command_batch)
{
  String remaining = command_batch;
  String combined;

  while (remaining.length() > 0)
  {
    int separator = remaining.indexOf(';');
    String command;
    if (separator >= 0)
    {
      command = remaining.substring(0, separator);
      remaining = remaining.substring(separator + 1);
    }
    else
    {
      command = remaining;
      remaining = "";
    }

    command.trim();
    if (command.length() == 0)
    {
      continue;
    }

    i2c_helper_append_text("TX -> ", command);
    String response = handle_i2c_command_line(command);

    if (!combined.isEmpty())
    {
      combined += "\r\n";
    }
    combined += response;
    i2c_helper_append_text("RX <- ", response);
  }

  if (combined.isEmpty())
  {
    return String("ERR empty command");
  }

  return combined;
}

static void uart_helper_start_bridge(long baud)
{
  begin_usb_logging_if_needed();
  uartHelperBridgeStatusShown = false;

  if (!uartHelperBridgeRunning)
  {
    discard_usb_serial_input();
    discard_serial_input();
    ble_clear_rx_queue();
    if (i2cActiveMode != I2cTransportMode::None && i2cscan_flag == 0 && !ble_uses_i2c_backend())
    {
      stop_i2c_backend();
    }
    if (spiHelperBridgeRunning && spi_bridge_flag == 0 && !ble_uses_spi_backend())
    {
      stop_spi_backend(false);
    }
    uartHelperBridgeRunning = true;
    uartHelperBridgeStatusShown = false;
    APP_LOGI("uart", "bridge started, logs suspended\r\n");
    app_log_suspend();
  }

  if (baud <= 0)
  {
    baud = currentBaudRate > 0 ? currentBaudRate : 115200;
  }

  if (uartHelperBridgeBaud != baud)
  {
    Serial.updateBaudRate(baud);
    uartHelperBridgeBaud = baud;
    currentBaudRate = baud;
    save_persistent_settings();
    uartHelperBridgeStatusShown = false;
  }

  if (!uartHelperBridgeStatusShown)
  {
    char status[96];
    snprintf(status, sizeof(status), "USB <-> UART0 bridge active @ %ld baud", baud);
    uart_helper_set_status(status, true);
    uartHelperBridgeStatusShown = true;
    APP_LOGI("uart", "bridge active @ %ld baud\r\n", baud);
  }
}

static void uart_helper_stop_bridge(void)
{
  stop_uart_bridge_backend(true);
  APP_LOGI("uart", "bridge stopped\r\n");

  if (uart_extarea != NULL && lv_obj_is_valid(uart_extarea))
  {
    lv_textarea_set_text(uart_extarea, "USB <-> UART0 bridge closed.\n");
  }
}

static void stop_uart_bridge_backend(bool restoreDebugOutput)
{
  uartHelperBridgeRunning = false;
  uartHelperBridgeStatusShown = false;
  uartHelperBridgeBaud = 0;

  if (restoreDebugOutput)
  {
    route_debug_output_to_usb();
  }
}

static void uart_helper_pump_bridge(void)
{
  uint8_t buffer[64];
  bool usbConnected = uartHelperUsbStarted && (bool)USBSerial;

  if (usbConnected != uartHelperUsbConnected)
  {
    uartHelperUsbConnected = usbConnected;
    uart_helper_set_status(usbConnected ? "[USB] Host connected" : "[USB] Host disconnected", false);
    APP_LOGI("uart", "usb host %s\r\n", usbConnected ? "connected" : "disconnected");
  }

  size_t usbBudget = 128, uartBudget = 128;
  while (usbBudget && uartHelperUsbStarted && USBSerial.available() > 0 && Serial.availableForWrite() > 0)
  {
    size_t chunk = (size_t)USBSerial.available();
    if (chunk > sizeof(buffer))
    {
      chunk = sizeof(buffer);
    }
    if (chunk > usbBudget) chunk = usbBudget;
    if (chunk > size_t(Serial.availableForWrite())) chunk = Serial.availableForWrite();

    chunk = USBSerial.read(buffer, chunk);
    if (chunk == 0)
    {
      break;
    }
    usbBudget -= chunk;

    Serial.write(buffer, chunk);
    append_directional_bytes_to_ui(uart_extarea, "TX -> ", buffer, chunk);
  }

  while (uartBudget && Serial.available() > 0)
  {
    size_t chunk = (size_t)Serial.available();
    if (chunk > sizeof(buffer))
    {
      chunk = sizeof(buffer);
    }
    if (chunk > uartBudget) chunk = uartBudget;

    chunk = Serial.read(buffer, chunk);
    if (chunk == 0)
    {
      break;
    }
    uartBudget -= chunk;
    notify_user_activity();

    append_directional_bytes_to_ui(uart_extarea, "RX <- ", buffer, chunk);

    if (uartHelperUsbStarted)
    {
      size_t offset = 0;
      while (offset < chunk)
      {
        int usb_space = USBSerial.availableForWrite();
        if (usb_space <= 0)
        {
          break;
        }

        size_t to_write = chunk - offset;
        if (to_write > (size_t)usb_space)
        {
          to_write = (size_t)usb_space;
        }

        size_t written = USBSerial.write(buffer + offset, to_write);
        if (written == 0)
        {
          break;
        }
        offset += written;
      }
    }
  }
}
#endif

BLEServer *pServer = NULL;            // BLEServer指针pServer
BLECharacteristic *pTxCharacteristic; // BLECharacteristic指针 pTxCharacteristic
BLECharacteristic *pRxCharacteristic; // BLECharacteristic指针 pRxCharacteristic
volatile bool deviceConnected = false; // Written by BTC_TASK, consumed by loop.
static volatile bool deviceBleSuspended = false;
static volatile int32_t deviceBleConnectionId = -1;
static volatile BleSessionMode deviceBleMode = BleSessionMode::Off;
static BleSessionMode requestedBleMode = BleSessionMode::Off;
static BleSessionPolicy deviceBleSession;
static uint32_t bleAdvertisingTimeoutS = 120, bleConnectionTimeoutS = 120;
static bool deviceBleRadioRunning;
static bool deviceBleGattNeedsRestore;
static bool deviceBleClosing;
static uint32_t deviceBleCloseStarted;
static bool deviceBleActivity;
static portMUX_TYPE deviceBleActivityMux = portMUX_INITIALIZER_UNLOCKED;
enum class DeviceBleEventKind : uint8_t { Connect, Disconnect, Mtu };
struct DeviceBleEvent { DeviceBleEventKind kind; uint16_t detail; };
static QueueHandle_t deviceBleEvents;
bool oldDeviceConnected = false;      // 上次连接状态
uint8_t txValue = 0;

class MyServerCallbacks : public BLEServerCallbacks // BLE连接回调函数
{
  void onConnect(BLEServer *pServer)
  {
    (void)pServer; // Use the identity-bearing callback below.
  };

  void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
  {
    if (deviceBleConnectionId >= 0) { server->disconnect(param->connect.conn_id); return; }
    deviceBleConnectionId = param->connect.conn_id;
    deviceConnected = true;
    exlink::bleUpdateConnection(true, param->connect.remote_bda);
    // Encryption request is asynchronous; only Phone mode needs pairing.
    if (deviceBleMode == BleSessionMode::Phone)
      esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
    const DeviceBleEvent event{DeviceBleEventKind::Connect, param->connect.conn_id};
    xQueueSend(deviceBleEvents, &event, 0);
  }

  void onDisconnect(BLEServer *pServer)
  {
    (void)pServer;
  }
  void onDisconnect(BLEServer *pServer, esp_ble_gatts_cb_param_t *param) override
  {
    if (param->disconnect.conn_id != deviceBleConnectionId) return;
    deviceBleConnectionId = -1;
    deviceConnected = false;
    exlink::bleUpdateConnection(false);
    (void)pServer;
    const DeviceBleEvent event{DeviceBleEventKind::Disconnect, uint16_t(param->disconnect.reason)};
    xQueueSend(deviceBleEvents, &event, 0);
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *param) override
  {
    const DeviceBleEvent event{DeviceBleEventKind::Mtu, param->mtu.mtu};
    xQueueSend(deviceBleEvents, &event, 0);
  }
};

static void note_ble_bridge_activity()
{
  portENTER_CRITICAL(&deviceBleActivityMux);
  deviceBleActivity = true;
  portEXIT_CRITICAL(&deviceBleActivityMux);
}

static void ble_notify_bytes(const uint8_t *data, size_t len)
{
  if (!deviceConnected || pTxCharacteristic == NULL || data == NULL || len == 0)
  {
    return;
  }

  size_t offset = 0;
  while (offset < len)
  {
    size_t notify_len = len - offset;
    if (notify_len > 20)
    {
      notify_len = 20;
    }

    pTxCharacteristic->setValue(const_cast<uint8_t *>(data + offset), notify_len);
    pTxCharacteristic->notify();
    note_ble_bridge_activity();
    offset += notify_len;
    delay(5);
  }
}

static void upper_transport_send_line(UpperTransport transport, const String &text)
{
  if (transport == UpperTransport::Usb)
  {
    if (uartHelperUsbStarted)
    {
      USBSerial.print(text);
      USBSerial.print("\r\n");
    }
    return;
  }

  ble_notify_bytes((const uint8_t *)text.c_str(), text.length());
  static const uint8_t newline_bytes[] = {'\r', '\n'};
  ble_notify_bytes(newline_bytes, sizeof(newline_bytes));
}

static void ble_queue_rx_bytes(const uint8_t *data, size_t len)
{
  if (data == NULL || len == 0)
  {
    return;
  }

  portENTER_CRITICAL(&bleRxMux);
  for (size_t i = 0; i < len; ++i)
  {
    size_t next = (bleRxHead + 1U) % sizeof(bleRxQueue);
    if (next == bleRxTail)
    {
      break;
    }
    bleRxQueue[bleRxHead] = data[i];
    bleRxHead = next;
  }
  portEXIT_CRITICAL(&bleRxMux);
}

static size_t ble_dequeue_rx_bytes(uint8_t *data, size_t maxLen)
{
  if (data == NULL || maxLen == 0)
  {
    return 0;
  }

  size_t count = 0;
  portENTER_CRITICAL(&bleRxMux);
  while (bleRxTail != bleRxHead && count < maxLen)
  {
    data[count++] = bleRxQueue[bleRxTail];
    bleRxTail = (bleRxTail + 1U) % sizeof(bleRxQueue);
  }
  portEXIT_CRITICAL(&bleRxMux);
  return count;
}

static void ble_clear_rx_queue(void)
{
  portENTER_CRITICAL(&bleRxMux);
  bleRxHead = 0;
  bleRxTail = 0;
  portEXIT_CRITICAL(&bleRxMux);
}

static void stop_ble_bridge_backend(bool release_memory = true)
{
  if (!bleBridgeRunning)
  {
    return;
  }

  (void)release_memory; // The update service remains discoverable outside BLE UI.
  bleBridgeRunning = false;
  bleUsbPassthroughActive = false;

  if (i2cActiveMode != I2cTransportMode::None && i2cscan_flag == 0)
  {
    stop_i2c_backend();
    i2cHelperBridgeRunning = false;
  }

  if (spiHelperBridgeRunning && spi_bridge_flag == 0)
  {
    spi_helper_reset_command_mode();
    spiHelperBridgeRunning = false;
    spi_bridge_end_hardware();
  }

  route_debug_output_to_usb();
  APP_LOGI("ble", "bridge stopped\r\n");
}


static void apply_ble_transport_bytes(const uint8_t *data, size_t len, BleProtocolMode mode, lv_obj_t *textarea, UpperTransport transport)
{
  if (data == NULL || len == 0)
  {
    return;
  }

  switch (mode)
  {
  case BleProtocolMode::Uart:
    Serial.write(data, len);
    break;
  case BleProtocolMode::I2cBridge:
    ensure_i2c_mode(I2cTransportMode::Bridge);
    i2c_bridge_process_bytes(data, len, transport, textarea);
    break;
  case BleProtocolMode::I2cListen:
    i2cHelperOutputTextArea = textarea;
    i2c_helper_append_text("RX <- ", "ERR listen mode");
    upper_transport_send_line(transport, "ERR listen mode");
    break;
  case BleProtocolMode::Spi:
    if (i2cActiveMode != I2cTransportMode::None)
    {
      stop_i2c_backend();
    }
    if (!spiHelperBridgeRunning)
    {
      spiHelperBridgeRunning = true;
      spi_bridge_begin_hardware();
    }
    spi_bridge_process_bytes(data, len, transport, textarea);
    break;
  }
}

static void discard_usb_serial_input(void)
{
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  if (!uartHelperUsbStarted)
  {
    return;
  }

  while (USBSerial.available() > 0)
  {
    USBSerial.read();
  }
#endif
}

static void discard_serial_input(void)
{
  while (Serial.available() > 0)
  {
    Serial.read();
  }
}

static String get_dropdown_selected_text(lv_obj_t *dropdown)
{
  if (dropdown == NULL || !lv_obj_is_valid(dropdown))
  {
    return String();
  }

  char buffer[32];
  lv_dropdown_get_selected_str(dropdown, buffer, sizeof(buffer));
  return String(buffer);
}

static BleProtocolMode get_ble_protocol_mode(long *selectedBaud)
{
  String selection = get_dropdown_selected_text(wireless_uart_list);
  selection.trim();

  long baud = selection.toInt();
  if (baud > 0)
  {
    if (selectedBaud != nullptr)
    {
      *selectedBaud = baud;
    }
    return BleProtocolMode::Uart;
  }

  if (selection.equalsIgnoreCase("I2C Bridge"))
  {
    return BleProtocolMode::I2cBridge;
  }
  if (selection.equalsIgnoreCase("I2C Listen"))
  {
    return BleProtocolMode::I2cListen;
  }
  return BleProtocolMode::Spi;
}

static I2cTransportMode get_i2c_page_mode(void)
{
  String selection = get_dropdown_selected_text(i2c_mode_list);
  if (selection.equalsIgnoreCase("Listen"))
  {
    return I2cTransportMode::Listen;
  }
  return I2cTransportMode::Bridge;
}

static bool ble_uses_i2c_backend(void)
{
  if (BluetoothSerial_flag == 0)
  {
    return false;
  }

  BleProtocolMode mode = get_ble_protocol_mode(nullptr);
  return mode == BleProtocolMode::I2cBridge || mode == BleProtocolMode::I2cListen;
}

static bool ble_uses_spi_backend(void)
{
  if (BluetoothSerial_flag == 0)
  {
    return false;
  }

  return get_ble_protocol_mode(nullptr) == BleProtocolMode::Spi;
}

class MyCallbacks : public BLECharacteristicCallbacks // BLE鎺ユ敹鍥炶皟鍑芥暟
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    if (deviceBleMode != BleSessionMode::Bridge || !deviceConnected) return;
    std::string rxValue = pCharacteristic->getValue();

    if (rxValue.length() > 0)
    {
      ble_queue_rx_bytes((const uint8_t *)rxValue.data(), rxValue.length());
      note_ble_bridge_activity();
    }
  }
};

static void start_device_ble_services()
{
  if (pServer) return;
  if (!deviceBleEvents) deviceBleEvents = xQueueCreate(8, sizeof(DeviceBleEvent));
  if (!deviceBleEvents) { APP_LOGE("ble", "event queue allocation failed\r\n"); return; }
  BLEDevice::init("Exlink");
  pServer = BLEDevice::createServer();
  static MyServerCallbacks serverCallbacks;
  static MyCallbacks bridgeCallbacks;
  pServer->setCallbacks(&serverCallbacks);
  BLEService *service = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = service->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  pRxCharacteristic = service->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(&bridgeCallbacks);
  service->start();
  exlink::bleUpdateInit(pServer);
  deviceBleRadioRunning = true;
}

extern "C" bool get_phone_ble_enabled(void)
{
  return requestedBleMode == BleSessionMode::Phone;
}

extern "C" void set_phone_ble_enabled(bool enabled)
{
  if (exlink::bleUpdateBusy()) return; // Finish/cancel the current transfer first.
  BluetoothSerial_flag = 0;
  requestedBleMode = enabled ? BleSessionMode::Phone : BleSessionMode::Off;
  notify_user_activity();
}

extern "C" uint32_t get_ble_advertising_timeout_s(void) { return bleAdvertisingTimeoutS; }
extern "C" uint32_t get_ble_connection_timeout_s(void) { return bleConnectionTimeoutS; }
extern "C" void set_ble_advertising_timeout_s(uint32_t value)
{
  bleAdvertisingTimeoutS = constrain(value, 30U, 300U);
}
extern "C" void set_ble_connection_timeout_s(uint32_t value)
{
  bleConnectionTimeoutS = constrain(value, 60U, 600U);
}

static void advertise_device_ble_mode()
{
  // Advertise one role only. Both registered services remain allocated, but
  // their RX/read callbacks are gated by the selected mode.
  BLEAdvertisementData advertisingData, scanData;
  advertisingData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advertisingData.setCompleteServices(BLEUUID(deviceBleMode == BleSessionMode::Phone ?
      "7c9e0001-7b3f-4f6d-9e1a-71b4c3a22100" : SERVICE_UUID));
  scanData.setName("Exlink");
  auto advertising = pServer->getAdvertising();
  advertising->setAdvertisementData(advertisingData);
  advertising->setScanResponseData(scanData);
  advertising->start();
}

static bool stop_device_ble_radio()
{
  // loop owns every radio lifecycle operation. Never delete/recreate GATT
  // objects on mode switches, and never disable a host with an open connection.
  if (!deviceBleRadioRunning) return true;
  if (deviceConnected) return false;
  exlink::bleUpdateSetEnabled(false);
  esp_err_t host = ESP_OK, controller = ESP_OK;
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
    host = esp_bluedroid_disable();
    // The database is already gone even if controller disable later fails.
    if (host == ESP_OK) deviceBleGattNeedsRestore = true;
  }
  if (host == ESP_OK && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED)
    controller = esp_bt_controller_disable();
  if (host != ESP_OK || controller != ESP_OK) {
    APP_LOGE("ble", "radio stop failed: host=%s controller=%s\r\n",
             esp_err_to_name(host), esp_err_to_name(controller));
    return false;
  }
  deviceBleGattNeedsRestore = true;
  deviceBleRadioRunning = false;
  return true;
}

static bool resume_device_ble_radio()
{
  if (!pServer) start_device_ble_services();
  if (!pServer) return false;
  if (!deviceBleRadioRunning) {
    esp_err_t controller = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    esp_err_t host = controller == ESP_OK ? esp_bluedroid_enable() : controller;
    if (host != ESP_OK) {
      if (controller == ESP_OK) esp_bt_controller_disable();
      APP_LOGE("ble", "radio start failed: %s\r\n", esp_err_to_name(host));
      return false;
    }
    deviceBleRadioRunning = true;
    if (deviceBleGattNeedsRestore) {
      // Bluedroid disable unregisters the application and GATT database.
      // C++ characteristic objects survive, so explicitly recreate handles.
      if (!pServer->restartGattDatabase()) {
        APP_LOGE("ble", "GATT database restore failed\r\n");
        stop_device_ble_radio();
        return false;
      }
      deviceBleGattNeedsRestore = false;
      APP_LOGI("ble", "GATT database restored after radio restart\r\n");
    }
  }
  return true;
}

static void device_ble_task()
{
  const uint32_t now = millis();
  // Entering BLE Serial is an explicit user action; staying on that page
  // after a timeout must not restart the radio automatically.
  if (BluetoothSerial_flag == 1 && requestedBleMode != BleSessionMode::Bridge)
    requestedBleMode = BleSessionMode::Bridge;
  if (requestedBleMode == BleSessionMode::Bridge && BluetoothSerial_flag == 0)
    requestedBleMode = BleSessionMode::Off;

  DeviceBleEvent event;
  while (deviceBleEvents && xQueueReceive(deviceBleEvents, &event, 0) == pdTRUE) {
    if (event.kind == DeviceBleEventKind::Connect) {
      deviceBleSession.connected(true, now);
      notify_user_activity();
      APP_LOGI("ble", "client connected: mode=%s id=%u\r\n",
               deviceBleMode == BleSessionMode::Phone ? "phone" : "bridge", event.detail);
    } else if (event.kind == DeviceBleEventKind::Disconnect) {
      deviceBleSession.connected(false, now);
      APP_LOGI("ble", "client disconnected: reason=0x%02X\r\n", event.detail);
      if (!deviceBleSuspended && requestedBleMode == deviceBleMode && deviceBleMode != BleSessionMode::Off)
        advertise_device_ble_mode();
    } else {
      APP_LOGI("ble", "MTU=%u\r\n", event.detail);
    }
  }
  portENTER_CRITICAL(&deviceBleActivityMux);
  const bool bridgeActivity = deviceBleActivity;
  deviceBleActivity = false;
  portEXIT_CRITICAL(&deviceBleActivityMux);
  if (exlink::bleUpdateTakeActivity() || bridgeActivity) {
    deviceBleSession.activity(now);
    notify_user_activity();
  }
  if (deviceBleSession.expired(now, exlink::bleUpdateBusy(),
      bleAdvertisingTimeoutS * 1000U, bleConnectionTimeoutS * 1000U)) {
    requestedBleMode = BleSessionMode::Off;
    BluetoothSerial_flag = 0;
    APP_LOGI("ble", "session timeout; radio OFF\r\n");
  }
  if (deviceBleMode == requestedBleMode && !deviceBleClosing &&
      (requestedBleMode != BleSessionMode::Off || !deviceBleRadioRunning)) return;

  if (deviceBleRadioRunning) {
    if (!deviceBleClosing) {
      deviceBleClosing = true;
      deviceBleCloseStarted = now;
      deviceBleSuspended = true;
      pServer->getAdvertising()->stop();
      exlink::updateRevokeConsent();
      if (deviceConnected) pServer->disconnect(deviceBleConnectionId);
      stop_ble_bridge_backend();
      ble_clear_rx_queue();
    }
    if (deviceConnected) {
      if (now - deviceBleCloseStarted >= 2000U) {
        APP_LOGW("ble", "waiting for disconnect before radio stop\r\n");
        deviceBleCloseStarted = now;
      }
      return;
    }
    if (!stop_device_ble_radio()) return;
  }
  deviceBleMode = BleSessionMode::Off;
  deviceBleSession.stop();
  deviceBleClosing = false;
  if (requestedBleMode != BleSessionMode::Off) {
    if (!resume_device_ble_radio()) {
      requestedBleMode = BleSessionMode::Off;
      BluetoothSerial_flag = 0;
    } else {
      deviceBleMode = requestedBleMode;
      deviceBleSuspended = false;
      exlink::bleUpdateSetEnabled(deviceBleMode == BleSessionMode::Phone);
      deviceBleSession.start(deviceBleMode, now);
      advertise_device_ble_mode();
      APP_LOGI("ble", "mode=%s advertising; timeout=%lu s\r\n",
               deviceBleMode == BleSessionMode::Phone ? "phone" : "bridge", (unsigned long)bleAdvertisingTimeoutS);
    }
  } else APP_LOGI("ble", "radio OFF\r\n");
  ui_refresh_phone_ble_state();
}

static void power_log_result(const char *action, const exlink::Mcp4017Result &result)
{
  app_log_log(result.status == exlink::Mcp4017Status::Ok ? APP_LOG_LEVEL_INFO : APP_LOG_LEVEL_ERROR,
              "power", "%s addr=0x2F target=%d queued=%u tx=%u rx=%u readback=%d result=%s\r\n",
              action, result.target, unsigned(result.queued_bytes), unsigned(result.tx_code),
              unsigned(result.received_bytes), result.readback, exlink::mcp4017_status_name(result.status));
}

static void power_finish_request(const char *action, const exlink::Mcp4017Result &result)
{
  power_last_result = result;
  ++power_request_count;
  if (result.status == exlink::Mcp4017Status::Ok) mcp4107_value = result.readback;
  else mcp4107_value = -1;
  power_log_result(action, result);
}

static void apply_mcp4107_value(int value)
{
  power_finish_request("write", power_wiper.write(value));
}

void mcp4017_init() // 数字电位器初始化
{
  APP_LOGI("power", "MCP4017 SDA=GPIO%d SCL=GPIO%d addr=0x2F; presets raw=%d/%d/%d boot=%d\r\n",
           INTERNAL_I2C_SDA, INTERNAL_I2C_SCL, power_preset_11v, power_preset_5v,
           power_preset_3v3, power_boot_preset);
  const uint8_t probe_code = power_wiper.probe();
  APP_LOGI("power", "MCP4017 address probe tx=%u (0=ACK, 2=NACK, 5=timeout)\r\n", unsigned(probe_code));
  if (probe_code != 0) {
    exlink::Mcp4017Result failure;
    failure.status = exlink::Mcp4017Status::TxFailed;
    failure.target = power_boot_preset;
    failure.tx_code = probe_code;
    power_finish_request("boot-probe", failure);
    return;
  }
  // Capture the actual pre-init value; an MCU reset need not reset the wiper.
  power_log_result("boot-read", power_wiper.read());
  apply_mcp4107_value(power_boot_preset);
}

void mcp4017_task() // 数字电位器任务
{
  if (power_pending_id || !internal_io_started) return;
  const int request = mcp4107_flag;
  if (request < 1 || request > 3) return;
  const auto kind = request == 1 ? exlink::IoRequestKind::PowerWrite :
                                  exlink::IoRequestKind::PowerAdjust;
  const int value = request == 1 ? mcp4107_target_value : (request == 2 ? -1 : 1);
  const uint32_t id = ++io_request_id;
  // Preserve an unaccepted UI request. An accepted write is attempted once;
  // a transport failure never causes an automatic retry of a power change.
  if (internal_io.submit(kind, value, id)) {
    power_pending_id = id;
    power_pending_action = request;
    mcp4107_flag = 0;
  }
}

static void internal_io_results_task()
{
  exlink::IoResult result;
  while (internal_io.takeResult(result)) {
    if (result.id == power_pending_id) {
      power_pending_id = 0;
      if (!result.cancelled)
        power_finish_request(power_pending_action == 1 ? "write" :
                             (power_pending_action == 2 ? "plus" : "minus"), result.power);
    } else if (result.id == power_diag_pending_id) {
      power_diag_pending_id = 0;
      if (!result.cancelled) { power_diag_sample = result; power_diag_sample_ready = true; }
    } else if (result.id == ina_pending_id) {
      ina_pending_id = 0;
      if (!result.cancelled) { ina_sample = result; ina_sample_ready = true; }
    }
  }
}

static void power_diagnostics_task()
{
#if EXLINK_POWER_DIAGNOSTICS
  static uint32_t last_sample_ms = 0;
  const uint32_t now = millis();
  if (!power_diag_pending_id && uint32_t(now - last_sample_ms) >= 2000) {
    const uint32_t id = ++io_request_id;
    if (internal_io.submit(exlink::IoRequestKind::PowerRead, 0, id)) {
      power_diag_pending_id = id;
      last_sample_ms = now;
    }
  }
  if (!power_diag_sample_ready) return;
  power_diag_sample_ready = false;
  const exlink::Mcp4017Result &sample = power_diag_sample.power;
  mcp4107_value = sample.status == exlink::Mcp4017Status::Ok ? sample.readback : -1;
  APP_LOGI("power.diag", "fw=%s t=%lu addr=0x2F read=%s raw=%d output=%d ina_active=%d V=%.3f "
           "last_target=%d last_tx=%u last_result=%s requests=%lu\r\n",
           esp_ota_get_app_description()->version, (unsigned long)now,
           exlink::mcp4017_status_name(sample.status), sample.readback, digitalRead(1), ina266_flag, v,
           power_last_result.target, unsigned(power_last_result.tx_code),
           exlink::mcp4017_status_name(power_last_result.status), (unsigned long)power_request_count);
  const exlink::IoDiagnostics stats = internal_io.diagnostics();
  APP_LOGI("touch.diag", "ready=%d irq=%d reads=%lu errors=%lu recoveries=%lu max_io_ms=%lu ui_gap_ms=%lu\r\n",
           stats.touch_ready, digitalRead(INT_N_PIN), (unsigned long)stats.touch_reads,
           (unsigned long)stats.touch_errors, (unsigned long)stats.touch_recoveries,
           (unsigned long)stats.max_transaction_ms, (unsigned long)ui_loop_max_gap_ms);
  ui_loop_max_gap_ms = 0;
#endif
}

void buzzer_task() // 蜂鸣器任务
{
  if (!buzzer_ledc_ready)
  {
    return;
  }

  float volume = constrain(buzzer_volume_percent, 0.0f, 100.0f);
  if (volume <= 0.0f)
  {
    ledcWrite(1, 0);
    buzzer_feedback_active = false;
    buzzer_feedback_end_ms = 0;
    return;
  }

  if (buzzer_feedback_active)
  {
    return;
  }

  // 用户侧仍然显示 0-100%，内部映射到额定 3.0V 对应的安全驱动范围。
  uint32_t duty_value = buzzer_percent_to_pwm_duty(volume);
  ledcWrite(1, duty_value);
  buzzer_feedback_active = true;
  buzzer_feedback_end_ms = millis() + BUZZER_FEEDBACK_DURATION_MS;
}

static void buzzer_update_task(void)
{
  if (!buzzer_ledc_ready || !buzzer_feedback_active)
  {
    return;
  }

  if ((long)(buzzer_feedback_end_ms - millis()) <= 0)
  {
    ledcWrite(1, 0);
    buzzer_feedback_active = false;
    buzzer_feedback_end_ms = 0;
  }
}

static bool configure_pwm_channel(uint8_t channel, uint32_t freq, uint8_t &resolution_bits)
{
  if (freq == 0)
  {
    return false;
  }

  for (int bits = LEDC_TIMER_14_BIT; bits >= LEDC_TIMER_1_BIT; --bits)
  {
    uint32_t actual_freq = ledcSetup(channel, freq, bits);
    if (actual_freq > 0)
    {
      resolution_bits = bits;
      return true;
    }
  }

  resolution_bits = 0;
  return false;
}

void pwm_task() // pwm输出任务
{

  if (pwm_flag == 1)
  {

    const char *freq_str = lv_textarea_get_text(fre);
    const char *duty_str = lv_textarea_get_text(duty);
    pwm_Freq = max(atoi(freq_str), 0);
    pwm_Duty = constrain(atoi(duty_str), 0, 100); // 限制占空比在 0-100 之间

    if (pwm_Freq != new_pwm_Freq || pwm_Duty != new_pwm_Duty)
    {
      uint8_t resolution_bits = 0;
      if (configure_pwm_channel(2, pwm_Freq, resolution_bits))
      {
        ledcAttachPin(5, 2); // 将引脚连接到通道2
        const uint32_t max_duty = (1U << resolution_bits) - 1U;
        const uint32_t duty_value = map(pwm_Duty, 0, 100, 0, max_duty);
        ledcWrite(2, duty_value); // 通道2输出
        pwm_ledc_ready = true;
      }
      else
      {
        pwm_ledc_ready = false;
      }

      new_pwm_Freq = pwm_Freq;
      new_pwm_Duty = pwm_Duty;
    }
  }

  if (pwm_flag == 0)
  {
    new_pwm_Freq = 0;
    new_pwm_Duty = 0;
    if (pwm_ledc_ready)
    {
      ledcWrite(2, 0);
      pwm_ledc_ready = false;
    }
  }
}

void i2cscan_task() // I2C扫描任务
{
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  I2cTransportMode desiredMode = i2cscan_flag == 1 ? get_i2c_page_mode() : I2cTransportMode::None;
  if (desiredMode == I2cTransportMode::None && ble_uses_i2c_backend())
  {
    BleProtocolMode bleMode = get_ble_protocol_mode(nullptr);
    desiredMode = bleMode == BleProtocolMode::I2cListen ? I2cTransportMode::Listen : I2cTransportMode::Bridge;
  }

  if (desiredMode == I2cTransportMode::None)
  {
    if (i2cHelperBridgeRunning)
    {
      stop_i2c_backend();
      discard_usb_serial_input();
      discard_serial_input();
      ble_clear_rx_queue();
      i2cHelperBridgeRunning = false;
      i2cHelperBridgeStatusShown = false;
      if (BluetoothSerial_flag == 0)
      {
        route_debug_output_to_usb();
      }
      if (i2cscan_flag != 1 && i2c_extarea != NULL && lv_obj_is_valid(i2c_extarea))
      {
        lv_textarea_set_text(i2c_extarea, "USB <-> I2C bridge closed.\n");
      }
      APP_LOGI("i2c", "bridge stopped\r\n");
    }
    return;
  }

  begin_usb_logging_if_needed();
  if (!i2cHelperBridgeRunning)
  {
    discard_usb_serial_input();
    discard_serial_input();
    ble_clear_rx_queue();
    if (uartHelperBridgeRunning && uart_helper_flag == 0)
    {
      stop_uart_bridge_backend(false);
    }
    i2cHelperBridgeRunning = true;
    i2cHelperBridgeStatusShown = false;
    APP_LOGI("i2c", "bridge started, logs suspended\r\n");
    app_log_suspend();
  }

  if (spiHelperBridgeRunning && spi_bridge_flag == 0 && !ble_uses_spi_backend())
  {
    stop_spi_backend(false);
  }

  if (desiredMode != i2cActiveMode)
  {
    discard_usb_serial_input();
    discard_serial_input();
    ble_clear_rx_queue();
  }
  ensure_i2c_mode(desiredMode);

  if (!i2cHelperBridgeStatusShown)
  {
    if (desiredMode == I2cTransportMode::Bridge && i2c_extarea != NULL && lv_obj_is_valid(i2c_extarea))
    {
      lv_textarea_set_text(i2c_extarea, "");
    }
    i2cHelperBridgeStatusShown = true;
  }

  if (i2cscan_flag != 1)
  {
    return;
  }

  if (desiredMode == I2cTransportMode::Listen)
  {
    i2cHelperOutputTextArea = i2c_extarea;
    while (uartHelperUsbStarted && USBSerial.available() > 0)
    {
      char c = (char)USBSerial.read();
      if (c == '\r')
      {
        continue;
      }
      if (c == '\n')
      {
        String command = i2cHelperCommandBuffer;
        i2cHelperCommandBuffer = "";
        command.trim();
        if (command.length() > 0)
        {
          i2c_helper_append_text("RX <- ", "ERR listen mode");
          upper_transport_send_line(UpperTransport::Usb, "ERR listen mode");
        }
        continue;
      }
      if (i2cHelperCommandBuffer.length() < 240)
      {
        i2cHelperCommandBuffer += c;
      }
    }
    drain_i2c_listener_events(UpperTransport::Usb, i2c_extarea);
  }
  else
  {
    while (uartHelperUsbStarted && USBSerial.available() > 0)
    {
      uint8_t c = (uint8_t)USBSerial.read();
      i2c_bridge_process_bytes(&c, 1, UpperTransport::Usb, i2c_extarea);
    }
    i2c_bridge_flush_idle(UpperTransport::Usb, i2c_extarea);
  }
#else
  if (i2cscan_flag == 1)
  {

    Wire1.begin(6, 7);
    lv_textarea_add_text(i2c_extarea, "Scanning...\n");
    APP_LOGI("i2c", "scan started\r\n");

    nDevices = 0;
    for (address = 1; address < 127; address++)
    {
      Wire1.beginTransmission(address); // 从指定的地址开始向I2C从设备进行传输
      error = Wire1.endTransmission();  // 停止与从机的数据传输
      if (error == 0)
      {
        char i2c_address[20];
        lv_textarea_add_text(i2c_extarea, "I2C device address 0x");
        if (address < 16)
          lv_textarea_add_text(i2c_extarea, "0");
        sprintf(i2c_address, "%X\n", address);
        lv_textarea_add_text(i2c_extarea, i2c_address);

        nDevices++;
      }
      else if (error == 4)
      {
        char i2c_address[20];
        lv_textarea_add_text(i2c_extarea, "Unknown error at address 0x");
        if (address < 16)
          lv_textarea_add_text(i2c_extarea, "0");
        sprintf(i2c_address, "%X\n", address);
        lv_textarea_add_text(i2c_extarea, i2c_address);
      }
    }
    if (nDevices == 0)
    {
      lv_textarea_add_text(i2c_extarea, "No I2C devices found");
      APP_LOGW("i2c", "scan: no devices found\r\n");
    }
    else
    {
      lv_textarea_add_text(i2c_extarea, "done\n");
      APP_LOGI("i2c", "scan done, devices=%d\r\n", nDevices);
    }

    i2cscan_flag = 0;
  }
#endif
}

void spi_bridge_task()
{
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  if (spi_bridge_flag != 1 && !ble_uses_spi_backend())
  {
    if (spiHelperBridgeRunning)
    {
      discard_usb_serial_input();
      discard_serial_input();
      ble_clear_rx_queue();
      stop_spi_backend(BluetoothSerial_flag == 0);
      APP_LOGI("spi", "bridge stopped\r\n");
    }
    return;
  }

  begin_usb_logging_if_needed();
  if (!spiHelperBridgeRunning)
  {
    discard_usb_serial_input();
    discard_serial_input();
    ble_clear_rx_queue();
    if (uartHelperBridgeRunning && uart_helper_flag == 0)
    {
      stop_uart_bridge_backend(false);
    }
    if (i2cActiveMode != I2cTransportMode::None && i2cscan_flag == 0 && !ble_uses_i2c_backend())
    {
      stop_i2c_backend();
    }
    spiHelperBridgeRunning = true;
    spiHelperBridgeStatusShown = false;
    APP_LOGI("spi", "bridge started, logs suspended\r\n");
    app_log_suspend();
    spi_bridge_begin_hardware();
  }

  if (!spiHelperBridgeStatusShown)
  {
    if (spi_extarea != NULL && lv_obj_is_valid(spi_extarea))
    {
      lv_textarea_set_text(spi_extarea, "");
    }
    spiHelperBridgeStatusShown = true;
  }

  if (spi_bridge_flag == 1)
  {
    while (uartHelperUsbStarted && USBSerial.available() > 0)
    {
      uint8_t c = (uint8_t)USBSerial.read();
      spi_bridge_process_bytes(&c, 1, UpperTransport::Usb, spi_extarea);
    }
    spi_bridge_flush_idle(UpperTransport::Usb, spi_extarea);
  }
#endif
}

void uart_helper_task() // 串口助手任务
{
  if (uart_helper_flag && (!uart_list || !lv_obj_is_valid(uart_list))) {
    uart_helper_flag = 0;
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
    stop_uart_bridge_backend(true);
#endif
    return;
  }
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  if (uart_helper_flag != 1)
  {
    if (uartHelperBridgeRunning)
    {
      uart_helper_stop_bridge();
    }
    return;
  }

  char buf[16];
  lv_dropdown_get_selected_str(uart_list, buf, sizeof(buf));
  long newBaudRate = atol(buf);
  if (newBaudRate <= 0)
  {
    newBaudRate = currentBaudRate > 0 ? currentBaudRate : 115200;
  }

  uart_helper_start_bridge(newBaudRate);
  uart_helper_pump_bridge();
#else
  if (uart_helper_flag == 1)
  {

    char buf[10];
    lv_dropdown_get_selected_str(uart_list, buf, sizeof(buf));
    long newBaudRate = 0;

    if (strcmp(buf, "1200") == 0)
    {
      newBaudRate = 1200;
    }
    else if (strcmp(buf, "2400") == 0)
    {
      newBaudRate = 2400;
    }
    else if (strcmp(buf, "4800") == 0)
    {
      newBaudRate = 4800;
    }
    else if (strcmp(buf, "9600") == 0)
    {
      newBaudRate = 9600;
    }
    else if (strcmp(buf, "19200") == 0)
    {
      newBaudRate = 19200;
    }
    else if (strcmp(buf, "43000") == 0)
    {
      newBaudRate = 43000;
    }
    else if (strcmp(buf, "76800") == 0)
    {
      newBaudRate = 76800;
    }
    else if (strcmp(buf, "115200") == 0)
    {
      newBaudRate = 115200;
    }
    else if (strcmp(buf, "128000") == 0)
    {
      newBaudRate = 128000;
    }
    else if (strcmp(buf, "230400") == 0)
    {
      newBaudRate = 230400;
    }
    else if (strcmp(buf, "256000") == 0)
    {
      newBaudRate = 256000;
    }
    else if (strcmp(buf, "460800") == 0)
    {
      newBaudRate = 460800;
    }
    else if (strcmp(buf, "921600") == 0)
    {
      newBaudRate = 921600;
    }
    if (newBaudRate != currentBaudRate)
    {
      Serial.end();                  // 结束当前串口
      Serial.begin(newBaudRate);     // 以新波特率开始串口
      currentBaudRate = newBaudRate; // 更新当前波特率
      save_persistent_settings();
    }
    if (Serial.available() > 0)
    {
      uint8_t bytes[64];
      const size_t count = Serial.read(bytes, min(size_t(Serial.available()), sizeof(bytes)));
      append_directional_bytes_to_ui(uart_extarea, "RX <- ", bytes, count);
      if (count) notify_user_activity();
    }
  }
#endif
}

static bool is_servo_slider_key_control_target()
{
  if (group == NULL || servo_slider == NULL)
  {
    return false;
  }

  if (!lv_obj_is_valid(servo_slider))
  {
    return false;
  }

  return lv_group_get_focused(group) == servo_slider;
}

static bool is_servo_slider_key_control_active()
{
  return is_servo_slider_key_control_target() && lv_group_get_editing(group);
}

void keypad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) // 按键扫描函数（可识别长按短按）
{
  static uint32_t last_key = 0;
  static uint32_t push_start_time = 0;
  static bool push_pressed = false;
  static bool push_long_sent = false;
  static bool push_intercept_slider = false;
  uint8_t key_state = 0;
  uint32_t act_key = last_key;

  LV_UNUSED(indev_drv);
  const bool hardware_key = any_key_down();
  if (hardware_key) notify_user_activity();
  if (display_idle_active || suppress_keys_until_release)
  {
    // Wake/cancel only. A wake-up Enter must not activate an underlying output.
    suppress_keys_until_release = hardware_key;
    push_pressed = push_long_sent = push_intercept_slider = false;
    data->state = LV_INDEV_STATE_REL;
    data->key = 0;
    return;
  }

  // 优先处理左右滚轮，再处理中键
  if (digitalRead(RIGHT_PIN) == LOW)
  {
    key_state = 1;
  }
  else if (digitalRead(LEFT_PIN) == LOW)
  {
    key_state = 2;
  }
  else if (digitalRead(PUSH_PIN) == LOW)
  {
    if (!push_pressed)
    {
      push_pressed = true;
      push_start_time = millis();
      push_long_sent = false;
      push_intercept_slider = is_servo_slider_key_control_target();
    }
    key_state = 3;
  }
  else
  {
    if (push_pressed && push_intercept_slider)
    {
      uint32_t held_ms = millis() - push_start_time;
      if (!push_long_sent && held_ms >= SHORT_PRESS_THRESHOLD && group != NULL)
      {
        lv_group_set_editing(group, !lv_group_get_editing(group));
        buzzer_task();
      }
    }

    key_state = 0;
    push_pressed = false;
    push_long_sent = false;
    push_intercept_slider = false;
  }

  // 舵机滑条接管中键时，不再把 Enter 继续传给 LVGL
  if (push_intercept_slider && key_state == 3)
  {
    if (!push_long_sent && millis() - push_start_time >= LONG_PRESS_THRESHOLD)
    {
      lv_event_send(lv_scr_act(), LV_EVENT_LONG_PRESSED, NULL);
      push_long_sent = true;
      last_key = 0;
    }

    data->state = LV_INDEV_STATE_REL;
    data->key = last_key;
    return;
  }

  if (key_state != 0)
  {
    data->state = LV_INDEV_STATE_PR;

    switch (key_state)
    {
    case 1:
      act_key = is_servo_slider_key_control_active() ? LV_KEY_RIGHT : LV_KEY_NEXT;
      last_key = act_key;
      break;
    case 2:
      act_key = is_servo_slider_key_control_active() ? LV_KEY_LEFT : LV_KEY_PREV;
      last_key = act_key;
      break;
    case 3:
      if (push_pressed)
      {
        if (!push_long_sent && millis() - push_start_time >= LONG_PRESS_THRESHOLD)
        {
          lv_event_send(lv_scr_act(), LV_EVENT_LONG_PRESSED, NULL);
          act_key = 0;
          last_key = 0;
          push_long_sent = true;
        }
        else if (!push_long_sent)
        {
          act_key = LV_KEY_ENTER;
          last_key = act_key;
        }
      }
      break;
    default:
      break;
    }

    buzzer_task();
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }

  data->key = last_key;
}

void ina266_reset_measurement(void)
{
  ina266_sample_valid = false;
  ina_sample_ready = false;
  // Late results from the previous output session must not become new readings.
  ina_pending_id = 0;
  v = a = w = 0;
  voltageStr[0] = currentStr[0] = powerStr[0] = '\0';
}

void ina266_task() // 功率监测任务
{
  static uint32_t last_request_ms = 0;
  static uint32_t last_measurement_ms = 0;
  if (ina266_flag != 1) { ina266_sample_valid = false; ina_sample_ready = false; last_measurement_ms = 0; return; }
  if (!ina266_sample_valid) last_measurement_ms = 0;
  if (!ina_pending_id && internal_io_started && millis() - last_request_ms >= 100U) {
    const uint32_t id = ++io_request_id;
    if (internal_io.submit(exlink::IoRequestKind::Measurement, 0, id)) {
      ina_pending_id = id;
      last_request_ms = millis();
    }
  }
  if (ina_sample_ready)
  {
    ina_sample_ready = false;
    vol = ina_sample.millivolts;
    cur = ina_sample.microamps / 5000;
    wat = ina_sample.microwatts;

    if (cur > 20 * 1000)
    {
      cur = 0;
    }

    v = vol / 1000.0;
    a = cur / 1000.0;
    w = v * a;

    if (v > 10)
    {
      sprintf(voltageStr, "%0.2f", v);
    }
    else
    {
      sprintf(voltageStr, "%0.3f", v);
    }

    if (a > 10)
    {
      sprintf(currentStr, "%0.2f", a);
    }
    else
    {
      sprintf(currentStr, "%0.3f", a);
    }

    if (w > 10)
    {
      sprintf(powerStr, "%0.2f", w);
    }
    else
    {
      sprintf(powerStr, "%0.3f", w);
    }

    ina266_sample_valid = true;

    // Queue scheduling changes sample cadence. Integrate actual elapsed time,
    // excluding long sleep/OTA/fault gaps for which no measurements exist.
    const uint32_t sample_ms = millis();
    const uint32_t elapsed_ms = sample_ms - last_measurement_ms;
    if (last_measurement_ms && elapsed_ms <= 1000U) mAH += cur * elapsed_ms;
    last_measurement_ms = sample_ms;
    sprintf(mAHStr, "%0.3f", mAH / 3600000.0);
  }
}

void BluetoothSerial_task() // 无线串口任务（基于BLE实现）
{
  static BleProtocolMode lastBleMode = BleProtocolMode::Uart;
  static bool bleModeInitialized = false;
  size_t usbBudget = 128;
  if (BluetoothSerial_flag && (!wireless_uart_list || !lv_obj_is_valid(wireless_uart_list))) {
    BluetoothSerial_flag = 0;
    if (requestedBleMode == BleSessionMode::Bridge) requestedBleMode = BleSessionMode::Off;
  }

  if (BluetoothSerial_flag == 0)
  {
    // An inactive BLE page must not drain another USB/UART/I2C/SPI owner's
    // input or clear that owner's command buffer on every loop iteration.
    if (bleBridgeRunning) {
      ble_clear_rx_queue();
      i2c_helper_reset_command_mode();
      spi_helper_reset_command_mode();
    }
    stop_ble_bridge_backend();
    bleModeInitialized = false;
    return;
  }

  if (BluetoothSerial_flag == 1)
  {
    if (deviceBleMode != BleSessionMode::Bridge) return; // Mode switch/disconnect pending.
    ble_clear_rx_queue();
    i2c_helper_reset_command_mode();
    spi_helper_reset_command_mode();
    discard_serial_input();
    if (ble_usb_passthrough_enabled)
    {
      begin_usb_logging_if_needed();
      discard_usb_serial_input();
      bleUsbPassthroughActive = true;
    }
    else
    {
      bleUsbPassthroughActive = false;
    }
    bleBridgeRunning = true;
    // USB is independent of UART0. Only mute it when the user explicitly
    // enables BLE+USB passthrough; otherwise keep crash diagnostics available.
    if (bleUsbPassthroughActive || get_app_log_channel() != APP_LOG_CHANNEL_USB)
      app_log_suspend();
    APP_LOGI("ble", "bridge started; USB passthrough=%s\r\n", bleUsbPassthroughActive ? "on" : "off");
    BluetoothSerial_flag = 2;
  }
  if (BluetoothSerial_flag == 2)
  {
    if (deviceBleMode != BleSessionMode::Bridge) return;
    long selectedBaud = currentBaudRate;
    BleProtocolMode mode = get_ble_protocol_mode(&selectedBaud);
    long newBaudRate = selectedBaud;
    long previousBaudRate = currentBaudRate;
    if (!bleModeInitialized || mode != lastBleMode)
    {
      ble_clear_rx_queue();
      i2c_helper_reset_command_mode();
      spi_helper_reset_command_mode();
      discard_serial_input();
      if (bleUsbPassthroughActive)
      {
        discard_usb_serial_input();
      }
      if (uartHelperBridgeRunning && uart_helper_flag == 0)
      {
        stop_uart_bridge_backend(false);
      }
      bleModeInitialized = true;
      lastBleMode = mode;
    }
    currentBaudRate = selectedBaud;
    if (bleUsbPassthroughActive && !deviceConnected)
    {
      if (mode == BleProtocolMode::Uart && newBaudRate != previousBaudRate)
      {
        currentBaudRate = newBaudRate;
        save_persistent_settings();
      }

      uint8_t usbBuffer[64];
      while (usbBudget && uartHelperUsbStarted && USBSerial.available() > 0)
      {
        size_t usbChunk = (size_t)USBSerial.available();
        if (usbChunk > sizeof(usbBuffer))
        {
          usbChunk = sizeof(usbBuffer);
        }
        if (usbChunk > usbBudget) usbChunk = usbBudget;

        usbChunk = USBSerial.read(usbBuffer, usbChunk);
        if (usbChunk == 0)
        {
          break;
        }
        usbBudget -= usbChunk;
        notify_user_activity();

        append_directional_bytes_to_ui(wireless_uart_extarea, "USB -> ", usbBuffer, usbChunk);
        apply_ble_transport_bytes(usbBuffer, usbChunk, mode, wireless_uart_extarea, UpperTransport::Usb);
      }

      if (mode == BleProtocolMode::I2cBridge)
      {
        i2cHelperBridgeRunning = true;
        ensure_i2c_mode(I2cTransportMode::Bridge);
        i2c_bridge_flush_idle(UpperTransport::Usb, wireless_uart_extarea);
      }
      else if (mode == BleProtocolMode::I2cListen)
      {
        i2cHelperBridgeRunning = true;
        ensure_i2c_mode(I2cTransportMode::Listen);
        drain_i2c_listener_events(UpperTransport::Usb, wireless_uart_extarea);
      }
      else if (i2cscan_flag != 1)
      {
        stop_i2c_backend();
        i2cHelperBridgeRunning = false;
      }

      if (mode == BleProtocolMode::Spi)
      {
        if (!spiHelperBridgeRunning)
        {
          spiHelperBridgeRunning = true;
          spi_bridge_begin_hardware();
        }
        spi_bridge_flush_idle(UpperTransport::Usb, wireless_uart_extarea);
      }
      else if (spi_bridge_flag != 1 && spiHelperBridgeRunning)
      {
        spi_helper_reset_command_mode();
        spiHelperBridgeRunning = false;
        spi_bridge_end_hardware();
      }

      if (mode == BleProtocolMode::Uart && Serial.available() > 0 && uartHelperUsbStarted)
      {
        uint8_t buffer[64];
        size_t chunk = (size_t)Serial.available();
        if (chunk > sizeof(buffer))
        {
          chunk = sizeof(buffer);
        }

        chunk = Serial.read(buffer, chunk);
        if (chunk > 0)
        {
          append_directional_bytes_to_ui(wireless_uart_extarea, "RX <- ", buffer, chunk);
          USBSerial.write(buffer, chunk);
        }
      }
    }
    if (mode == BleProtocolMode::Uart && newBaudRate != previousBaudRate)
    {
      Serial.end();                  // 结束当前串口
      Serial.begin(newBaudRate);     // 以新波特率开始串口
      currentBaudRate = newBaudRate; // 更新当前波特率
      save_persistent_settings();
    }

    if (deviceConnected) // 连接时执行串口转发
    {
      uint8_t bleBuffer[64];
      size_t bleChunk = ble_dequeue_rx_bytes(bleBuffer, sizeof(bleBuffer));
      if (bleChunk > 0)
      {
        append_directional_bytes_to_ui(wireless_uart_extarea, "TX -> ", bleBuffer, bleChunk);
        apply_ble_transport_bytes(bleBuffer, bleChunk, mode, wireless_uart_extarea, UpperTransport::Ble);
      }

      if (bleUsbPassthroughActive)
      {
        uint8_t usbBuffer[64];
        while (usbBudget && uartHelperUsbStarted && USBSerial.available() > 0)
        {
          size_t usbChunk = (size_t)USBSerial.available();
          if (usbChunk > sizeof(usbBuffer))
          {
            usbChunk = sizeof(usbBuffer);
          }
          if (usbChunk > usbBudget) usbChunk = usbBudget;

          usbChunk = USBSerial.read(usbBuffer, usbChunk);
          if (usbChunk == 0)
          {
            break;
          }
          usbBudget -= usbChunk;
          notify_user_activity();

          append_directional_bytes_to_ui(wireless_uart_extarea, "USB -> ", usbBuffer, usbChunk);
          apply_ble_transport_bytes(usbBuffer, usbChunk, mode, wireless_uart_extarea, UpperTransport::Usb);
        }
      }

      if (mode == BleProtocolMode::Uart && newBaudRate != previousBaudRate)
      {
        currentBaudRate = newBaudRate;
        save_persistent_settings();
      }

      if (mode == BleProtocolMode::I2cBridge)
      {
        i2cHelperBridgeRunning = true;
        ensure_i2c_mode(I2cTransportMode::Bridge);
        i2c_bridge_flush_idle(UpperTransport::Ble, wireless_uart_extarea);
        if (bleUsbPassthroughActive)
        {
          i2c_bridge_flush_idle(UpperTransport::Usb, wireless_uart_extarea);
        }
      }
      else if (mode == BleProtocolMode::I2cListen)
      {
        i2cHelperBridgeRunning = true;
        ensure_i2c_mode(I2cTransportMode::Listen);
        drain_i2c_listener_events(UpperTransport::Ble, wireless_uart_extarea);
        if (bleUsbPassthroughActive)
        {
          drain_i2c_listener_events(UpperTransport::Usb, wireless_uart_extarea);
        }
      }
      else if (i2cscan_flag != 1)
      {
        stop_i2c_backend();
        i2cHelperBridgeRunning = false;
      }

      if (mode == BleProtocolMode::Spi)
      {
        if (!spiHelperBridgeRunning)
        {
          spiHelperBridgeRunning = true;
          spi_bridge_begin_hardware();
        }
        spi_bridge_flush_idle(UpperTransport::Ble, wireless_uart_extarea);
        if (bleUsbPassthroughActive)
        {
          spi_bridge_flush_idle(UpperTransport::Usb, wireless_uart_extarea);
        }
      }
      else if (spi_bridge_flag != 1 && spiHelperBridgeRunning)
      {
        spi_helper_reset_command_mode();
        spiHelperBridgeRunning = false;
        spi_bridge_end_hardware();
      }

      if (mode == BleProtocolMode::Uart && Serial.available() > 0)
      {
        uint8_t buffer[64];
        size_t chunk = (size_t)Serial.available();
        if (chunk > sizeof(buffer))
        {
          chunk = sizeof(buffer);
        }

        chunk = Serial.read(buffer, chunk);
        if (chunk > 0)
        {
          append_directional_bytes_to_ui(wireless_uart_extarea, "RX <- ", buffer, chunk);

          ble_notify_bytes(buffer, chunk);
          if (bleUsbPassthroughActive && uartHelperUsbStarted)
          {
            USBSerial.write(buffer, chunk);
          }
        }
      }
    }

    if (!deviceConnected && oldDeviceConnected) // 未连接时执行蓝牙广播
    {
      oldDeviceConnected = deviceConnected;
    }
    if (deviceConnected && !oldDeviceConnected) // 更新蓝牙状态
    {
      oldDeviceConnected = deviceConnected;
    }
  }
}

void FREcount_task() // 数字频率计任务
{
  static bool counter_running = false;
  if (FREcount_flag == 0)
  {
    if (counter_running)
    {
      pcnt_counter_pause(PCNT_UNIT);
      pcnt_counter_clear(PCNT_UNIT);
      pcnt_set_pin(PCNT_UNIT, PCNT_CHANNEL_0, PCNT_PIN_NOT_USED, PCNT_PIN_NOT_USED);
      counter_running = false;
    }
    return;
  }
  if (FREcount_flag == 1)
  {
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = 5,                // 输入引脚
        .ctrl_gpio_num = PCNT_PIN_NOT_USED, // 不使用控制引脚
        .lctrl_mode = PCNT_MODE_KEEP,       // 脉冲计数模式
        .hctrl_mode = PCNT_MODE_KEEP,       // 禁用高电平计数
        .pos_mode = PCNT_COUNT_INC,         // 正脉冲计数
        .neg_mode = PCNT_COUNT_DIS,         // 禁用负脉冲计数
        .unit = PCNT_UNIT,                  // 使用的PCNT单元
        .channel = PCNT_CHANNEL_0,
    };
    esp_err_t result = pcnt_unit_config(&pcnt_config);
    if (result == ESP_OK) result = pcnt_counter_pause(PCNT_UNIT);
    if (result == ESP_OK) result = pcnt_counter_clear(PCNT_UNIT);
    if (result == ESP_OK) result = pcnt_counter_resume(PCNT_UNIT);
    if (result != ESP_OK)
    {
      pcnt_counter_pause(PCNT_UNIT);
      pcnt_counter_clear(PCNT_UNIT);
      pcnt_set_pin(PCNT_UNIT, PCNT_CHANNEL_0, PCNT_PIN_NOT_USED, PCNT_PIN_NOT_USED);
      counter_running = false;
      FREcount_flag = 0;
      ui_refresh_runtime_control();
      APP_LOGE("frequency", "counter start failed: %d\r\n", int(result));
      return;
    }
    counter_running = true;
    previousMillis = millis();
    count = 0;
    snprintf(freqencyStr, sizeof(freqencyStr), "0");
    FREcount_flag = 2;
  }
  if (FREcount_flag == 2)
  {
    unsigned long currentMillis = millis();
    pcnt_get_counter_value(PCNT_UNIT, &count);
    unsigned long currentinterval = currentMillis - previousMillis;
    if (currentinterval >= interval) // 如果达到读取间隔
    {

      //  计算频率（假设每次读取的时间为1秒）
      //  unsigned long frequency = count/currentinterval*1000; // 频率 = 计数值（赫兹）
      sprintf(freqencyStr, "%d", count * 8); // 清零计数
      pcnt_counter_clear(PCNT_UNIT);         // 更新数据
      previousMillis = currentMillis;        // 更新上次读取计数的时间
    }
  }
}

// 屏幕打点函数
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
  const int32_t x1 = max<int32_t>(area->x1, 0);
  const int32_t y1 = max<int32_t>(area->y1, 0);
  const int32_t x2 = min<int32_t>(area->x2, LCD_WIDTH - 1);
  const int32_t y2 = min<int32_t>(area->y2, LCD_HEIGHT - 1);
  if (x1 > x2 || y1 > y2)
  {
    lv_disp_flush_ready(disp);
    return;
  }

  // Arduino_GFX's CO5300 implementation emits the proper QSPI command/window
  // sequence.  Pass LVGL's contiguous dirty rectangle through unchanged so
  // there is no non-uniform software scaling or stale-pixel overlap.
  const uint32_t source_width = (uint32_t)(area->x2 - area->x1 + 1);
  const uint16_t *source = reinterpret_cast<const uint16_t *>(color_p);
  const int16_t draw_width = (int16_t)(x2 - x1 + 1);
  const int16_t draw_height = (int16_t)(y2 - y1 + 1);
  if (x1 == area->x1 && y1 == area->y1 &&
      x2 == area->x2 && y2 == area->y2)
  {
    tft->draw16bitRGBBitmap(x1, y1, const_cast<uint16_t *>(source),
                            draw_width, draw_height);
  }
  else
  {
    // Retain the source stride when clipping, but never submit 1-row windows:
    // RASET requires paired rows too. Normal LVGL areas are already clamped
    // by rounder_cb; this packing path is defensive for externally supplied
    // out-of-bounds dirty rectangles.
    if ((x1 | y1 | draw_width | draw_height) & 3)
    {
      APP_LOGE("lcd", "reject unaligned flush %ld,%ld %dx%d\r\n",
               (long)x1, (long)y1, (int)draw_width, (int)draw_height);
      lv_disp_flush_ready(disp);
      return;
    }
    const uint32_t x_offset = (uint32_t)(x1 - area->x1);
    for (int32_t row = y1; row <= y2; row += 4)
    {
      for (uint32_t n = 0; n < 4; ++n)
      {
        const uint32_t source_row = (uint32_t)(row + n - area->y1);
        memcpy(lcd_transfer_rows + n * draw_width,
               source + source_row * source_width + x_offset,
               draw_width * sizeof(uint16_t));
      }
      tft->draw16bitRGBBitmap(x1, row, lcd_transfer_rows, draw_width, 4);
    }
  }

  lv_disp_flush_ready(disp);
}

static void clear_touch_contact()
{
  touch_contact_down = false;
  touch_contact_cancelled = false;
  touch_contact_sample_ms = 0;
  suppress_touch_until_release = true;
  edge_back_gesture.update(false, 0, 0, false);
}

// Consume a real sample once. Reading the cache again must never extend the
// lifetime of a contact or turn an old touch into fresh user activity.
static bool consume_touch_sample(bool idle, bool gpio_woke = false)
{
  exlink::TouchSample sample;
  if (!internal_io.takeTouch(sample)) return false;
  touch_consumed_sequence = sample.sequence;
  if (sample.down && millis() - sample.sampled_ms < 75U) {
    touch_contact_x = sample.x;
    touch_contact_y = sample.y;
    touch_contact_down = true;
    touch_contact_sample_ms = sample.sampled_ms;
    if (idle) suppress_touch_until_release = true;
    notify_user_activity();
    static uint32_t last_touch_log_ms = 0;
    if (millis() - last_touch_log_ms >= 250U) {
      last_touch_log_ms = millis();
      APP_LOGI("touch", "point x=%u y=%u int=%d (cached)\r\n",
               unsigned(sample.x), unsigned(sample.y), digitalRead(INT_N_PIN));
    }
  } else if (sample.cancelled) {
    touch_contact_cancelled = touch_contact_down;
    touch_contact_down = false;
  } else if (sample.released) {
    if (idle && (gpio_woke || touch_contact_down)) notify_user_activity();
    touch_contact_down = false;
  }
  return true;
}

// LVGL input never performs bus transactions or waits for the I2C task.
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  LV_UNUSED(indev_driver);
  if (!touch_input_enabled)
  {
    data->state = LV_INDEV_STATE_REL;
    data->point.x = data->point.y = 0;
    return;
  }
  consume_touch_sample(false);
  if (touch_contact_down && millis() - touch_contact_sample_ms >= 75U) {
    touch_contact_cancelled = true;
    touch_contact_down = false;
  }
  // Preserve a quick down/up pair queued between two LVGL polls.
  data->continue_reading = internal_io.touchPending();
  data->point.x = touch_contact_x;
  data->point.y = touch_contact_y;
  data->state = LV_INDEV_STATE_REL;
  if (touch_contact_cancelled) {
    touch_contact_cancelled = false;
    lv_indev_t *pointer = lv_indev_get_act();
    // A missing report is not a tap. Keep LVGL's normal scroll release/throw,
    // but cancel an unscrolled press so it cannot open a menu on a bus timeout.
    if (pointer && !lv_indev_get_scroll_obj(pointer))
      exlink::cancelLvglPointer(pointer);
    edge_back_gesture.update(false, 0, 0, false);
    return; // Finish this cancellation before accepting the next queued down.
  }
  if (display_idle_active || suppress_touch_until_release)
  {
    suppress_touch_until_release = touch_contact_down;
    edge_back_gesture.update(false, 0, 0, false);
    return;
  }
  const EdgeBackGesture::Result route = edge_back_gesture.update(
      touch_contact_down, touch_contact_x, touch_contact_y, ui_edge_back_allowed());
  if (route == EdgeBackGesture::Result::Back)
  {
    ui_request_back();
    return;
  }
  if (route == EdgeBackGesture::Result::Forward && touch_contact_down)
  {
    data->state = LV_INDEV_STATE_PR;
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
}

// 定时器中断服务函数
void tim1Interrupt()
{
  lv_tick_inc(1);
}

// lvgl任务处理函数
void lvgl_handler(void *pvParameters)
{
  while (1)
  {
    lv_timer_handler(); /* let the GUI do its work */
    delay(5);
  }
}

static void apply_display_brightness()
{
  if (!co5300_ready) return;
  const uint8_t raw = (unsigned(display_brightness_percent) * 255U + 50U) / 100U;
  tft->Display_Brightness(raw);
  APP_LOGI("lcd", "brightness=%u%% raw=%u\r\n", unsigned(display_brightness_percent), unsigned(raw));
}

extern "C" uint8_t get_display_brightness_percent(void)
{
  return display_brightness_percent;
}

extern "C" void set_display_brightness_percent(uint8_t percent)
{
  percent = constrain(percent, 5, 100);
  if (percent == display_brightness_percent) return;
  display_brightness_percent = percent;
  apply_display_brightness();
  // Only selection changes write NVS; no writes in rendering or sensor tasks.
  prefs.begin("usolve", false);
  prefs.putUChar("brightpct", percent);
  prefs.end();
}

extern "C" void save_persistent_settings(void)
{
  prefs.begin("usolve", false);
  prefs.putInt("p11v", constrain(power_preset_11v, 1, 126));
  prefs.putInt("p5v", constrain(power_preset_5v, 1, 126));
  prefs.putInt("p3v3", constrain(power_preset_3v3, 1, 126));
  prefs.putInt("pboot", constrain(power_boot_preset, 1, 126));
  prefs.putInt("pwmf", max(pwm_Freq, 0));
  prefs.putInt("pwmd", constrain(pwm_Duty, 0, 100));
  prefs.putInt("servo", constrain(servo_angle, 0, 180));
  prefs.putFloat("buzz", constrain(buzzer_volume_percent, 0.0f, 100.0f));
  prefs.putLong("baud", currentBaudRate);
  prefs.putBool("logen", log_config_enabled);
  prefs.putInt("logch", (int)log_config_channel);
  prefs.putInt("loglv", (int)log_config_level);
  prefs.putBool("bleusb", ble_usb_passthrough_enabled);
  prefs.putBool("autosleep", screen_sleep_enabled);
  prefs.putUInt("idlesec", screen_idle_timeout_s);
  prefs.putUInt("photosec", screen_photo_duration_s);
  prefs.putUChar("brightpct", display_brightness_percent);
  prefs.putUInt("bleadvsec", bleAdvertisingTimeoutS);
  prefs.putUInt("bleidle", bleConnectionTimeoutS);
  prefs.end();
}

extern "C" void load_persistent_settings(void)
{
  prefs.begin("usolve", true);
  power_preset_11v = constrain(prefs.getInt("p11v", power_preset_11v), 1, 126);
  power_preset_5v = constrain(prefs.getInt("p5v", power_preset_5v), 1, 126);
  power_preset_3v3 = constrain(prefs.getInt("p3v3", power_preset_3v3), 1, 126);
  power_boot_preset = constrain(prefs.getInt("pboot", power_boot_preset), 1, 126);
  pwm_Freq = max(prefs.getInt("pwmf", pwm_Freq), 0);
  pwm_Duty = constrain(prefs.getInt("pwmd", pwm_Duty), 0, 100);
  servo_angle = constrain(prefs.getInt("servo", servo_angle), 0, 180);
  buzzer_volume_percent = constrain(prefs.getFloat("buzz", buzzer_volume_percent), 0.0f, 100.0f);
  currentBaudRate = prefs.getLong("baud", currentBaudRate);
  log_config_enabled = prefs.getBool("logen", log_config_enabled);
  log_config_channel = sanitize_log_channel(prefs.getInt("logch", (int)log_config_channel));
  log_config_level = sanitize_log_level(prefs.getInt("loglv", (int)log_config_level));
  ble_usb_passthrough_enabled = prefs.getBool("bleusb", ble_usb_passthrough_enabled);
  screen_sleep_enabled = prefs.getBool("autosleep", screen_sleep_enabled);
  screen_idle_timeout_s = sanitize_screen_idle_seconds(prefs.getUInt("idlesec", 30));
  screen_photo_duration_s = sanitize_screen_photo_seconds(prefs.getUInt("photosec", 5));
  display_brightness_percent = constrain(prefs.getUChar("brightpct", 100), 5, 100);
  bleAdvertisingTimeoutS = constrain(prefs.getUInt("bleadvsec", 120), 30U, 300U);
  bleConnectionTimeoutS = constrain(prefs.getUInt("bleidle", 120), 60U, 600U);
  prefs.end();

  apply_configured_log_state();
}

void servo_task()
{
  if (servo_flag == 1)
  {
    pwm_flag = 0;

    if (!servo_ledc_ready)
    {
      if (configure_pwm_channel(3, 50, servo_resolution_bits))
      {
        ledcAttachPin(5, 3);
        servo_ledc_ready = true;
        new_servo_angle = -1;
      }
      else
      {
        return;
      }
    }

    int clamped_angle = constrain(servo_angle, 0, 180);
    if (clamped_angle != new_servo_angle)
    {
      const uint32_t max_duty = (1U << servo_resolution_bits) - 1U;
      const float pulse_us = 500.0f + (2000.0f * clamped_angle / 180.0f);
      const uint32_t duty_value = (uint32_t)((pulse_us / 20000.0f) * max_duty + 0.5f);
      ledcWrite(3, duty_value);
      new_servo_angle = clamped_angle;
    }
  }
  else
  {
    new_servo_angle = -1;
    if (servo_ledc_ready)
    {
      ledcWrite(3, 0);
      // PWM and the frequency counter can take GPIO5 while Servo is stopped.
      servo_ledc_ready = false;
    }
  }
}

void servo_encoder_task()
{
  // 舵机滚轮控制已接入 LVGL 焦点/编辑态，这里保留空实现避免旧逻辑与新交互重复生效。
}

static void poll_idle_inputs(bool gpio_woke = false)
{
  if (any_key_down())
  {
    suppress_keys_until_release = true;
    notify_user_activity();
  }
  if (!touch_input_enabled) return;
  const uint32_t now = millis();
  if (now - idle_touch_poll_ms < 16U) return;
  idle_touch_poll_ms = now;
  for (unsigned i = 0; i < 16 && consume_touch_sample(true, gpio_woke); ++i) {}
  if (now - touch_contact_sample_ms >= 75U) touch_contact_down = false;
}

struct IdleRows
{
  unsigned used = 0;
  int next_y = 168;
};

static bool idle_submit_row(IdleRows *rows, const uint16_t *pixels, unsigned width)
{
  uint16_t *out = lcd_transfer_rows + rows->used * LCD_WIDTH;
  memset(out, 0, LCD_WIDTH * sizeof(uint16_t));
  if (pixels != nullptr && width == 125)
  {
    // Match the tested ESP-VoCat layout: left x=70, auto-mirrored right x=285,
    // eye y=170..329. Do not enlarge one eye into a full-screen picture.
    for (unsigned n = 0; n < width; ++n)
    {
      out[70 + n] = pixels[n];
      out[285 + n] = pixels[width - 1 - n];
    }
  }
  if (++rows->used == 4)
  {
    poll_idle_inputs();
    if (display_resume_pending) return false;
    tft->draw16bitRGBBitmap(0, rows->next_y, lcd_transfer_rows, LCD_WIDTH, 4);
    rows->next_y += 4;
    rows->used = 0;
  }
  return true;
}

static bool idle_decode_row(void *user, uint16_t y, const uint16_t *rgb565, uint16_t width)
{
  LV_UNUSED(y);
  return idle_submit_row(static_cast<IdleRows *>(user), rgb565, width);
}

struct CustomIdleRows
{
  unsigned used = 0;
  int next_y = 0;
};

static bool custom_idle_submit_row(CustomIdleRows *rows, const uint16_t *pixels,
                                   unsigned width)
{
  if (rows == nullptr || pixels == nullptr || width != LCD_WIDTH) return false;
  memcpy(lcd_transfer_rows + rows->used * LCD_WIDTH, pixels,
         LCD_WIDTH * sizeof(uint16_t));
  if (++rows->used == 4)
  {
    poll_idle_inputs();
    if (display_resume_pending) return false;
    tft->draw16bitRGBBitmap(0, rows->next_y, lcd_transfer_rows, LCD_WIDTH, 4);
    rows->next_y += 4;
    rows->used = 0;
  }
  return true;
}

static bool custom_idle_decode_row(uint16_t y, const uint16_t *rgb565,
                                   size_t width, void *user)
{
  LV_UNUSED(y);
  if (width != LCD_WIDTH) return false;
  return custom_idle_submit_row(static_cast<CustomIdleRows *>(user), rgb565,
                                static_cast<unsigned>(width));
}

static void restore_active_display(bool was_sleeping)
{
  if (was_sleeping)
  {
    // Render the UI after SLPOUT but before DISPON, preventing stale eyes/GRAM
    // from flashing during wake. The panel and touch power rails stay intact.
    co5300_bus->sendCommand(CO5300_C_SLPOUT);
    delay(120);
    apply_display_brightness();
  }
  display_idle_active = false;
  display_resume_pending = false;
  touch_contact_cancelled = false;
  suppress_touch_until_release = touch_contact_down;
  suppress_keys_until_release = any_key_down();
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
  if (was_sleeping) co5300_bus->sendCommand(CO5300_C_DISPON);
  last_user_activity_ms = millis();
}

static bool runtime_prevents_sleep()
{
  const RuntimeActivity activity = {
    digitalRead(1) == HIGH,
    ina266_flag != 0,
    pwm_flag != 0 || servo_flag != 0,
    DSO_flag != 0 || FREcount_flag != 0,
    uart_helper_flag != 0 || uartHelperBridgeRunning ||
        i2cscan_flag != 0 || i2cActiveMode != I2cTransportMode::None ||
        spi_bridge_flag != 0 || spiHelperBridgeRunning,
    deviceConnected || (bleBridgeRunning && bleUsbPassthroughActive),
    exlink::bleUpdateBusy()
  };
  return blocks_automatic_sleep(activity);
}

static void enter_display_sleep()
{
  poll_idle_inputs();
  if (display_resume_pending || runtime_prevents_sleep())
  {
    restore_active_display(false);
    return;
  }
  const int saved_power_output = digitalRead(1);
  const uint32_t saved_pwm = pwm_ledc_ready ? ledcRead(2) : 0;
  const uint32_t saved_servo = servo_ledc_ready ? ledcRead(3) : 0;
  digitalWrite(1, LOW); // user requested power output OFF in sleep
  if (pwm_ledc_ready) ledcWrite(2, 0);
  if (servo_ledc_ready) ledcWrite(3, 0);
  ledcWrite(1, 0);
  buzzer_feedback_active = false;
  if (!internal_io.pause(true)) {
    internal_io.resume();
    digitalWrite(1, saved_power_output);
    if (pwm_ledc_ready) ledcWrite(2, saved_pwm);
    if (servo_ledc_ready) ledcWrite(3, saved_servo);
    restore_active_display(false);
    APP_LOGW("sleep", "I2C transaction pending; defer sleep\r\n");
    return;
  }
  clear_touch_contact();
  requestedBleMode = BleSessionMode::Off;
  BluetoothSerial_flag = 0;
  // Finish the asynchronous disconnect before suspending the BLE host. This
  // also handles the default Off case, where BLE has never been initialized.
  const uint32_t closeStarted = millis();
  do { device_ble_task(); if (deviceBleRadioRunning) delay(10); }
  while (deviceBleRadioRunning && millis() - closeStarted < 2000U);
  const bool bleStopped = !deviceBleRadioRunning;
  if (!bleStopped) {
    internal_io.resume();
    digitalWrite(1, saved_power_output);
    if (pwm_ledc_ready) ledcWrite(2, saved_pwm);
    if (servo_ledc_ready) ledcWrite(3, saved_servo);
    restore_active_display(false);
    APP_LOGW("sleep", "BLE disconnect pending; defer sleep\r\n");
    return;
  }
  co5300_bus->sendCommand(CO5300_C_DISPOFF);
  delay(20);
  co5300_bus->sendCommand(CO5300_C_SLPIN);
  delay(120);
  APP_LOGI("sleep", "panel off; light-sleep, %s wake; power output OFF\r\n",
           touch_input_enabled ? "touch/key" : "key only");
  timerAlarmDisable(tim1);

  const gpio_num_t wake_pins[] = {
#if EXLINK_TOUCH_ENABLED
    (gpio_num_t)INT_N_PIN,
#endif
    (gpio_num_t)RIGHT_PIN, (gpio_num_t)LEFT_PIN, (gpio_num_t)PUSH_PIN};
  bool wake_config_ok = bleStopped;
  for (gpio_num_t pin : wake_pins)
    wake_config_ok = gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL) == ESP_OK && wake_config_ok;
  wake_config_ok = esp_sleep_enable_gpio_wakeup() == ESP_OK && wake_config_ok;
  // Backup RTC polling: a missed short IRQ must not make touch wake impossible.
  // These timer wakeups remain dark and do not run the normal application loop.
  wake_config_ok = esp_sleep_enable_timer_wakeup(30000ULL) == ESP_OK && wake_config_ok;
  if (!wake_config_ok) APP_LOGW("sleep", "wake configuration failed; restoring UI\r\n");
  while (wake_config_ok && !display_resume_pending)
  {
    const esp_err_t error = esp_light_sleep_start();
    // A paused worker has no fresh touch cache. Resume it for a new IRQ read
    // before deciding whether this wake was user input or just the RTC timer.
    const uint32_t wake_sequence = touch_consumed_sequence;
    internal_io.resume();
    const bool gpio_woke = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
    const uint32_t wake_started = millis();
    idle_touch_poll_ms = millis() - 16U;
    do {
      poll_idle_inputs(gpio_woke);
      if (display_resume_pending || !touch_input_enabled ||
          (touch_consumed_sequence != wake_sequence && !internal_io.touchPending())) break;
      delay(2);
    } while (millis() - wake_started < 100U);
    if (!display_resume_pending) {
      if (!internal_io.pause(true)) {
        wake_config_ok = false;
        APP_LOGW("sleep", "I2C pause timed out; waking UI\r\n");
      }
      clear_touch_contact();
    }
    if (error != ESP_OK && !display_resume_pending) delay(30);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  for (gpio_num_t pin : wake_pins) gpio_wakeup_disable(pin);
  timerAlarmEnable(tim1);
  internal_io.resume();
  restore_active_display(true);
  digitalWrite(1, saved_power_output);
  if (pwm_ledc_ready) ledcWrite(2, saved_pwm);
  if (servo_ledc_ready) ledcWrite(3, saved_servo);
  // Wake restores local functions; BLE stays Off until explicitly enabled.
  APP_LOGI("sleep", "awake; original page/output state restored\r\n");
}

static void display_idle_task()
{
#if EXLINK_POWER_DIAGNOSTICS
  // Keep the manual test visible without changing the saved sleep preference.
  return;
#endif
  if (runtime_prevents_sleep())
  {
    // Stopping a function starts a complete idle interval. Do not count time
    // spent measuring/outputting as inactivity, or sleep can cut work short.
    last_user_activity_ms = millis();
    if (display_idle_active) restore_active_display(false);
    return;
  }
  if (!display_idle_active)
  {
    if (!screen_sleep_enabled || !idle_timeout_elapsed(millis(), last_user_activity_ms, screen_idle_timeout_s)) return;
    display_idle_active = true;
    idle_custom_active = exlink::screenAssetAvailable();
    idle_play_started_ms = millis();
    idle_last_expression = -1;
    idle_last_frame = UINT16_MAX;
    tft->fillScreen(BLACK);
    APP_LOGI("sleep", idle_custom_active ?
             "idle: custom EXA1 screensaver\r\n" :
             "idle: 5 official expressions, 5s each\r\n");
  }
  poll_idle_inputs();
  if (display_resume_pending || !screen_sleep_enabled || screen_idle_timeout_s == 0)
  {
    restore_active_display(false);
    return;
  }
  const uint32_t elapsed = millis() - idle_play_started_ms;
  const uint32_t idle_duration = idle_custom_active ? screensaver_duration_ms(
      exlink::screenAssetFrameCount(), exlink::screenAssetDurationMs(), screen_photo_duration_s) : 25000U;
  if (idle_duration == 0 || elapsed >= idle_duration)
  {
    enter_display_sleep();
    return;
  }
  if (idle_custom_active)
  {
    const uint16_t frame = exlink::screenAssetFrameForElapsed(elapsed);
    if (frame == idle_last_frame) return;
    CustomIdleRows rows;
    const bool ok = exlink::screenAssetDecodeFrameRows(frame, idle_custom_source_row,
                                               LCD_WIDTH, custom_idle_decode_row, &rows) &&
                    rows.used == 0;
    if (!ok)
    {
      if (!display_resume_pending) APP_LOGW("sleep", "custom asset decode failed; stay awake\r\n");
      restore_active_display(false);
      return;
    }
    idle_last_frame = frame;
    return;
  }
  const int expression_index = elapsed / 5000U;
  const auto expression = static_cast<idle_emote::Emote>(expression_index);
  const uint16_t frame = idle_emote::frameForElapsed(expression, elapsed % 5000U);
  if (expression_index == idle_last_expression && frame == idle_last_frame) return;
  IdleRows rows;
  // 2 black rows above/below preserve y=170 while making y=168/h=164 aligned.
  bool ok = idle_submit_row(&rows, nullptr, 0) && idle_submit_row(&rows, nullptr, 0);
  if (ok) ok = idle_emote::decodeFrameRows(expression, frame, idle_source_row, 125, idle_decode_row, &rows);
  if (ok) ok = idle_submit_row(&rows, nullptr, 0) && idle_submit_row(&rows, nullptr, 0);
  if (!ok)
  {
    if (!display_resume_pending) APP_LOGW("sleep", "expression decode failed; stay awake\r\n");
    restore_active_display(false);
    return;
  }
  idle_last_expression = expression_index;
  idle_last_frame = frame;
}

void setup()
{
  esp_task_wdt_delete(NULL);  // 删除当前任务的看门狗
  esp_ota_img_states_t startupOtaState;
  const esp_partition_t *startupApp = esp_ota_get_running_partition();
  const bool pendingStartup = startupApp &&
      esp_ota_get_state_partition(startupApp, &startupOtaState) == ESP_OK &&
      startupOtaState == ESP_OTA_IMG_PENDING_VERIFY;
  if (pendingStartup) {
    // A pending image that hangs before health confirmation must reset so the
    // rollback-enabled bootloader can reject it on the following boot.
    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);
  }
  install_log_filter_once();
  load_persistent_settings();
  Serial.begin(currentBaudRate);
  route_boot_output_to_usb();
  APP_LOGI("boot", "native USB debug active on GPIO19/GPIO20\r\n");
  APP_LOGI("boot", "reset reason=%u (3=software, 4=panic, 6=task watchdog, 9=brownout); build=%s %s\r\n",
           unsigned(esp_reset_reason()), __DATE__, __TIME__);
  APP_LOGI("boot", "ui mode = full\r\n");
  APP_LOGI("boot", "firmware=%s touch=%s\r\n",
           esp_ota_get_app_description()->version,
           touch_input_enabled ? "enabled" : "disabled (OTA test)");
  APP_LOGI("i2c", "driver=%s\r\n", exlink_i2c_timeout_backport());
#if EXLINK_POWER_DIAGNOSTICS
  APP_LOGI("power.diag", "manual test; USB INFO logs; auto-sleep paused; no automatic voltage sweep\r\n");
#endif
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);
  pinMode(3, OUTPUT);
  if (ledcSetup(1, 1000, 8) > 0)
  {
    ledcAttachPin(3, 1);
    ledcWrite(1, 0);
    buzzer_ledc_ready = true;
  }
  pinMode(RIGHT_PIN, INPUT_PULLUP);
  pinMode(LEFT_PIN, INPUT_PULLUP);
  pinMode(PUSH_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(2, ADC_11db);
  analogSetPinAttenuation(4, ADC_11db);
  co5300_power_sequence();
  if (!tft->begin(40000000))
  {
    APP_LOGE("lcd", "CO5300 QSPI initialization failed\r\n");
    delay(250);
    ESP.restart();
  }
  co5300_ready = true;
  apply_display_brightness();
  tft->setRotation(0);
  tft->fillScreen(BLACK);

  lv_init();
  if (!allocate_lvgl_draw_buffer())
  {
    delay(250);
    ESP.restart();
  }
  lv_disp_draw_buf_init(&draw_buf, draw_buf_pixels, NULL, draw_buf_pixel_count);
  static lv_disp_drv_t disp_drv; // 初始化显示器

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LVGL_WIDTH;
  disp_drv.ver_res = LVGL_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.rounder_cb = co5300_rounder;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.sw_rotate = 0;             // 屏幕镜像
  disp_drv.rotated = LV_DISP_ROT_NONE; // 屏幕旋转
  lv_disp_drv_register(&disp_drv);    // 注册显示屏
  APP_LOGI("lvgl", "native canvas %ux%u -> panel %ux%u (no flush scaling)\r\n",
           (unsigned)LVGL_WIDTH, (unsigned)LVGL_HEIGHT,
           (unsigned)LCD_WIDTH, (unsigned)LCD_HEIGHT);

  if (touch_input_enabled)
  {
    static lv_indev_drv_t indev_drv1;
    lv_indev_drv_init(&indev_drv1);
    indev_drv1.type = LV_INDEV_TYPE_POINTER; // 设置为触摸屏类型
    indev_drv1.read_cb = my_touchpad_read;   // 注册回调函数
    lv_indev_drv_register(&indev_drv1);      // 注册输入设备
  }

  static lv_indev_drv_t indev_drv2;
  lv_indev_drv_init(&indev_drv2);
  indev_drv2.read_cb = keypad_read;                  // 注册回调函数
  indev_drv2.type = LV_INDEV_TYPE_KEYPAD;            // 设置为按键类型
  indev_keypad = lv_indev_drv_register(&indev_drv2); // 注册输入设备

  // 启动定时器为lvgl提供心跳时钟
  tim1 = timerBegin(0, 80, true);
  timerAttachInterrupt(tim1, tim1Interrupt, true);
  timerAlarmWrite(tim1, 1000, true);
  timerAlarmEnable(tim1);

  ledcWrite(1, 3);
  const bool startupTouchReady = touch_input_enabled && cst9220.begin(INTERNAL_I2C_HZ);
  if (!touch_input_enabled)
  {
    // Keep the controller in reset; the shared INA I2C bus remains active.
    pinMode(RST_N_PIN, OUTPUT);
    digitalWrite(RST_N_PIN, LOW);
    pinMode(INT_N_PIN, INPUT);
    APP_LOGI("touch", "OTA test: CST9220 input and wake disabled; use physical keys\r\n");
  }
  else if (!startupTouchReady)
  {
    APP_LOGW("touch", "CST9220 did not ACK at 0x5A or 0x64; touch input disabled\r\n");
  }
  else
  {
    APP_LOGI("touch", "CST9220 ready: addr=0x%02X protocol=%s SDA=GPIO%d SCL=GPIO%d RST=GPIO%d INT=GPIO%d\r\n",
             cst9220.address(), cst9220.protocolName(), I2C_SDA, I2C_SCL,
             RST_N_PIN, INT_N_PIN);
  }
  // INA and CST9220 share the board's internal I2C pins.
  Wire.begin(INTERNAL_I2C_SDA, INTERNAL_I2C_SCL, INTERNAL_I2C_HZ);
  Wire1.begin(EXTERNAL_I2C_SDA, EXTERNAL_I2C_SCL);
  uint8_t devicesFound = 0;
  while (deviceNumber == UINT8_MAX)
  {
    devicesFound = INA.begin(10, 10000);
    if (devicesFound > 0)
    {
      APP_LOGI("ina", "%s\r\n", INA.getDeviceName(devicesFound - 1));
    }
    for (uint8_t i = 0; i < devicesFound; i++)
    {

      if (strcmp(INA.getDeviceName(i), "INA226") == 0)
      {
        deviceNumber = i;
        INA.reset(deviceNumber);

        break;
      }
    }
    if (deviceNumber == UINT8_MAX)
    {
      APP_LOGW("ina", "No INA found. Waiting 5s and retrying...\r\n");
      delay(5000);
    }
  }
  APP_LOGI("ina", "Found INA at device number %u\r\n\r\n", (unsigned)deviceNumber);
  INA.setAveraging(4, deviceNumber);
  INA.setBusConversion(8244, deviceNumber);            // 最大转换时间8.244ms
  INA.setShuntConversion(8244, deviceNumber);          // 最大转换时间8.244ms
  INA.setMode(INA_MODE_CONTINUOUS_BOTH, deviceNumber); // 连续转换模式

  // Wire.begin() leaves an already initialized master on its existing pins.
  // Finish all startup transactions before handing this bus to its worker.
  Wire.begin(INTERNAL_I2C_SDA, INTERNAL_I2C_SCL, INTERNAL_I2C_HZ);
  APP_LOGI("i2c", "Internal I2C bus ready on SDA=GPIO%d SCL=GPIO%d clock=%luHz\r\n",
           INTERNAL_I2C_SDA, INTERNAL_I2C_SCL, (unsigned long)Wire.getClock());

  APP_LOGI("ui", "ui_init start\r\n");
  ui_init(); // 初始化ui界面
  APP_LOGI("ui", "ui_init done\r\n");
  APP_LOGI("boot", "Setup done\r\n");
  mcp4017_init();
  internal_io_started = internal_io.begin(touch_input_enabled, deviceNumber);
  if (!internal_io_started) {
    APP_LOGE("i2c", "worker/bus setup failed; runtime I2C disabled\r\n");
  } else {
    APP_LOGI("i2c", "single-owner worker ready; LVGL uses touch cache; IRQ + error backoff\r\n");
  }
  // Keep runtime diagnostics on the native USB monitor. GPIO19/GPIO20 are
  // the USB Hub D-/D+ pair and are intentionally not treated as UART pins.
  route_boot_output_to_usb();
  APP_LOGI("boot", "Setup done; native USB logs active @ %ld baud\r\n", currentBaudRate);
  ledcWrite(1, 0);
  last_user_activity_ms = millis();
  APP_LOGI("ble", "radio OFF at boot; enable Phone connection in Settings or BLE Serial\r\n");
  esp_ota_img_states_t otaState;
  const esp_partition_t *runningApp = esp_ota_get_running_partition();
  if (runningApp && esp_ota_get_state_partition(runningApp, &otaState) == ESP_OK &&
      otaState == ESP_OTA_IMG_PENDING_VERIFY)
  {
    const bool healthy = internal_io_started && (!touch_input_enabled || startupTouchReady) && deviceNumber != UINT8_MAX &&
                         draw_buf_pixels && ESP.getFreeHeap() > 16000;
    if (healthy) esp_ota_mark_app_valid_cancel_rollback();
    else esp_ota_mark_app_invalid_rollback_and_reboot();
  }
  if (pendingStartup) esp_task_wdt_delete(NULL);
}

static volatile uint32_t updateConsentUntil;
static lv_obj_t *update_consent_message;
static lv_obj_t *update_progress_panel;
static lv_obj_t *update_progress_title;
static lv_obj_t *update_progress_status;
static lv_obj_t *update_progress_detail;
static lv_obj_t *update_progress_bar;
static uint32_t update_progress_refresh_ms;
static uint32_t update_progress_hide_at;
static int update_progress_percent = -1;
static bool update_progress_paused;
static bool update_progress_active;
static exlink::UpdateState update_progress_state = exlink::UpdateState::Idle;
static exlink::UpdateResult update_progress_result = exlink::UpdateResult::Ok;

static void clear_update_consent_message()
{
  if (update_consent_message && lv_obj_is_valid(update_consent_message))
    lv_msgbox_close(update_consent_message);
  update_consent_message = nullptr;
}

static void update_progress_ui_task()
{
  if (update_progress_panel && update_progress_hide_at &&
      int32_t(millis() - update_progress_hide_at) >= 0) {
    lv_obj_del(update_progress_panel);
    update_progress_panel = nullptr;
    update_progress_hide_at = 0;
    lv_obj_invalidate(lv_scr_act());
  }
}
static void poll_update_consent()
{
  static uint32_t pressedAt;
  static bool issued;
  if (digitalRead(PUSH_PIN) != LOW) { pressedAt = 0; issued = false; return; }
  if (!pressedAt) pressedAt = millis();
  if (!issued && millis() - pressedAt >= 3000U)
  {
    issued = true;
    if (!deviceConnected) return;
    updateConsentUntil = millis() + 120000U;
    if (display_idle_active) restore_active_display(false);
    last_user_activity_ms = millis();
    static const char *buttons[] = {"OK", ""};
    clear_update_consent_message();
    update_consent_message = lv_msgbox_create(nullptr, "BLE UPDATE", "Phone update allowed for 120 seconds. Outputs pause during transfer.", buttons, true);
    lv_obj_center(update_consent_message);
    APP_LOGI("update", "local consent: BEGIN permitted for 120s\r\n");
  }
}

static void sensor_diagnostics_task()
{
  static uint32_t last_ms = 0;
  const uint32_t now = millis();
  if (display_idle_active || (!level_page_active && !environment_page_active) ||
      uint32_t(now - last_ms) < 1000U) return;
  last_ms = now;
  if (level_page_active) {
    const exlink::LevelSample s = internal_io.levelSample();
    APP_LOGI("level", "addr=0x%02X id=0x%02X cfg=%u raw=%d/%d/%d gyro=%d/%d/%d g=%.3f valid=%u stable=%u age=%lums reg=%02X:%02X\r\n",
             s.address, s.who_am_i, unsigned(s.configured),
             s.accel_raw[0], s.accel_raw[1], s.accel_raw[2],
             s.gyro_raw[0], s.gyro_raw[1], s.gyro_raw[2], double(s.gravity_g),
             unsigned(s.available), unsigned(s.stable),
             (unsigned long)(now - s.sampled_ms), s.error_register, s.error_value);
  }
  if (environment_page_active) {
    const exlink::Aht20Sample s = internal_io.environmentSample();
    APP_LOGI("environment", "AHT20 status=0x%02X mode=%u error=%u valid=%u T=%.1fC RH=%.1f%% raw=%02X/%02X/%02X/%02X/%02X/%02X/%02X age=%lums\r\n",
             s.status, unsigned(s.status_method), unsigned(s.error), unsigned(s.available),
             double(s.temperature_c), double(s.humidity_rh),
             s.raw[0], s.raw[1], s.raw[2], s.raw[3], s.raw[4], s.raw[5], s.raw[6],
             (unsigned long)(now - s.sampled_ms));
  }
}

void loop()
{
  const uint32_t now = millis();
  if (ui_loop_last_ms && uint32_t(now - ui_loop_last_ms) > ui_loop_max_gap_ms)
    ui_loop_max_gap_ms = now - ui_loop_last_ms;
  ui_loop_last_ms = now;
  // Bounded diagnostics for the reported idle/key resets. No extra task or
  // bus operation: correlate uptime, heap/stack reserve and I2C stalls.
  static uint32_t last_health_ms = 0;
  if (uint32_t(now - last_health_ms) >= 10000U) {
    last_health_ms = now;
    const exlink::IoDiagnostics stats = internal_io.diagnostics();
    APP_LOGI("health", "up=%lums heap=%u min_heap=%u loop_stack=%lu ui_gap=%lums io_max=%lums i2c_hz=%lu touch=%lu/%lu recoveries=%lu idle=%u\r\n",
             (unsigned long)now, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL),
             (unsigned long)ui_loop_max_gap_ms, (unsigned long)stats.max_transaction_ms,
             (unsigned long)Wire.getClock(), (unsigned long)stats.touch_errors,
             (unsigned long)stats.touch_reads, (unsigned long)stats.touch_recoveries,
             unsigned(display_idle_active));
    ui_loop_max_gap_ms = 0;
  }
  internal_io_results_task();
  device_ble_task();
  poll_update_consent();
  exlink::bleUpdatePoll();
  update_progress_ui_task();
  if (exlink::bleUpdateBusy()) { delay(1); return; }
  sensor_diagnostics_task();
  display_idle_task();
  // lvgl任务处理函数
  // One QSPI owner: keep UI objects alive beneath the expression layer.
  if (!display_idle_active) lv_task_handler();
  buzzer_update_task();
  // 用户任务处理函数
  ina266_task();
  mcp4017_task();
  power_diagnostics_task();
  pwm_task();
  servo_task();
  servo_encoder_task();
  i2cscan_task();
  spi_bridge_task();
  uart_helper_task();
  BluetoothSerial_task();
  FREcount_task();
}

namespace exlink {
static int savedUpdatePower;
static bool savedUpdateScope;
static uint32_t savedUpdatePwm, savedUpdateServo;
bool updateLocalConsent() { return updateConsentUntil && int32_t(updateConsentUntil - millis()) > 0; }
void updateRevokeConsent() { updateConsentUntil = 0; }
size_t updateReadTelemetry(char *buffer, size_t capacity)
{
  int result = snprintf(buffer, capacity,
      "{\"voltage\":%.3f,\"current\":%.3f,\"power\":%.3f,\"battery\":null,\"output\":%s,\"pwm\":%s,\"servo\":%s,\"updating\":%s}",
      v, a, w, digitalRead(1) ? "true" : "false", pwm_flag ? "true" : "false", servo_flag ? "true" : "false", bleUpdateBusy() ? "true" : "false");
  return result < 0 ? 0 : (size_t(result) < capacity ? size_t(result) : capacity - 1);
}
bool updateQuiesceOutputs()
{
  savedUpdateScope = DSO_flag != 0;
  if (!scope_acq_quiesce()) {
    if (savedUpdateScope && !scope_acq_resume()) DSO_flag = 0;
    APP_LOGW("update", "ADC capture pending; update not started\r\n");
    return false;
  }
  if (!internal_io.pause(false)) {
    internal_io.resume();
    if (savedUpdateScope && !scope_acq_resume()) DSO_flag = 0;
    APP_LOGW("update", "I2C transaction pending; update not started\r\n");
    return false;
  }
  clear_touch_contact();
  if (display_idle_active) restore_active_display(false);
  clear_update_consent_message();
  savedUpdatePower = digitalRead(1);
  savedUpdatePwm = pwm_ledc_ready ? ledcRead(2) : 0;
  savedUpdateServo = servo_ledc_ready ? ledcRead(3) : 0;
  digitalWrite(1, LOW);
  if (pwm_ledc_ready) ledcWrite(2, 0);
  if (servo_ledc_ready) ledcWrite(3, 0);
  APP_LOGI("update", "BLE transfer: output disabled, UI/sleep paused\r\n");
  return true;
}
void updateReportProgress(const UpdateProgressSnapshot &snapshot)
{
  if (!lv_disp_get_default()) return;
  if (!snapshot.active && !update_progress_panel) return;
  const uint32_t now = millis();
  const int percent = snapshot.total ? int(uint64_t(snapshot.accepted) * 100U / snapshot.total) : 0;
  const bool transition = snapshot.state != update_progress_state ||
                          snapshot.paused != update_progress_paused ||
                          snapshot.active != update_progress_active ||
                          snapshot.result != update_progress_result;
  if (update_progress_panel && !transition && percent == update_progress_percent &&
      now - update_progress_refresh_ms < 1000U) return;
  if (update_progress_panel && !transition && now - update_progress_refresh_ms < 200U) return;
  if (!update_progress_panel) {
    update_progress_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(update_progress_panel);
    lv_obj_set_size(update_progress_panel, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(update_progress_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(update_progress_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(update_progress_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(update_progress_panel, LV_OBJ_FLAG_CLICKABLE);
    update_progress_title = lv_label_create(update_progress_panel);
    lv_obj_set_style_text_font(update_progress_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(update_progress_title, lv_color_white(), 0);
    lv_obj_align(update_progress_title, LV_ALIGN_TOP_MID, 0, 125);
    update_progress_status = lv_label_create(update_progress_panel);
    lv_obj_set_style_text_color(update_progress_status, lv_color_white(), 0);
    lv_obj_align(update_progress_status, LV_ALIGN_TOP_MID, 0, 175);
    update_progress_bar = lv_bar_create(update_progress_panel);
    lv_obj_set_size(update_progress_bar, 360, 16);
    lv_obj_align(update_progress_bar, LV_ALIGN_TOP_MID, 0, 225);
    lv_obj_set_style_bg_color(update_progress_bar, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_color(update_progress_bar, lv_color_white(), LV_PART_INDICATOR);
    update_progress_detail = lv_label_create(update_progress_panel);
    lv_obj_set_style_text_color(update_progress_detail, lv_color_white(), 0);
    lv_obj_align(update_progress_detail, LV_ALIGN_TOP_MID, 0, 265);
  }
  lv_label_set_text(update_progress_title, snapshot.kind == UpdateKind::Firmware ? "FIRMWARE UPDATE" : "SCREEN ASSET UPDATE");
  const char *stage = "Receiving";
  if (snapshot.paused) stage = "Link lost - reconnect within 60s";
  else if (snapshot.state == UpdateState::Ready) stage = "Verified - applying update";
  else if (snapshot.state == UpdateState::Failed || (!snapshot.active && snapshot.result != UpdateResult::Ok))
    stage = "Update stopped - retry from App";
  else if (snapshot.result != UpdateResult::Ok) stage = "Retrying transfer";
  else if (snapshot.state == UpdateState::Idle)
    stage = snapshot.accepted && snapshot.accepted == snapshot.total ?
            (snapshot.kind == UpdateKind::Firmware ? "Complete - rebooting" : "Complete") : "Cancelled";
  lv_label_set_text(update_progress_status, stage);
  lv_obj_align(update_progress_status, LV_ALIGN_TOP_MID, 0, 175);
  char detail[80];
  snprintf(detail, sizeof(detail), "%d%%   %lu / %lu KiB", percent,
           (unsigned long)(snapshot.accepted / 1024U), (unsigned long)(snapshot.total / 1024U));
  lv_label_set_text(update_progress_detail, detail);
  lv_obj_align(update_progress_detail, LV_ALIGN_TOP_MID, 0, 265);
  lv_bar_set_value(update_progress_bar, percent, LV_ANIM_OFF);
  update_progress_percent = percent;
  update_progress_paused = snapshot.paused;
  update_progress_active = snapshot.active;
  update_progress_state = snapshot.state;
  update_progress_result = snapshot.result;
  update_progress_refresh_ms = now;
  if (snapshot.active) update_progress_hide_at = 0;
  else if (!update_progress_hide_at) update_progress_hide_at = now + 3000U;
  // Flush only the display; no input callbacks or business timers during OTA.
  lv_refr_now(nullptr);
}
void updateRestoreOutputs()
{
  internal_io.resume();
  if (savedUpdateScope && DSO_flag && !scope_acq_resume()) DSO_flag = 0;
  clear_touch_contact();
  digitalWrite(1, savedUpdatePower);
  if (pwm_ledc_ready) ledcWrite(2, savedUpdatePwm);
  if (servo_ledc_ready) ledcWrite(3, savedUpdateServo);
  last_user_activity_ms = millis();
  APP_LOGI("update", "BLE transfer ended: output state restored\r\n");
}
}
