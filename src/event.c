#include <Arduino.h>
#include "ui.h"
#include "event.h"
#include "level_page.h"
#include "environment_page.h"

extern int mcp4107_target_value;

static uint32_t last_back_navigation_time = 0;
static bool back_navigation_pending = false;
static bool main_menu_reset_pending = false;

int i = 0;
static bool servo_control_syncing = false;

// 声明一个静态变量来存储上一次点击的时间
static uint32_t last_click_time = 0;
// 设置双击的时间阈值（单位为毫秒）
#define DOUBLE_CLICK_TIME_THRESHOLD 100

static void capture_menu_state_before_navigation(int index)
{
    save_menu_state();
    /* A touch click may leave the keypad focus on a different menu card. */
    menu_selected_index = index;
}

static int read_textarea_int(lv_obj_t *textarea, int fallback, int min_value, int max_value)
{
    if (textarea == NULL || !lv_obj_is_valid(textarea) ||
        !lv_obj_check_type(textarea, &lv_textarea_class))
    {
        return fallback;
    }

    const char *text = lv_textarea_get_text(textarea);
    if (text == NULL || text[0] == '\0')
    {
        return fallback;
    }

    return constrain(atoi(text), min_value, max_value);
}

static float read_textarea_float(lv_obj_t *textarea, float fallback, float min_value, float max_value)
{
    if (textarea == NULL || !lv_obj_is_valid(textarea) ||
        !lv_obj_check_type(textarea, &lv_textarea_class))
    {
        return fallback;
    }

    const char *text = lv_textarea_get_text(textarea);
    if (text == NULL || text[0] == '\0')
    {
        return fallback;
    }

    float value = atof(text);
    if (value < min_value)
    {
        value = min_value;
    }
    if (value > max_value)
    {
        value = max_value;
    }
    return value;
}

static long read_dropdown_baud(lv_obj_t *dropdown, long fallback)
{
    if (dropdown == NULL || !lv_obj_is_valid(dropdown) ||
        !lv_obj_check_type(dropdown, &lv_dropdown_class))
    {
        return fallback;
    }

    char buf[16];
    lv_dropdown_get_selected_str(dropdown, buf, sizeof(buf));
    long baud = atol(buf);
    return baud > 0 ? baud : fallback;
}

static void servo_set_angle_internal(int angle, bool normalize_text)
{
    servo_angle = constrain(angle, 0, 180);

    servo_control_syncing = true;

    if (servo_slider)
    {
        lv_slider_set_value(servo_slider, servo_angle, LV_ANIM_OFF);
    }

    if (servo_angle_text && normalize_text)
    {
        char angle_str[8];
        snprintf(angle_str, sizeof(angle_str), "%d", servo_angle);
        lv_textarea_set_text(servo_angle_text, angle_str);
    }

    servo_control_syncing = false;
}

static void sync_persistent_inputs_from_ui(void)
{
    if (fre && lv_obj_is_valid(fre))
    {
        pwm_Freq = read_textarea_int(fre, pwm_Freq, 0, 100000);
    }

    if (duty && lv_obj_is_valid(duty))
    {
        pwm_Duty = read_textarea_int(duty, pwm_Duty, 0, 100);
    }

    if (servo_angle_text && lv_obj_is_valid(servo_angle_text))
    {
        servo_angle = read_textarea_int(servo_angle_text, servo_angle, 0, 180);
    }

    if (buzzer_volume && lv_obj_is_valid(buzzer_volume))
    {
        buzzer_volume_percent = read_textarea_float(buzzer_volume, buzzer_volume_percent, 0.0f, 100.0f);
    }

    if (uart_list && lv_obj_is_valid(uart_list))
    {
        currentBaudRate = read_dropdown_baud(uart_list, currentBaudRate);
    }

    if (wireless_uart_list && lv_obj_is_valid(wireless_uart_list))
    {
        currentBaudRate = read_dropdown_baud(wireless_uart_list, currentBaudRate);
    }
}

static bool can_process_back_navigation(void)
{
    uint32_t now = millis();
    if (now - last_back_navigation_time < 150)
    {
        return false;
    }

    last_back_navigation_time = now;
    return true;
}

static void back_navigation_async_cb(void *user_data)
{
    LV_UNUSED(user_data);
    back_navigation_pending = false;
    ui_Screen1_screen_init();
}

