#ifndef _SQUARELINE_PROJECT_UI_H
#define _SQUARELINE_PROJECT_UI_H

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_MENU_ITEM_COUNT 16
#include "display_geometry.h"
#if defined __has_include
#include "app_log.h"
#if __has_include("lvgl.h")
#include "lvgl.h"
#elif __has_include("lvgl/lvgl.h")
#include "lvgl/lvgl.h"
#else
#include "lvgl.h"
#endif
#else
#include "lvgl.h"
#endif

    extern int ina266_flag;
    extern bool ina266_sample_valid;
    void ina266_reset_measurement(void);
    void ui_power_refresh_readings(void);
    extern bool screen_sleep_enabled;
    extern uint32_t screen_idle_timeout_s;
    extern uint32_t screen_photo_duration_s;
    bool ui_edge_back_allowed(void);
    void ui_prepare_page(void);
    void ui_refresh_runtime_control(void);
    void ui_request_back(void);
    char batStr[20];
    lv_obj_t *btn1;
    lv_obj_t *btn2;
    lv_obj_t *btn3;
    lv_obj_t *btn4;
    lv_obj_t *btn5;
    lv_obj_t *btn6;
    lv_obj_t *btn7;
    lv_obj_t *btn8;
    lv_obj_t *btn9;
    lv_obj_t *btn10;
    lv_obj_t *btn11;
    lv_obj_t *btn_level;
    lv_obj_t *btn_environment;
    lv_obj_t *btn12;
    lv_obj_t *btn13;
    lv_obj_t *btn14;
    lv_obj_t *bat_label;

    lv_obj_t *ui_Screen1;
    lv_obj_t *ui_Label1;
    lv_obj_t *ui_Image1;
    lv_obj_t *ui_Screen2;
    lv_obj_t *ui_Image2;
    lv_obj_t *label1;
    lv_obj_t *label2;
    lv_obj_t *label3;
    lv_obj_t *label4;
    lv_obj_t *chart5;
    lv_obj_t *panel;
    lv_obj_t *btn;
    lv_obj_t *label;
    lv_obj_t *voltmeter_chart5;
    lv_obj_t *volt_chart;
    lv_obj_t *cur_chart;

    lv_obj_t *poweron_label;
    lv_obj_t *fre;
    lv_obj_t *duty;
    lv_obj_t *buzzer_volume;
    lv_obj_t *power_value_11v;
    lv_obj_t *power_value_5v;
    lv_obj_t *power_value_3v3;
    lv_obj_t *power_value_default;
    lv_obj_t *servo_slider;
    lv_obj_t *servo_angle_text;
    lv_obj_t *fre_keyboard;
    lv_obj_t *duty_keyboard;
    lv_obj_t *buzzer_keyboard;
    lv_obj_t *power_keyboard;
    lv_obj_t *servo_keyboard;
    extern lv_indev_t *indev_keypad;
    lv_group_t *group;
    lv_obj_t *i2c_extarea;
    lv_obj_t *i2con;
    lv_obj_t *i2c_mode_list;
    lv_obj_t *spi_extarea;
    lv_obj_t *spicon;
    lv_obj_t *uart_extarea;
    lv_obj_t *wireless_uart_extarea;
    lv_obj_t *pwm_btn;
    lv_obj_t *uart_btn;
    lv_obj_t *uart_list;
    lv_obj_t *wireless_uart_list;
    lv_obj_t *wireless_uart_btn;

    lv_timer_t *updatelabel_timer1;
    lv_timer_t *updatelabel_timer2;
    lv_timer_t *updatelabel_timer3;

    lv_timer_t *slider_update_timer;

    lv_timer_t *FRE_label_update_timer;

    extern char voltageStr[20], currentStr[20], powerStr[20], mAHStr[20];
    extern char freqencyStr[20];
    extern float buzzer_volume_percent;
    extern int pwm_Freq;
    extern int pwm_Duty;
    extern long currentBaudRate;
    extern int servo_flag;
    extern int servo_angle;
    extern int power_preset_11v;
    extern int power_preset_5v;
    extern int power_preset_3v3;
    extern int power_boot_preset;
    lv_timer_t *adddata_timer;
    lv_timer_t *adddata_timer2;

    extern int DSO_flag;
    lv_obj_t *FRE_label;

    lv_obj_t *slider;
    extern bool menu_slider_syncing;
    extern int menu_selected_index;
    extern int menu_saved_scroll_y;

    LV_IMG_DECLARE(ui_img_game3_png);
    LV_IMG_DECLARE(Exlink_png);
    LV_IMG_DECLARE(pinmap_png);
    LV_IMG_DECLARE(power_png);
    LV_IMG_DECLARE(pwm_png);
    LV_IMG_DECLARE(usarthelper_png);
    LV_IMG_DECLARE(i2c_png);
    LV_IMG_DECLARE(spi_placeholder_png);
    LV_IMG_DECLARE(voltmeter_png);
    LV_IMG_DECLARE(DSO_png);
    LV_IMG_DECLARE(wireless_png);
    LV_IMG_DECLARE(readme_png);
    LV_IMG_DECLARE(pwmint_png);
    LV_IMG_DECLARE(FREcounter_png);
    LV_IMG_DECLARE(settings_png);
    LV_IMG_DECLARE(servo_png);

    void create_boot_animation(void);
    void my_gui(void);
    void ui_Screen1_screen_init(void);
    void ui_init(void);
    void save_menu_state(void);
    void pinmap_init(void);
    void power_init(void);
    void pwm_init(void);
    void servo_init(void);
    void uarthelper_init(void);
    void i2c_init(void);
    void spi_init(void);
    void voltmeter_init(void);
    void DSO_init(void);
    void wirelessuart_init(void);
    void FREcount_init(void);
    void settings_init(void);
    void readme_init(void);
    void game2048_init(void);
    void save_persistent_settings(void);
    void load_persistent_settings(void);
    uint8_t get_display_brightness_percent(void);
    void set_display_brightness_percent(uint8_t percent);
    bool get_app_log_enabled(void);
    void set_app_log_enabled(bool enabled);
    app_log_channel_t get_app_log_channel(void);
    void set_app_log_channel(app_log_channel_t channel);
    app_log_level_t get_app_log_level(void);
    void set_app_log_level(app_log_level_t level);
    bool get_ble_usb_passthrough_enabled(void);
    void set_ble_usb_passthrough_enabled(bool enabled);
    bool get_phone_ble_enabled(void);
    void set_phone_ble_enabled(bool enabled);
    uint32_t get_ble_advertising_timeout_s(void);
    void set_ble_advertising_timeout_s(uint32_t timeout_s);
    uint32_t get_ble_connection_timeout_s(void);
    void set_ble_connection_timeout_s(uint32_t timeout_s);
    void ui_refresh_phone_ble_state(void);
    void release_transport_page_handles(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
