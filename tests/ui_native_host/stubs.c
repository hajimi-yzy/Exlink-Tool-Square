/* Hardware and event actions only. UI construction uses complete ui.c. */
#include "ui.h"
#include "event.h"
#include "scope_capture.h"
#include "level_page.h"
#include "environment_page.h"

lv_indev_t *indev_keypad;
int ina266_flag, mcp4107_flag, pwm_flag, i2cscan_flag, spi_bridge_flag;
int uart_helper_flag, BluetoothSerial_flag, FREcount_flag, DSO_flag;
bool ina266_sample_valid, screen_sleep_enabled = true;
uint32_t screen_idle_timeout_s = 60, screen_photo_duration_s = 10;
char voltageStr[20] = "0.00", currentStr[20] = "0.00";
char powerStr[20] = "0.00", mAHStr[20] = "0.00", freqencyStr[20] = "0";
float buzzer_volume_percent = 20;
int pwm_Freq = 1000, pwm_Duty = 50, servo_flag, servo_angle = 90;
int power_preset_11v = 100, power_preset_5v = 50, power_preset_3v3 = 30;
int power_boot_preset = 30;
long currentBaudRate = 115200;
double v, a, w;
bool level_sensor_read(level_reading_t *reading)
{
    *reading = (level_reading_t){true, true, 7.5f, -3.2f, 0x68, 0x68, true, true};
    return true;
}
void level_sensor_set_active(bool active) { (void)active; }
bool level_sensor_zero(void) { return true; }
bool environment_sensor_read(environment_reading_t *reading)
{
    *reading = (environment_reading_t){true, true, 25.6f, 58.2f, ENVIRONMENT_ERROR_NONE};
    return true;
}
void environment_sensor_set_active(bool active) { (void)active; }

void ina266_reset_measurement(void) {}
void save_persistent_settings(void) {}
void release_transport_page_handles(void) {}
uint8_t get_display_brightness_percent(void) { return 70; }
void set_display_brightness_percent(uint8_t value) { (void)value; }
bool get_app_log_enabled(void) { return true; }
app_log_channel_t get_app_log_channel(void) { return APP_LOG_CHANNEL_UART0; }
app_log_level_t get_app_log_level(void) { return APP_LOG_LEVEL_INFO; }
bool get_ble_usb_passthrough_enabled(void) { return false; }
bool get_phone_ble_enabled(void) { return true; }
void set_phone_ble_enabled(bool value) { (void)value; }
uint32_t get_ble_advertising_timeout_s(void) { return 60; }
uint32_t get_ble_connection_timeout_s(void) { return 120; }
void set_ble_advertising_timeout_s(uint32_t value) { (void)value; }
void set_ble_connection_timeout_s(uint32_t value) { (void)value; }
bool scope_adc_owned(void) { return false; }
bool scope_acq_start(const scope_config_t *config) { (void)config; return true; }
void scope_acq_stop(void) {}
scope_state_t scope_acq_state(void) { return SCOPE_STOPPED; }
bool scope_acq_read(scope_frame_t *frame, uint32_t sequence)
{ (void)frame; (void)sequence; return false; }

/* Derived from event.h prototypes; no copied page code or geometry. */
#include "event_stubs.inc"