static void finish_back_navigation(void)
{
    sync_persistent_inputs_from_ui();
    save_persistent_settings();

    if (!back_navigation_pending)
    {
        back_navigation_pending = true;
        lv_async_call(back_navigation_async_cb, NULL);
    }
}

static void main_menu_reset_async_cb(void *user_data)
{
    LV_UNUSED(user_data);
    main_menu_reset_pending = false;

    if (panel == NULL || slider == NULL || btn1 == NULL)
    {
        return;
    }
    if (!lv_obj_is_valid(panel) || !lv_obj_is_valid(slider) || !lv_obj_is_valid(btn1))
    {
        return;
    }

    menu_selected_index = 1;
    menu_saved_scroll_y = 0;

    menu_slider_syncing = true;
    lv_slider_set_value(slider, lv_slider_get_max_value(slider), LV_ANIM_OFF);
    menu_slider_syncing = false;

    lv_obj_update_layout(panel);
    if (group != NULL)
    {
        lv_group_focus_obj(btn1);
    }
    lv_obj_scroll_to_y(panel, 0, LV_ANIM_OFF);
}

void anim_cb1(void *obj, int32_t v)
{
    // Boot animation coordinates are already native panel pixels.
    lv_obj_set_x(obj, v);
}

void anim_cb2(void *obj, int32_t v)
{
    // Boot animation coordinates are already native panel pixels.
    lv_obj_set_y(obj, v);
}

void anim_end_callback(lv_anim_t *animation)
{
    /* LVGL unlinks this completed animation before calling ready_cb. Deleting
     * its owner can therefore stop/free the timeline without freeing it twice. */
    lv_obj_del((lv_obj_t *)animation->var);
    ui_Image1 = NULL;
    ui_Image2 = NULL;
    ui_Screen1_screen_init();
    lv_obj_invalidate(lv_scr_act());
}

void btn1_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open pinmap\r\n");
        capture_menu_state_before_navigation(1);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        pinmap_init();
    }
}

void btn2_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open power\r\n");
        capture_menu_state_before_navigation(2);
        lv_obj_clean(lv_scr_act());
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        power_init();
    }
}

void btn3_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (slider_update_timer)
    {
        lv_timer_del(slider_update_timer); // 删除定时器1
        slider_update_timer = NULL;        // 清空指针以避免悬空指针
    }
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open pwm\r\n");
        capture_menu_state_before_navigation(3);
        pwm_init();
    }
}

void btn4_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (slider_update_timer)
    {
        lv_timer_del(slider_update_timer); // 删除定时器1
        slider_update_timer = NULL;        // 清空指针以避免悬空指针
    }
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open servo\r\n");
        capture_menu_state_before_navigation(4);
        servo_init();
    }
}

void btn5_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open uart helper\r\n");
        capture_menu_state_before_navigation(5);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        uarthelper_init();
    }
}

void btn6_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open i2c\r\n");
        capture_menu_state_before_navigation(6);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        i2c_init();
    }
}

void btn7_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open spi\r\n");
        capture_menu_state_before_navigation(7);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        spi_init();
    }
}

void btn8_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open voltmeter\r\n");
        capture_menu_state_before_navigation(8);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        voltmeter_init();
    }
}

void btn9_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open dso\r\n");
        capture_menu_state_before_navigation(9);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        DSO_init();
    }
}

void btn10_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open wireless uart\r\n");
        capture_menu_state_before_navigation(10);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 删除定时器1
            slider_update_timer = NULL;        // 清空指针以避免悬空指针
        }
        wirelessuart_init();
    }
}

void btn11_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open frequency counter\r\n");
        capture_menu_state_before_navigation(11);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 鍒犻櫎瀹氭椂鍣?
            slider_update_timer = NULL;        // 娓呯┖鎸囬拡浠ラ伩鍏嶆偓绌烘寚閽?
        }
        FREcount_init();
    }
}

void btn_level_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open level\r\n");
        capture_menu_state_before_navigation(12);
        level_init();
    }
}

void btn_environment_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open environment\r\n");
        capture_menu_state_before_navigation(13);
        environment_init();
    }
}

