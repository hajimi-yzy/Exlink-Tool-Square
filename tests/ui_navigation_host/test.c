/* Complete production pages/events and the exact keypad_read on real LVGL.
 * Only hardware/transport/sensor services are replaced. No target timing claims. */
#include "Arduino.h"
#include "ui.h"
#include "event.h"
#include "level_page.h"
#include "environment_page.h"
#include "scope_capture.h"

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #value); exit(1); } } while (0)

static lv_color_t pixels[480 * 32];
static unsigned pressed_pin = 255;
static unsigned buzzes;
static unsigned saves;
static bool display_idle_active, suppress_keys_until_release;

uint32_t millis(void) { return lv_tick_get(); }
int digitalRead(uint8_t pin) { return pin == pressed_pin ? LOW : HIGH; }
void digitalWrite(uint8_t pin, uint8_t value) { (void)pin; (void)value; }
static void notify_user_activity(void) {}
void buzzer_task(void) { ++buzzes; }

#include "production_keypad.inc"

lv_indev_t *indev_keypad;
int ina266_flag, mcp4107_flag, pwm_flag, i2cscan_flag, spi_bridge_flag;
int uart_helper_flag, BluetoothSerial_flag, FREcount_flag, DSO_flag;
int mcp4107_target_value;
bool ina266_sample_valid, screen_sleep_enabled = true;
uint32_t screen_idle_timeout_s = 60, screen_photo_duration_s = 10;
char voltageStr[20] = "0.00", currentStr[20] = "0.00";
char powerStr[20] = "0.00", mAHStr[20] = "0.00", freqencyStr[20] = "0";
float buzzer_volume_percent = 20;
int pwm_Freq = 1000, pwm_Duty = 50, servo_flag, servo_angle = 90;
int power_preset_11v = 16, power_preset_5v = 44, power_preset_3v3 = 86;
int power_boot_preset = 86;
long currentBaudRate = 115200;
double v, a, w;

void ina266_reset_measurement(void) {}
void save_persistent_settings(void) { ++saves; }
void release_transport_page_handles(void) {}
uint8_t get_display_brightness_percent(void) { return 70; }
void set_display_brightness_percent(uint8_t value) { (void)value; }
bool get_app_log_enabled(void) { return true; }
void set_app_log_enabled(bool value) { (void)value; }
app_log_channel_t get_app_log_channel(void) { return APP_LOG_CHANNEL_USB; }
void set_app_log_channel(app_log_channel_t value) { (void)value; }
app_log_level_t get_app_log_level(void) { return APP_LOG_LEVEL_INFO; }
void set_app_log_level(app_log_level_t value) { (void)value; }
bool get_ble_usb_passthrough_enabled(void) { return false; }
void set_ble_usb_passthrough_enabled(bool value) { (void)value; }
bool get_phone_ble_enabled(void) { return false; }
void set_phone_ble_enabled(bool value) { (void)value; }
uint32_t get_ble_advertising_timeout_s(void) { return 120; }
uint32_t get_ble_connection_timeout_s(void) { return 120; }
void set_ble_advertising_timeout_s(uint32_t value) { (void)value; }
void set_ble_connection_timeout_s(uint32_t value) { (void)value; }
int app_log_log(app_log_level_t level, const char *tag, const char *fmt, ...)
{ (void)level; (void)tag; (void)fmt; return 0; }

bool level_sensor_read(level_reading_t *reading)
{ *reading = (level_reading_t){true, true, 7.5f, -3.2f, 0x68, 0x68, true, true}; return true; }
void level_sensor_set_active(bool active) { (void)active; }
bool level_sensor_zero(void) { return true; }
bool environment_sensor_read(environment_reading_t *reading)
{ *reading = (environment_reading_t){true, true, 25.6f, 58.2f, ENVIRONMENT_ERROR_NONE}; return true; }
void environment_sensor_set_active(bool active) { (void)active; }
bool scope_adc_owned(void) { return false; }
bool scope_acq_start(const scope_config_t *config) { (void)config; return true; }
void scope_acq_stop(void) {}
scope_state_t scope_acq_state(void) { return SCOPE_STOPPED; }
bool scope_acq_read(scope_frame_t *frame, uint32_t sequence)
{ (void)frame; (void)sequence; return false; }

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *data)
{
    CHECK(area->x1 >= 0 && area->y1 >= 0 && area->x2 < 480 && area->y2 < 480);
    (void)data;
    lv_disp_flush_ready(driver);
}

static void advance(unsigned ms)
{
    for (unsigned i = 0; i < ms; i += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

static void key(unsigned pin, unsigned duration)
{
    pressed_pin = pin;
    advance(duration);
    pressed_pin = 255;
    advance(20);
}

int main(void)
{
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, NULL, 480 * 32);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = display.ver_res = 480;
    display.draw_buf = &buffer;
    display.flush_cb = flush;
    CHECK(lv_disp_drv_register(&display));
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_KEYPAD;
    input.read_cb = keypad_read;
    indev_keypad = lv_indev_drv_register(&input);
    CHECK(indev_keypad);
    lv_timer_set_period(indev_keypad->driver->read_timer, 5);
    lv_theme_default_init(NULL, lv_color_hex(0), lv_color_hex(0xFF0000), true, NULL);
    ui_Screen1_screen_init();
    advance(400);
    const unsigned turns = 2048;
    for (unsigned i = 0; i < turns; ++i) {
        lv_obj_t *before = lv_group_get_focused(group);
        key(i % 2 ? 39 : 38, 15);
        CHECK(lv_group_get_focused(group) != before);
        CHECK(lv_group_get_obj_count(group) == 16);
    }
    /* Real enter/back events rebuild groups/pages while the indev runs. */
    for (unsigned pass = 0; pass < 32; ++pass) {
        for (unsigned index = 0; index < 16; ++index) {
            menu_selected_index = index + 1;
            ui_Screen1_screen_init();
            advance(40);
            CHECK(ui_edge_back_allowed() == false);
            key(40, 30);
            CHECK(ui_edge_back_allowed());
            key(38, 15);
            key(39, 15);
            key(40, 350);
            advance(350);
            CHECK(!ui_edge_back_allowed());
            CHECK(group && lv_group_get_obj_count(group) == 16);
        }
    }
    /* Repeat steady-state main-menu rotations/page rebuilds and bound growth. */
    ui_Screen1_screen_init(); advance(1000);
    lv_mem_monitor_t first, last;
    lv_mem_monitor(&first);
    for (unsigned i = 0; i < 128; ++i) {
        key(38, 15); key(39, 15);
        ui_Screen1_screen_init(); advance(400);
    }
    lv_mem_monitor(&last);
    CHECK(lv_mem_test() == LV_RES_OK);
    CHECK(last.free_size + 256 >= first.free_size);
    printf("PASS production keypad: %u rotations, 512 page enter/back cycles; steady heap delta=%ld bytes, free=%lu, largest=%lu, saves=%u buzzes=%u\n",
           turns + 512U * 2U + 256U, (long)first.free_size - (long)last.free_size,
           (unsigned long)last.free_size, (unsigned long)last.free_biggest_size, saves, buzzes);
    return 0;
}