void btn12_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open game2048\r\n");
        capture_menu_state_before_navigation(14);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer); // 閸掔娀娅庣€规碍妞傞崳?
            slider_update_timer = NULL;        // 濞撳懐鈹栭幐鍥嫛娴犮儵浼╅崗宥嗗亾缁岀儤瀵氶柦?
        }
        game2048_init();
    }
}

void btn13_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open settings\r\n");
        capture_menu_state_before_navigation(15);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer);
            slider_update_timer = NULL;
        }
        settings_init();
    }
}

void btn14_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        APP_LOGD("nav", "open readme\r\n");
        capture_menu_state_before_navigation(16);
        if (slider_update_timer)
        {
            lv_timer_del(slider_update_timer);
            slider_update_timer = NULL;
        }
        readme_init();
    }
}

void poweronbtn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        if (digitalRead(1) == LOW)
        {
            ina266_reset_measurement();
            digitalWrite(1, HIGH);
            ina266_flag = 1;
            lv_obj_set_style_text_color(poweron_label, lv_color_hex(0x00FF7F), 0);
            APP_LOGI("power", "power output enabled\r\n");
        }
        else
        {
            digitalWrite(1, LOW);
            ina266_flag = 0;
            ina266_reset_measurement();
            lv_obj_set_style_text_color(poweron_label, lv_color_hex(0xFF0000), 0);
            APP_LOGI("power", "power output disabled\r\n");
        }
        ui_power_refresh_readings();
    }
}

void pwm_btn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        pwm_Freq = read_textarea_int(fre, pwm_Freq, 0, 100000);
        pwm_Duty = read_textarea_int(duty, pwm_Duty, 0, 100);
        i++;
        if (i % 2 == 1)
        {
            pwm_flag = 1;
            lv_obj_set_style_bg_color(pwm_btn, lv_color_hex(0x00FF7F), 0);
            APP_LOGI("pwm", "pwm enabled freq=%d duty=%d\r\n", pwm_Freq, pwm_Duty);
        }
        else
        {
            pwm_flag = 0;
            lv_obj_set_style_bg_color(pwm_btn, lv_color_hex(0xFF0000), 0);
            APP_LOGI("pwm", "pwm disabled\r\n");
        }
        save_persistent_settings();
    }
}

void i2conbtn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        if (i2cscan_flag == 0)
        {
            i2cscan_flag = 1;
            lv_obj_set_style_bg_color(i2con, lv_color_hex(0x00FF7F), 0);
            APP_LOGI("i2c", "i2c page enabled\r\n");
        }
        else
        {
            i2cscan_flag = 0;
            lv_obj_set_style_bg_color(i2con, lv_color_hex(0xFF0000), 0);
            APP_LOGI("i2c", "i2c page disabled\r\n");
        }
    }
}

void uart_btn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        if (uart_helper_flag == 0)
        {
            uart_helper_flag = 1;
            lv_obj_set_style_bg_color(uart_btn, lv_color_hex(0x00FF7F), 0);
            APP_LOGI("uart", "uart helper enabled\r\n");
        }
        else
        {
            uart_helper_flag = 0;
            lv_obj_set_style_bg_color(uart_btn, lv_color_hex(0xFF0000), 0);
            APP_LOGI("uart", "uart helper disabled\r\n");
        }
    }
}

void wireless_uart_btn_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        if (BluetoothSerial_flag == 0)
        {
            BluetoothSerial_flag = 1;
            lv_obj_set_style_bg_color(wireless_uart_btn, lv_color_hex(0x00FF7F), 0);
            APP_LOGI("ble", "wireless page enabled\r\n");
        }
        else
        {
            BluetoothSerial_flag = 0;
            lv_obj_set_style_bg_color(wireless_uart_btn, lv_color_hex(0xFF0000), 0);
            APP_LOGI("ble", "wireless page disabled\r\n");
        }
    }
}

void VUPbtn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        mcp4107_flag = 2;
    }
}

void VDOWNbtn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        mcp4107_flag = 3;
    }
}

void V11btn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        mcp4107_target_value = power_preset_11v;
        mcp4107_flag = 1;
        APP_LOGI("power", "apply 11V preset raw=%d\r\n", power_preset_11v);
    }
}

void V5btn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        mcp4107_target_value = power_preset_5v;
        mcp4107_flag = 1;
        APP_LOGI("power", "apply 5V preset raw=%d\r\n", power_preset_5v);
    }
}

void V3btn_event_cb(lv_event_t *e)
{

    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        mcp4107_target_value = power_preset_3v3;
        mcp4107_flag = 1;
        APP_LOGI("power", "apply 3V3 preset raw=%d\r\n", power_preset_3v3);
    }
}

void spiconbtn_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        if (spi_bridge_flag == 0)
        {
            spi_bridge_flag = 1;
            lv_obj_set_style_bg_color(spicon, lv_color_hex(0x00FF7F), 0);
            APP_LOGI("spi", "spi page enabled\r\n");
        }
        else
        {
            spi_bridge_flag = 0;
            lv_obj_set_style_bg_color(spicon, lv_color_hex(0xFF0000), 0);
            APP_LOGI("spi", "spi page disabled\r\n");
        }
    }
}

void save_11v_preset_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        int old_value = power_preset_11v;
        power_preset_11v = read_textarea_int(power_value_11v, power_preset_11v, 1, 126);

        char value_str[8];
        snprintf(value_str, sizeof(value_str), "%d", power_preset_11v);
        lv_textarea_set_text(power_value_11v, value_str);

        if (mcp4107_target_value == old_value)
        {
            mcp4107_target_value = power_preset_11v;
        }
        APP_LOGI("settings", "saved 11V preset=%d\r\n", power_preset_11v);
        save_persistent_settings();
    }
}

void save_5v_preset_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        int old_value = power_preset_5v;
        power_preset_5v = read_textarea_int(power_value_5v, power_preset_5v, 1, 126);

        char value_str[8];
        snprintf(value_str, sizeof(value_str), "%d", power_preset_5v);
        lv_textarea_set_text(power_value_5v, value_str);

        if (mcp4107_target_value == old_value)
        {
            mcp4107_target_value = power_preset_5v;
        }
        APP_LOGI("settings", "saved 5V preset=%d\r\n", power_preset_5v);
        save_persistent_settings();
    }
}

void save_3v3_preset_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        int old_value = power_preset_3v3;
        power_preset_3v3 = read_textarea_int(power_value_3v3, power_preset_3v3, 1, 126);

        char value_str[8];
        snprintf(value_str, sizeof(value_str), "%d", power_preset_3v3);
        lv_textarea_set_text(power_value_3v3, value_str);

        if (mcp4107_target_value == old_value)
        {
            mcp4107_target_value = power_preset_3v3;
        }
        APP_LOGI("settings", "saved 3V3 preset=%d\r\n", power_preset_3v3);
        save_persistent_settings();
    }
}

void save_default_preset_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        power_boot_preset = read_textarea_int(power_value_default, power_boot_preset, 1, 126);

        char value_str[8];
        snprintf(value_str, sizeof(value_str), "%d", power_boot_preset);
        lv_textarea_set_text(power_value_default, value_str);
        APP_LOGI("settings", "saved default power preset=%d\r\n", power_boot_preset);
        save_persistent_settings();
    }
}

void buzzer_apply_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        buzzer_volume_percent = read_textarea_float(buzzer_volume, buzzer_volume_percent, 0.0f, 100.0f);
        APP_LOGI("settings", "saved buzzer volume=%.1f\r\n", buzzer_volume_percent);
        save_persistent_settings();
    }
}

void settings_open_event_cb(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_CLICKED)
    {
        settings_init();
    }
}

void log_enabled_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    set_app_log_enabled(enabled);
    APP_LOGI("log", "enabled=%s\r\n", enabled ? "true" : "false");
    save_persistent_settings();
}

void log_channel_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    uint16_t index = lv_dropdown_get_selected(target);
    app_log_channel_t channel = APP_LOG_CHANNEL_USB;
    switch (index)
    {
    case 0:
        channel = APP_LOG_CHANNEL_UART0;
        break;
    case 1:
        channel = APP_LOG_CHANNEL_USB;
        break;
    case 2:
        channel = APP_LOG_CHANNEL_NONE;
        break;
    default:
        channel = APP_LOG_CHANNEL_USB;
        break;
    }

    set_app_log_channel(channel);
    APP_LOGI("log", "channel=%d\r\n", (int)channel);
    save_persistent_settings();
}

void log_level_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    uint16_t index = lv_dropdown_get_selected(target);
    app_log_level_t level = APP_LOG_LEVEL_INFO;
    switch (index)
    {
    case 0:
        level = APP_LOG_LEVEL_ERROR;
        break;
    case 1:
        level = APP_LOG_LEVEL_WARN;
        break;
    case 2:
        level = APP_LOG_LEVEL_INFO;
        break;
    case 3:
        level = APP_LOG_LEVEL_DEBUG;
        break;
    default:
        level = APP_LOG_LEVEL_INFO;
        break;
    }

    set_app_log_level(level);
    APP_LOGI("log", "level=%d\r\n", (int)level);
    save_persistent_settings();
}

void ble_usb_passthrough_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t *target = lv_event_get_target(e);
    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    set_ble_usb_passthrough_enabled(enabled);
    APP_LOGI("settings", "ble+usb passthrough=%s\r\n", enabled ? "true" : "false");
    save_persistent_settings();
}
void keyboard_event_handler(lv_event_t *e)
{

    lv_event_code_t code = lv_event_get_code(e); /* 获取事件类型 */
    lv_obj_t *target = lv_event_get_target(e);   /* 获取触发源 */
}

static void hide_all_soft_keyboards(void)
{
    if (fre_keyboard)
    {
        lv_keyboard_set_textarea(fre_keyboard, NULL);
        lv_obj_add_flag(fre_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (duty_keyboard)
    {
        lv_keyboard_set_textarea(duty_keyboard, NULL);
        lv_obj_add_flag(duty_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (buzzer_keyboard)
    {
        lv_keyboard_set_textarea(buzzer_keyboard, NULL);
        lv_obj_add_flag(buzzer_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (power_keyboard)
    {
        lv_keyboard_set_textarea(power_keyboard, NULL);
        lv_obj_add_flag(power_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (servo_keyboard)
    {
        lv_keyboard_set_textarea(servo_keyboard, NULL);
        lv_obj_add_flag(servo_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_keyboard_for_textarea(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    if (keyboard == NULL || textarea == NULL)
    {
        return;
    }

    hide_all_soft_keyboards();
    lv_keyboard_set_textarea(keyboard, textarea);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

void fretext_event_handler(lv_event_t *e)
{
    show_keyboard_for_textarea(fre_keyboard, fre);
}

void dutytext_event_handler(lv_event_t *e)
{
    show_keyboard_for_textarea(duty_keyboard, duty);
}

void buzzertext_event_handler(lv_event_t *e)
{
    show_keyboard_for_textarea(buzzer_keyboard, buzzer_volume);
}

void power_value_text_event_handler(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    show_keyboard_for_textarea(power_keyboard, target);
}

void servo_angle_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        show_keyboard_for_textarea(servo_keyboard, servo_angle_text);
        return;
    }

    if (servo_control_syncing || servo_angle_text == NULL)
    {
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED)
    {
        const char *text = lv_textarea_get_text(servo_angle_text);
        if (text && text[0] != '\0')
        {
            servo_angle = constrain(atoi(text), 0, 180);
            servo_control_syncing = true;
            if (servo_slider)
            {
                lv_slider_set_value(servo_slider, servo_angle, LV_ANIM_OFF);
            }
            servo_control_syncing = false;
        }
    }

    if (code == LV_EVENT_READY || code == LV_EVENT_DEFOCUSED)
    {
        servo_set_angle_internal(read_textarea_int(servo_angle_text, servo_angle, 0, 180), true);
    }
}

void servo_slider_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED && !servo_control_syncing)
    {
        servo_set_angle_internal(lv_slider_get_value(servo_slider), true);
    }
}

void servo_minus_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        servo_set_angle_internal(servo_angle - 1, true);
    }
}

void servo_plus_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        servo_set_angle_internal(servo_angle + 1, true);
    }
}

void servo_preset_0_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        servo_set_angle_internal(0, true);
    }
}

void servo_preset_center_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        servo_set_angle_internal(90, true);
    }
}

void servo_preset_180_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        servo_set_angle_internal(180, true);
    }
}

void clear_keyboard_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e); /* 获取事件类型 */
    lv_obj_t *target = lv_event_get_target(e);   /* 获取触发源 */
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        /* 获取键盘按钮索引 */
        uint16_t id = lv_btnmatrix_get_selected_btn(target);
        /* 获取按钮文本 */
        const char *txt = lv_btnmatrix_get_btn_text(target, id);
        if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) /* 判断是不是键盘图标被按下 */
        {
            if (fre_keyboard)
            {
                lv_obj_add_flag(fre_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
            if (duty_keyboard)
            {
                lv_obj_add_flag(duty_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
            if (buzzer_keyboard)
            {
                lv_obj_add_flag(buzzer_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
            if (power_keyboard)
            {
                lv_obj_add_flag(power_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
            if (servo_keyboard)
            {
                lv_obj_add_flag(servo_keyboard, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// 滑动条的事件回调
void slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED || menu_slider_syncing)
    {
        return;
    }

    int value = lv_slider_get_value(slider); // 获取滑动条的值
    switch (UI_MENU_ITEM_COUNT - value)
    {
    case 1:
        lv_group_focus_obj(btn1);
        break;
    case 2:
        lv_group_focus_obj(btn2);
        break;
    case 3:
        lv_group_focus_obj(btn3);
        break;
    case 4:
        lv_group_focus_obj(btn4);
        break;
    case 5:
        lv_group_focus_obj(btn5);
        break;
    case 6:
        lv_group_focus_obj(btn6);
        break;
    case 7:
        lv_group_focus_obj(btn7);
        break;
    case 8:
        lv_group_focus_obj(btn8);
        break;
    case 9:
        lv_group_focus_obj(btn9);
        break;
    case 10:
        lv_group_focus_obj(btn10);
        break;
    case 11:
        lv_group_focus_obj(btn11);
        break;
    case 12:
        lv_group_focus_obj(btn_level);
        break;
    case 13:
        lv_group_focus_obj(btn_environment);
        break;
    case 14:
        lv_group_focus_obj(btn12);
        break;
    case 15:
        lv_group_focus_obj(btn13);
        break;
    case 16:
        lv_group_focus_obj(btn14);
        break;
    default:
        break;
    }
}

void main_menu_longpress_event_handler(lv_event_t *e)
{
    LV_UNUSED(e);

    if (main_menu_reset_pending)
    {
        return;
    }

    main_menu_reset_pending = true;
    lv_async_call(main_menu_reset_async_cb, NULL);
}

void event_handler_back(lv_event_t *e)
{
    // Global edge navigation is arbitrated before hit-testing in main.cpp.
    // Page/container RELEASED handlers cannot cancel an already-fired click.
    LV_UNUSED(e);
}

void ui_request_back(void)
{
    if (ui_edge_back_allowed()) longpress_event_handler_back(NULL);
}

void longpress_event_handler_back(lv_event_t *e)
{
    if (!can_process_back_navigation())
    {
        return;
    }
    ina266_flag = 0;
    mcp4107_flag = 0;
    pwm_flag = 0;
    servo_flag = 0;
    i2cscan_flag = 0;
    spi_bridge_flag = 0;
    uart_helper_flag = 0;
    DSO_flag = 0;
    BluetoothSerial_flag = 0;
    FREcount_flag = 0;
    digitalWrite(1, LOW);
    if (updatelabel_timer1)
    {
        lv_timer_del(updatelabel_timer1); // 删除定时器1
        updatelabel_timer1 = NULL;        // 清空指针以避免悬空指针
    }
    if (updatelabel_timer2)
    {
        lv_timer_del(updatelabel_timer2); // 删除定时器1
        updatelabel_timer2 = NULL;        // 清空指针以避免悬空指针
    }
    if (updatelabel_timer3)
    {
        lv_timer_del(updatelabel_timer3); // 删除定时器1
        updatelabel_timer3 = NULL;        // 清空指针以避免悬空指针
    }
    if (adddata_timer)
    {
        lv_timer_del(adddata_timer); // 删除定时器2
        adddata_timer = NULL;        // 清空指针以避免悬空指针
    }
    if (adddata_timer2)
    {
        lv_timer_del(adddata_timer2); // 删除定时器2
        adddata_timer2 = NULL;        // 清空指针以避免悬空指针
    }

    if (slider_update_timer)
    {
        lv_timer_del(slider_update_timer); // 删除定时器1
        slider_update_timer = NULL;        // 清空指针以避免悬空指针
    }
    if (FRE_label_update_timer)
    {
        lv_timer_del(FRE_label_update_timer); // 删除定时器1
        FRE_label_update_timer = NULL;        // 清空指针以避免悬空指针
    }
    finish_back_navigation();
}
