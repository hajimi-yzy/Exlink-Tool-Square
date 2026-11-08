/*
 * Exlink Tool
 * 原作者：expert电子实验室
 * 二次开发与适配：一只羊蒸鱼
 * 保留原项目及第三方代码各自的许可证和版权声明。
 */
#include <Arduino.h>
#include "ui.h"
#include "event.h"
#include "scope_capture.h"
#include "boot_artwork.h"
#include <stdio.h>
#include <string.h>

/* Every page uses native 480x480 pixels: object bounds, animation values,
 * display dirty rectangles and touch points share one coordinate system.
 * Do not remap LVGL setters or rescale an already rendered dirty area. */

static bool ui_home_page = false;
static const uint32_t screen_timeout_choices[] = {0, 15, 30, 60, 120, 300};
static const uint32_t screen_photo_duration_choices[] = {5, 10, 15, 30, 60};
static const uint8_t display_brightness_choices[] = {5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
static const uint32_t ble_advertising_timeout_choices[] = {30, 60, 120, 300};
static const uint32_t ble_connection_timeout_choices[] = {60, 120, 300, 600};
static lv_obj_t *phone_ble_switch = NULL;
static lv_obj_t *battery_voltage_label = NULL;
static lv_obj_t *runtime_toggle_button = NULL;
static int *runtime_toggle_active = NULL;
static lv_obj_t *power_chart_note = NULL;

static void screen_sleep_enabled_cb(lv_event_t *event)
{
    screen_sleep_enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    save_persistent_settings();
}

static void screen_timeout_cb(lv_event_t *event)
{
    const uint16_t index = lv_dropdown_get_selected(lv_event_get_target(event));
    if (index < sizeof(screen_timeout_choices) / sizeof(screen_timeout_choices[0]))
    {
        screen_idle_timeout_s = screen_timeout_choices[index];
        save_persistent_settings();
    }
}

static void display_brightness_cb(lv_event_t *event)
{
    const uint16_t index = lv_dropdown_get_selected(lv_event_get_target(event));
    if (index < sizeof(display_brightness_choices) / sizeof(display_brightness_choices[0]))
    {
        set_display_brightness_percent(display_brightness_choices[index]);
    }
}

static void screen_photo_duration_cb(lv_event_t *event)
{
    const uint16_t index = lv_dropdown_get_selected(lv_event_get_target(event));
    if (index < sizeof(screen_photo_duration_choices) / sizeof(screen_photo_duration_choices[0]))
    {
        screen_photo_duration_s = screen_photo_duration_choices[index];
        save_persistent_settings();
    }
}

static uint16_t timeout_choice_index(const uint32_t *choices, size_t count,
                                     uint32_t value, uint16_t default_index)
{
    for (uint16_t index = 0; index < count; ++index)
    {
        if (choices[index] == value) return index;
    }
    return default_index;
}

static void phone_ble_enabled_cb(lv_event_t *event)
{
    set_phone_ble_enabled(lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
}

static void ble_advertising_timeout_cb(lv_event_t *event)
{
    const uint16_t index = lv_dropdown_get_selected(lv_event_get_target(event));
    if (index < sizeof(ble_advertising_timeout_choices) / sizeof(ble_advertising_timeout_choices[0]))
    {
        set_ble_advertising_timeout_s(ble_advertising_timeout_choices[index]);
        save_persistent_settings();
    }
}

static void ble_connection_timeout_cb(lv_event_t *event)
{
    const uint16_t index = lv_dropdown_get_selected(lv_event_get_target(event));
    if (index < sizeof(ble_connection_timeout_choices) / sizeof(ble_connection_timeout_choices[0]))
    {
        set_ble_connection_timeout_s(ble_connection_timeout_choices[index]);
        save_persistent_settings();
    }
}

void ui_refresh_phone_ble_state(void)
{
    if (phone_ble_switch != NULL && lv_obj_is_valid(phone_ble_switch))
    {
        const bool ui_enabled = lv_obj_has_state(phone_ble_switch, LV_STATE_CHECKED);
        const bool service_enabled = get_phone_ble_enabled();
        if (ui_enabled != service_enabled)
        {
            if (service_enabled) lv_obj_add_state(phone_ble_switch, LV_STATE_CHECKED);
            else lv_obj_clear_state(phone_ble_switch, LV_STATE_CHECKED);
        }
    }

    if (BluetoothSerial_flag == 0 &&
        wireless_uart_btn != NULL &&
        lv_obj_is_valid(wireless_uart_btn) &&
        lv_obj_get_parent(wireless_uart_btn) == lv_scr_act())
    {
        lv_obj_set_style_bg_color(wireless_uart_btn, lv_color_hex(0xFF0000), 0);
    }
}

bool ui_edge_back_allowed(void)
{
    return !ui_home_page;
}

void ui_prepare_page(void)
{
    lv_obj_t *screen = lv_scr_act();
    release_transport_page_handles();
    runtime_toggle_button = NULL;
    runtime_toggle_active = NULL;
    power_chart_note = NULL;
    /* Stop callbacks before deleting the objects they reference. Widget
     * addresses can be reused by the next page, so validity alone is unsafe. */
    lv_timer_t **page_timers[] = {
        &updatelabel_timer1, &updatelabel_timer2, &updatelabel_timer3,
        &adddata_timer, &adddata_timer2,
        &slider_update_timer, &FRE_label_update_timer,
    };
    for (size_t n = 0; n < sizeof(page_timers) / sizeof(page_timers[0]); ++n) {
        if (*page_timers[n]) lv_timer_del(*page_timers[n]);
        *page_timers[n] = NULL;
    }
    fre = duty = servo_angle_text = buzzer_volume = NULL;
    power_value_11v = power_value_5v = power_value_3v3 = power_value_default = NULL;
    fre_keyboard = duty_keyboard = servo_keyboard = buzzer_keyboard = power_keyboard = NULL;
    servo_slider = poweron_label = bat_label = battery_voltage_label = NULL;
    phone_ble_switch = NULL;
    uart_list = NULL;
    uart_extarea = NULL;
    uart_btn = NULL;
    wireless_uart_list = NULL;
    wireless_uart_extarea = NULL;
    wireless_uart_btn = NULL;
    i2c_mode_list = NULL;
    i2c_extarea = NULL;
    i2con = NULL;
    spi_extarea = NULL;
    spicon = NULL;
    // lv_obj_clean removes children, NOT the screen's event callbacks. Without
    // removing these, every page visit accumulates back/home handlers.
    while (lv_obj_remove_event_cb(screen, event_handler_back)) {}
    while (lv_obj_remove_event_cb(screen, longpress_event_handler_back)) {}
    while (lv_obj_remove_event_cb(screen, main_menu_longpress_event_handler)) {}
    lv_obj_clean(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_home_page = false;
}

// 创建样式对象
lv_style_t style_rect;
static void ensure_main_menu_styles(lv_style_t *focused_style)
{
    static bool ready = false;
    if (ready) return;
    lv_style_init(&style_rect);
    lv_style_set_border_color(&style_rect, lv_color_hex(0xFF0000));
    lv_style_set_border_width(&style_rect, 3);
    lv_style_set_bg_color(&style_rect, lv_color_hex(0x000000));
    lv_style_set_radius(&style_rect, 20);
    lv_style_init(focused_style);
    lv_style_set_border_color(focused_style, lv_color_hex(0xFFD700));
    lv_style_set_border_width(focused_style, 5);
    lv_style_set_outline_color(focused_style, lv_color_hex(0xFFD700));
    lv_style_set_outline_width(focused_style, 3);
    lv_style_set_outline_pad(focused_style, 2);
    lv_style_set_shadow_color(focused_style, lv_color_hex(0xFFD700));
    lv_style_set_shadow_width(focused_style, 20);
    lv_style_set_shadow_opa(focused_style, LV_OPA_50);
    ready = true;
}
bool menu_slider_syncing = false;
int menu_selected_index = 1;
int menu_saved_scroll_y = 0;

#define MENU_ITEM_COUNT UI_MENU_ITEM_COUNT

extern double v, a, w;

static const uint8_t BATTERY_ADC_PIN = 2;
static const uint8_t BATTERY_SAMPLE_COUNT = 8;
static const float BATTERY_DIVIDER_GAIN = 2.0f;
static const float BATTERY_FILTER_ALPHA = 0.18f;
static float battery_voltage_filtered = 0.0f;
static bool battery_voltage_initialized = false;
static lv_obj_t *ble_mode_title_label = NULL;
static lv_obj_t *ble_mode_hint_label = NULL;
static lv_obj_t *ble_pin_blocks[5] = {NULL};
static lv_obj_t *ble_pin_number_labels[5] = {NULL};
static lv_obj_t *ble_pin_name_labels[5] = {NULL};
static bool ui_focus_styles_ready = false;
static lv_style_t ui_focus_emphasis_style;
static lv_style_t ui_input_focus_suppressed_style;
static const lv_coord_t UI_FOCUS_ROW_TOLERANCE = 8;

static void ensure_ui_focus_styles(void)
{
    if (ui_focus_styles_ready)
    {
        return;
    }

    lv_style_init(&ui_focus_emphasis_style);
    lv_style_set_border_color(&ui_focus_emphasis_style, lv_color_hex(0xFFD700));
    lv_style_set_border_width(&ui_focus_emphasis_style, 4);
    lv_style_set_outline_color(&ui_focus_emphasis_style, lv_color_hex(0xFFD700));
    lv_style_set_outline_width(&ui_focus_emphasis_style, 3);
    lv_style_set_outline_pad(&ui_focus_emphasis_style, 2);
    lv_style_set_shadow_color(&ui_focus_emphasis_style, lv_color_hex(0xFFD700));
    lv_style_set_shadow_width(&ui_focus_emphasis_style, 18);
    lv_style_set_shadow_opa(&ui_focus_emphasis_style, LV_OPA_50);

    lv_style_init(&ui_input_focus_suppressed_style);
    lv_style_set_outline_width(&ui_input_focus_suppressed_style, 0);
    lv_style_set_shadow_width(&ui_input_focus_suppressed_style, 0);
    lv_style_set_border_color(&ui_input_focus_suppressed_style, lv_color_hex(0x5A5A5A));
    lv_style_set_border_width(&ui_input_focus_suppressed_style, 1);

    ui_focus_styles_ready = true;
}

static void apply_ui_focus_emphasis(lv_obj_t *obj)
{
    if (obj == NULL)
    {
        return;
    }

    ensure_ui_focus_styles();
    lv_obj_add_style(obj, &ui_focus_emphasis_style, LV_STATE_FOCUSED);
}

void ui_refresh_runtime_control(void)
{
    if (runtime_toggle_button == NULL || runtime_toggle_active == NULL ||
        !lv_obj_is_valid(runtime_toggle_button)) return;
    lv_label_set_text(lv_obj_get_child(runtime_toggle_button, 0),
                      *runtime_toggle_active ? "STOP" : "START");
    lv_obj_set_style_bg_color(runtime_toggle_button,
        lv_color_hex(*runtime_toggle_active ? 0x00A660 : 0xB02020), 0);
}

static void runtime_toggle_event_cb(lv_event_t *event)
{
    int *active = (int *)lv_event_get_user_data(event);
    *active = *active ? 0 : 1;
    ui_refresh_runtime_control();
}

/* Opening a tool only builds its UI. Explicit START owns its measurement or
 * output flag, so an unused page does not keep the whole device awake. */
static lv_obj_t *create_runtime_toggle(int *active, lv_coord_t x, lv_coord_t y,
                                       lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_btn_create(lv_scr_act());
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xB02020), 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_add_event_cb(button, runtime_toggle_event_cb, LV_EVENT_CLICKED, active);
    apply_ui_focus_emphasis(button);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "START");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    runtime_toggle_button = button;
    runtime_toggle_active = active;
    return button;
}

static void suppress_ui_input_focus(lv_obj_t *obj)
{
    if (obj == NULL)
    {
        return;
    }

    ensure_ui_focus_styles();
    lv_obj_add_style(obj, &ui_input_focus_suppressed_style, LV_STATE_FOCUSED);
}

static bool focus_obj_precedes(lv_obj_t *candidate, lv_obj_t *reference)
{
    if (candidate == reference)
    {
        return false;
    }
    if (candidate == NULL)
    {
        return false;
    }
    if (reference == NULL)
    {
        return true;
    }

    lv_area_t candidate_area;
    lv_area_t reference_area;
    lv_obj_get_coords(candidate, &candidate_area);
    lv_obj_get_coords(reference, &reference_area);

    lv_coord_t delta_y = candidate_area.y1 - reference_area.y1;
    if (delta_y > UI_FOCUS_ROW_TOLERANCE || delta_y < -UI_FOCUS_ROW_TOLERANCE)
    {
        return delta_y < 0;
    }

    if (candidate_area.x1 != reference_area.x1)
    {
        return candidate_area.x1 < reference_area.x1;
    }

    if (candidate_area.y1 != reference_area.y1)
    {
        return candidate_area.y1 < reference_area.y1;
    }

    if (candidate_area.x2 != reference_area.x2)
    {
        return candidate_area.x2 < reference_area.x2;
    }

    return candidate_area.y2 < reference_area.y2;
}

static void sort_focus_chain_by_layout(lv_obj_t **focus_chain, size_t focus_count)
{
    if (focus_chain == NULL || focus_count < 2)
    {
        return;
    }

    for (size_t index = 1; index < focus_count; ++index)
    {
        lv_obj_t *current = focus_chain[index];
        size_t insert_pos = index;

        while (insert_pos > 0 && focus_obj_precedes(current, focus_chain[insert_pos - 1]))
        {
            focus_chain[insert_pos] = focus_chain[insert_pos - 1];
            --insert_pos;
        }

        focus_chain[insert_pos] = current;
    }
}

static lv_obj_t *find_first_focus_obj(lv_obj_t **focus_chain, size_t focus_count)
{
    if (focus_chain == NULL)
    {
        return NULL;
    }

    for (size_t index = 0; index < focus_count; ++index)
    {
        if (focus_chain[index] != NULL)
        {
            return focus_chain[index];
        }
    }

    return NULL;
}
static void rebuild_focus_group_internal(lv_obj_t *initial_focus, lv_obj_t **focus_chain, size_t focus_count, bool sort_by_layout)
{
    if (group)
    {
        lv_group_del(group);
        group = NULL;
    }
    if (indev_keypad)
    {
        lv_indev_set_group(indev_keypad, NULL);
    }

    group = lv_group_create();
    if (indev_keypad)
    {
        lv_indev_set_group(indev_keypad, group);
    }

    lv_obj_t *active_screen = lv_scr_act();
    if (active_screen != NULL && lv_obj_is_valid(active_screen))
    {
        lv_obj_update_layout(active_screen);
    }

    if (sort_by_layout)
    {
        sort_focus_chain_by_layout(focus_chain, focus_count);
    }
    lv_obj_t *fallback_focus = find_first_focus_obj(focus_chain, focus_count);
    for (size_t index = 0; index < focus_count; ++index)
    {
        if (focus_chain[index] == NULL)
        {
            continue;
        }
        lv_obj_add_flag(focus_chain[index], LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_group_add_obj(group, focus_chain[index]);
    }

    lv_group_set_wrap(group, true);
    if (initial_focus && lv_obj_is_valid(initial_focus))
    {
        lv_group_focus_obj(initial_focus);
    }
    else if (fallback_focus && lv_obj_is_valid(fallback_focus))
    {
        lv_group_focus_obj(fallback_focus);
    }
}

static void rebuild_focus_group(lv_obj_t *initial_focus, lv_obj_t **focus_chain, size_t focus_count)
{
    rebuild_focus_group_internal(initial_focus, focus_chain, focus_count, true);
}

static void rebuild_focus_group_in_order(lv_obj_t *initial_focus, lv_obj_t **focus_chain, size_t focus_count)
{
    rebuild_focus_group_internal(initial_focus, focus_chain, focus_count, false);
}

static const char *log_channel_to_text(app_log_channel_t channel)
{
    switch (channel)
    {
    case APP_LOG_CHANNEL_UART0:
        return "UART0";
    case APP_LOG_CHANNEL_USB:
        return "USB";
    case APP_LOG_CHANNEL_NONE:
        return "NONE";
    default:
        return "UNKNOWN";
    }
}

static const char *log_level_to_text(app_log_level_t level)
{
    switch (level)
    {
    case APP_LOG_LEVEL_ERROR:
        return "ERROR";
    case APP_LOG_LEVEL_WARN:
        return "WARN";
    case APP_LOG_LEVEL_INFO:
        return "INFO";
    case APP_LOG_LEVEL_DEBUG:
        return "DEBUG";
    default:
        return "UNKNOWN";
    }
}

static void build_device_info_snapshot(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
    {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "板卡: ESP32-S3 DevKitC-1\n"
             "编译时间: %s %s\n"
             "BLE 广播名: Exlink\n"
             "串口波特率: %ld\n"
             "日志输出: %s / %s / %s\n"
             "BLE+USB 联动: %s\n"
             "电源预设(raw): 11V=%d  5V=%d  3V3=%d\n"
             "默认上电预设(raw): %d\n"
             "PWM: %d Hz / %d%%\n"
             "舵机角度: %d deg\n"
             "蜂鸣器音量: %.1f%%\n"
             "\n"
             "说明:\n"
             "- 启动阶段日志固定先走 UART0\n"
             "- UI 完成后恢复为当前日志配置\n"
             "- 桥接页打开时日志会临时静音\n"
             "- 长按或右滑可返回主菜单",
             __DATE__,
             __TIME__,
             currentBaudRate,
             get_app_log_enabled() ? "ON" : "OFF",
             log_channel_to_text(get_app_log_channel()),
             log_level_to_text(get_app_log_level()),
             get_ble_usb_passthrough_enabled() ? "ON" : "OFF",
             power_preset_11v,
             power_preset_5v,
             power_preset_3v3,
             power_boot_preset,
             pwm_Freq,
             pwm_Duty,
             servo_angle,
             buzzer_volume_percent);
}

static void ble_set_pin_block(uint8_t index, bool visible, lv_color_t color, const char *number, const char *name)
{
    if (index >= 5 || ble_pin_blocks[index] == NULL)
    {
        return;
    }

    if (visible)
    {
        lv_obj_clear_flag(ble_pin_blocks[index], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ble_pin_blocks[index], color, LV_PART_MAIN);
        lv_label_set_text(ble_pin_number_labels[index], number);
        lv_label_set_text(ble_pin_name_labels[index], name);
    }
    else
    {
        lv_obj_add_flag(ble_pin_blocks[index], LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_ble_mode_ui_from_selection(lv_obj_t *dropdown)
{
    if (dropdown == NULL || !lv_obj_is_valid(dropdown))
    {
        return;
    }

    char selected[32];
    lv_dropdown_get_selected_str(dropdown, selected, sizeof(selected));

    bool is_i2c_bridge = strcmp(selected, "I2C Bridge") == 0;
    bool is_i2c_listen = strcmp(selected, "I2C Listen") == 0;
    bool is_spi = strcmp(selected, "SPI") == 0;
    bool is_uart = !is_i2c_bridge && !is_i2c_listen && !is_spi;

    if (ble_mode_title_label && lv_obj_is_valid(ble_mode_title_label))
    {
        if (is_spi)
        {
            lv_label_set_text(ble_mode_title_label, LV_SYMBOL_BLUETOOTH "BLE SPI");
        }
        else if (is_i2c_bridge)
        {
            lv_label_set_text(ble_mode_title_label, LV_SYMBOL_BLUETOOTH "BLE I2C");
        }
        else if (is_i2c_listen)
        {
            lv_label_set_text(ble_mode_title_label, LV_SYMBOL_BLUETOOTH "BLE LISTEN");
        }
        else
        {
            lv_label_set_text(ble_mode_title_label, LV_SYMBOL_BLUETOOTH "BLE");
        }
    }

    if (wireless_uart_extarea && lv_obj_is_valid(wireless_uart_extarea))
    {
        if (is_spi)
        {
            lv_textarea_set_placeholder_text(wireless_uart_extarea, "x 0x9f");
        }
        else if (is_i2c_bridge)
        {
            lv_textarea_set_placeholder_text(wireless_uart_extarea, "A dev_addr / SCAN / W dev_addr data... / R reg [len] / T dev_addr read_len ... / RAW ...");
        }
        else if (is_i2c_listen)
        {
            lv_textarea_set_placeholder_text(wireless_uart_extarea, "Passive I2C listen mode. Incoming bus traffic will appear here.");
        }
        else
        {
            lv_textarea_set_placeholder_text(wireless_uart_extarea, "Use '\\n' as the end of the usart information");
        }
    }

    if (ble_mode_hint_label && lv_obj_is_valid(ble_mode_hint_label))
    {
        if (is_spi)
        {
            lv_obj_add_flag(ble_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
        }
        else if (is_i2c_bridge || is_i2c_listen || is_uart)
        {
            lv_obj_clear_flag(ble_mode_hint_label, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ble_mode_hint_label, "#FFD700 ROW1#");
        }
    }

    ble_set_pin_block(0, true, lv_color_hex(0x696969), "1", "GND");

    if (is_spi)
    {
        ble_set_pin_block(1, true, lv_color_hex(0x0000FF), "7", "CLK");
        ble_set_pin_block(2, true, lv_color_hex(0x1E90FF), "8", "CS");
        ble_set_pin_block(3, true, lv_color_hex(0xFF8C00), "12", "MISO");
        ble_set_pin_block(4, true, lv_color_hex(0xFF8C00), "13", "MOSI");
        return;
    }

    if (is_i2c_bridge || is_i2c_listen)
    {
        ble_set_pin_block(1, true, lv_color_hex(0x1E90FF), "7", "SCL");
        ble_set_pin_block(2, true, lv_color_hex(0x1E90FF), "8", "SDA");
        ble_set_pin_block(3, false, lv_color_hex(0x000000), "", "");
        ble_set_pin_block(4, false, lv_color_hex(0x000000), "", "");
        return;
    }

    ble_set_pin_block(1, true, lv_color_hex(0xFFA500), "12", "RX");
    ble_set_pin_block(2, true, lv_color_hex(0xFFA500), "13", "TX");
    ble_set_pin_block(3, false, lv_color_hex(0x000000), "", "");
    ble_set_pin_block(4, false, lv_color_hex(0x000000), "", "");
}

static void ble_mode_dropdown_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        update_ble_mode_ui_from_selection(lv_event_get_target(e));
    }
}

static float read_battery_voltage(void)
{
    /* Battery GPIO2 and scope GPIO4 share ADC1. Never run Arduino oneshot
     * while DMA owns ADC1, including its asynchronous startup/teardown. */
    if (scope_adc_owned()) return battery_voltage_filtered;
    uint32_t millivolts_sum = 0;
    for (uint8_t sample = 0; sample < BATTERY_SAMPLE_COUNT; ++sample)
    {
        millivolts_sum += (uint32_t)analogReadMilliVolts(BATTERY_ADC_PIN);
    }

    float battery_voltage = (millivolts_sum / (float)BATTERY_SAMPLE_COUNT) * BATTERY_DIVIDER_GAIN / 1000.0f;
    if (!battery_voltage_initialized)
    {
        battery_voltage_filtered = battery_voltage;
        battery_voltage_initialized = true;
    }
    else
    {
        battery_voltage_filtered += (battery_voltage - battery_voltage_filtered) * BATTERY_FILTER_ALPHA;
    }

    return battery_voltage_filtered;
}

static void update_battery_indicator(void)
{
    if (bat_label == NULL || !lv_obj_is_valid(bat_label) ||
        battery_voltage_label == NULL || !lv_obj_is_valid(battery_voltage_label))
    {
        return;
    }

    float battery_voltage = read_battery_voltage();
    const char *icon = LV_SYMBOL_BATTERY_EMPTY;
    lv_color_t accent_color = lv_color_hex(0xFF4D4F);
    if (battery_voltage >= 3.95f)
    {
        icon = LV_SYMBOL_BATTERY_FULL;
        accent_color = lv_color_hex(0x32CD32);
    }
    else if (battery_voltage >= 3.70f)
    {
        icon = LV_SYMBOL_BATTERY_3;
        accent_color = lv_color_hex(0x7CFC00);
    }
    else if (battery_voltage >= 3.40f)
    {
        icon = LV_SYMBOL_BATTERY_2;
        accent_color = lv_color_hex(0xFFD700);
    }
    else if (battery_voltage >= 3.00f)
    {
        icon = LV_SYMBOL_BATTERY_1;
        accent_color = lv_color_hex(0xFFA500);
    }

    char voltage_text[8];
    snprintf(voltage_text, sizeof(voltage_text), "%.2fV", battery_voltage);

    lv_obj_set_style_text_color(bat_label, accent_color, 0);
    lv_label_set_text(bat_label, icon);
    lv_obj_set_style_text_color(battery_voltage_label, accent_color, 0);
    lv_label_set_text(battery_voltage_label, voltage_text);
    lv_obj_align_to(battery_voltage_label, bat_label, LV_ALIGN_OUT_TOP_MID, 0, -4);
}

static void create_battery_indicator(void)
{
    bat_label = lv_label_create(lv_scr_act());
    lv_label_set_text(bat_label, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_pos(bat_label, 400, 392);
    lv_obj_set_style_text_color(bat_label, lv_color_hex(0x32CD32), 0);
    lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_26, 0);

    battery_voltage_label = lv_label_create(lv_scr_act());
    lv_label_set_text(battery_voltage_label, "0.00V");
    lv_obj_set_style_text_color(battery_voltage_label, lv_color_hex(0x32CD32), 0);
    lv_obj_set_style_text_font(battery_voltage_label, &lv_font_montserrat_10, 0);
    lv_obj_align_to(battery_voltage_label, bat_label, LV_ALIGN_OUT_TOP_MID, 0, -4);

    update_battery_indicator();
}

static int clamp_menu_index(int index)
{
    if (index < 1)
    {
        return 1;
    }
    if (index > MENU_ITEM_COUNT)
    {
        return MENU_ITEM_COUNT;
    }
    return index;
}

static lv_obj_t *get_menu_button_by_index(int index)
{
    switch (clamp_menu_index(index))
    {
    case 1:
        return btn1;
    case 2:
        return btn2;
    case 3:
        return btn3;
    case 4:
        return btn4;
    case 5:
        return btn5;
    case 6:
        return btn6;
    case 7:
        return btn7;
    case 8:
        return btn8;
    case 9:
        return btn9;
    case 10:
        return btn10;
    case 11:
        return btn11;
    case 12:
        return btn_level;
    case 13:
        return btn_environment;
    case 14:
        return btn12;
    case 15:
        return btn13;
    case 16:
        return btn14;
    default:
        return btn1;
    }
}

static int get_menu_index_by_button(lv_obj_t *button)
{
    if (button == btn1)
    {
        return 1;
    }
    if (button == btn2)
    {
        return 2;
    }
    if (button == btn3)
    {
        return 3;
    }
    if (button == btn4)
    {
        return 4;
    }
    if (button == btn5)
    {
        return 5;
    }
    if (button == btn6)
    {
        return 6;
    }
    if (button == btn7)
    {
        return 7;
    }
    if (button == btn8)
    {
        return 8;
    }
    if (button == btn9)
    {
        return 9;
    }
    if (button == btn10)
    {
        return 10;
    }
    if (button == btn11)
    {
        return 11;
    }
    if (button == btn_level)
    {
        return 12;
    }
    if (button == btn_environment)
    {
        return 13;
    }
    if (button == btn12)
    {
        return 14;
    }
    if (button == btn13)
    {
        return 15;
    }
    if (button == btn14)
    {
        return 16;
    }

    return 0;
}

static int get_slider_value_from_menu_index(int index)
{
    return MENU_ITEM_COUNT - clamp_menu_index(index);
}

static void restore_menu_selection(void)
{
    menu_selected_index = clamp_menu_index(menu_selected_index);
    lv_obj_t *selected_btn = get_menu_button_by_index(menu_selected_index);
    if (selected_btn == NULL || panel == NULL || slider == NULL)
    {
        return;
    }

    menu_slider_syncing = true;
    lv_slider_set_value(slider, get_slider_value_from_menu_index(menu_selected_index), LV_ANIM_OFF);
    menu_slider_syncing = false;

    lv_obj_update_layout(panel);
    lv_obj_scroll_to_y(panel, menu_saved_scroll_y, LV_ANIM_OFF);
    lv_obj_clear_flag(selected_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_group_focus_obj(selected_btn);
    lv_obj_add_flag(selected_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_scroll_to_y(panel, menu_saved_scroll_y, LV_ANIM_OFF);
}

void save_menu_state(void)
{
    if (group != NULL)
    {
        lv_obj_t *focused_btn = lv_group_get_focused(group);
        int focused_index = get_menu_index_by_button(focused_btn);
        if (focused_index > 0)
        {
            menu_selected_index = focused_index;
        }
    }

    menu_selected_index = clamp_menu_index(menu_selected_index);

    if (panel != NULL && lv_obj_is_valid(panel))
    {
        menu_saved_scroll_y = lv_obj_get_scroll_y(panel);
    }
}

static uint16_t get_baud_dropdown_index(long baud)
{
    switch (baud)
    {
    case 1200:
        return 0;
    case 2400:
        return 1;
    case 4800:
        return 2;
    case 9600:
        return 3;
    case 19200:
        return 4;
    case 43000:
        return 5;
    case 76800:
        return 6;
    case 115200:
        return 7;
    case 128000:
        return 8;
    case 230400:
        return 9;
    case 256000:
        return 10;
    case 460800:
        return 11;
    case 921600:
        return 12;
    default:
        return 7;
    }
}

void update_label_timer1(lv_timer_t *timer)
{
    lv_obj_t *voltmeter_label1 = (lv_obj_t *)timer->user_data;
    lv_label_set_text(voltmeter_label1, !ina266_flag ? "OFF" :
                      ina266_sample_valid ? voltageStr : "---");
}

void update_label_timer2(lv_timer_t *timer)
{
    lv_obj_t *voltmeter_label2 = (lv_obj_t *)timer->user_data;
    lv_label_set_text(voltmeter_label2, !ina266_flag ? "OFF" :
                      ina266_sample_valid ? currentStr : "---");
}

void update_label_timer3(lv_timer_t *timer)
{
    lv_obj_t *voltmeter_label3 = (lv_obj_t *)timer->user_data;
    lv_label_set_text(voltmeter_label3, !ina266_flag ? "OFF" :
                      ina266_sample_valid ? powerStr : "---");
    if (power_chart_note) lv_label_set_text(power_chart_note, !ina266_flag ?
        "OFF - zero baseline" : ina266_sample_valid ?
        "LIVE - initial zero is reference" : "WAITING - zero baseline");
}

void update_slider_timer(lv_timer_t *timer)
{
    for (int index = 1; index <= MENU_ITEM_COUNT; ++index)
    {
        lv_obj_t *button = get_menu_button_by_index(index);
        if (button && lv_obj_has_state(button, LV_STATE_FOCUSED))
        {
            menu_selected_index = index;
            menu_slider_syncing = true;
            lv_slider_set_value(slider, get_slider_value_from_menu_index(menu_selected_index), LV_ANIM_OFF);
            menu_slider_syncing = false;
            break;
        }
    }
    update_battery_indicator();
}

void FRE_label_update(lv_timer_t *timer)
{
    lv_label_set_text(FRE_label, freqencyStr);
}

void add_data(lv_timer_t *timer)
{
    if (!ina266_flag || !ina266_sample_valid) return;
    lv_chart_set_next_value(volt_chart, timer->user_data, v);
    // lv_label_set_text(label1, voltageStr);
}

void add_data2(lv_timer_t *timer)
{
    if (!ina266_flag || !ina266_sample_valid) return;
    lv_chart_set_next_value(cur_chart, timer->user_data, a * 100);
    // lv_label_set_text(label1, voltageStr);
}

void ui_power_refresh_readings(void)
{
    if (updatelabel_timer1) update_label_timer1(updatelabel_timer1);
    if (updatelabel_timer2) update_label_timer2(updatelabel_timer2);
    if (updatelabel_timer3) update_label_timer3(updatelabel_timer3);
    if (!ina266_sample_valid) {
        if (adddata_timer) lv_chart_set_all_value(volt_chart, adddata_timer->user_data, 0);
        if (adddata_timer2) lv_chart_set_all_value(cur_chart, adddata_timer2->user_data, 0);
    }
}

static void boot_caption_progress(void *object, int32_t elapsed)
{
    lv_obj_t *caption = object;
    lv_obj_t *text = lv_obj_get_child(caption, 0);
    lv_obj_t *block = lv_obj_get_child(caption, 1);
    const lv_coord_t width = boot_caption_white.header.w;
    const lv_coord_t height = boot_caption_white.header.h;
    lv_obj_clear_flag(block, LV_OBJ_FLAG_HIDDEN);
    if (elapsed < 600) {
        /* 500 ms stretch, then 100 ms hold. Text stays hidden beneath it. */
        const uint32_t step = lv_bezier3(LV_MIN(elapsed, 500) * 1024 / 500,
                                         0, 900, 950, 1024);
        lv_obj_add_flag(text, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(block, 0, 0);
        lv_obj_set_size(block, height + ((width - height) * step >> 10), height);
    } else {
        /* Reveal from left to right, without moving the lettering itself. */
        const lv_coord_t revealed = width * LV_MIN(elapsed - 600, 700) / 700;
        lv_obj_clear_flag(text, LV_OBJ_FLAG_HIDDEN);
        if (revealed == width) {
            lv_obj_add_flag(block, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_pos(block, revealed, 0);
            lv_obj_set_size(block, width - revealed, height);
        }
    }
}

static void boot_animation_owner_deleted(lv_event_t *event)
{
    /* The owner also releases the timeline if the boot page is left early.
     * Stopping only matches object addresses; it does not dereference them. */
    lv_anim_timeline_del((lv_anim_timeline_t *)lv_event_get_user_data(event));
}

// 创建启动动画
void create_boot_animation(void)
{
    ui_prepare_page();
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_scroll_to_x(screen, 0, LV_ANIM_OFF);
    lv_obj_scroll_to_y(screen, 0, LV_ANIM_OFF);

    lv_anim_timeline_t *anim_timeline = lv_anim_timeline_create();
    if (anim_timeline == NULL)
    {
        ui_Screen1_screen_init();
        lv_obj_invalidate(screen);
        return;
    }

    /* The completed logo occupies the upper-left corner in native pixels. */
    const lv_coord_t logo_x = 12;
    const lv_coord_t portrait_y = 12;
    const lv_coord_t portrait_x = logo_x +
        (Exlink_png.header.w - boot_portrait_white.header.w) / 2;
    const lv_coord_t wordmark_y = portrait_y + boot_portrait_white.header.h + 6;
    ui_Image1 = lv_img_create(screen);
    lv_img_set_src(ui_Image1, &boot_portrait_white);
    lv_obj_set_style_img_recolor(ui_Image1, lv_color_white(), 0);
    lv_obj_set_style_img_recolor_opa(ui_Image1, LV_OPA_COVER, 0);
    lv_obj_set_size(ui_Image1, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_Image1, -200, portrait_y);
    lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_SCROLLABLE);

    // 初始化动画
    lv_anim_t anim1;
    lv_anim_init(&anim1);                                // 初始化动画结构体
    lv_anim_set_exec_cb(&anim1, anim_cb1);               // 设置动画回调函数
    lv_anim_set_var(&anim1, ui_Image1);                  // 设置动画作用的对象
    lv_anim_set_time(&anim1, 450);                       // 设置动画时间
    lv_anim_set_values(&anim1, -200, portrait_x);
    lv_anim_set_path_cb(&anim1, lv_anim_path_ease_out);

    ui_Image2 = lv_img_create(screen);
    lv_img_set_src(ui_Image2, &Exlink_png); /* Keep the original red Exlink / white TOOL. */
    lv_obj_set_size(ui_Image2, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(ui_Image2, logo_x, EXLINK_DISPLAY_HEIGHT);
    lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_clear_flag(ui_Image2, LV_OBJ_FLAG_SCROLLABLE);
    lv_anim_t anim3;
    lv_anim_init(&anim3);                                      // 初始化动画结构体
    lv_anim_set_var(&anim3, ui_Image2);                        // 设置动画作用的对象
    lv_anim_set_exec_cb(&anim3, anim_cb2);
    lv_anim_set_values(&anim3, EXLINK_DISPLAY_HEIGHT, wordmark_y);
    lv_anim_set_time(&anim3, 450);                             // 动画时间
    lv_anim_set_path_cb(&anim3, lv_anim_path_ease_out);

    lv_obj_t *caption = lv_obj_create(screen);
    lv_obj_remove_style_all(caption);
    lv_obj_set_pos(caption, logo_x, 150);
    lv_obj_set_size(caption, boot_caption_white.header.w, boot_caption_white.header.h);
    lv_obj_clear_flag(caption, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *text = lv_img_create(caption);
    lv_img_set_src(text, &boot_caption_white);
    lv_obj_set_style_img_recolor(text, lv_color_white(), 0);
    lv_obj_set_style_img_recolor_opa(text, LV_OPA_COVER, 0);
    lv_obj_set_pos(text, 0, 0);
    lv_obj_add_flag(text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *block = lv_obj_create(caption);
    lv_obj_remove_style_all(block);
    lv_obj_set_pos(block, 0, 0);
    lv_obj_set_size(block, boot_caption_white.header.h, boot_caption_white.header.h);
    lv_obj_set_style_bg_color(block, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(block, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t reveal;
    lv_anim_init(&reveal);
    lv_anim_set_var(&reveal, caption);
    lv_anim_set_exec_cb(&reveal, boot_caption_progress);
    lv_anim_set_values(&reveal, 0, 1300);
    lv_anim_set_time(&reveal, 1300);
    /* A delayed animation must not display its square before 850 ms. */
    lv_anim_set_early_apply(&reveal, false);

    lv_obj_t *label = lv_label_create(screen); /* Timeline lifetime owner. */
    lv_label_set_text(label, "");                    /* 设置文本内容 */
    lv_obj_set_pos(label, 0, 0);
    lv_obj_add_event_cb(label, boot_animation_owner_deleted, LV_EVENT_DELETE, anim_timeline);
    lv_anim_t anim4;
    lv_anim_init(&anim4);           // 初始化动画结构体
    lv_anim_set_var(&anim4, label); // 设置动画作用的对象
    lv_anim_set_ready_cb(&anim4, anim_end_callback);
    /* No exec callback is needed for the final hold animation. */
    lv_anim_set_time(&anim4, 3600); /* Hold the finished caption until 3.6 s. */

    // 设置动画时间线
    lv_anim_timeline_add(anim_timeline, 0, &anim1);
    lv_anim_timeline_add(anim_timeline, 200, &anim3);
    lv_anim_timeline_add(anim_timeline, 850, &reveal);
    lv_anim_timeline_add(anim_timeline, 0, &anim4);
    // 启动动画时间线
    lv_anim_timeline_start(anim_timeline);
    lv_obj_invalidate(screen);
}

void ui_Screen1_screen_init(void) // 创建主界面
{
    ui_prepare_page();
    ui_home_page = true;
    power_value_11v = NULL;
    power_value_5v = NULL;
    power_value_3v3 = NULL;
    power_value_default = NULL;
    power_keyboard = NULL;
    buzzer_volume = NULL;
    buzzer_keyboard = NULL;
    fre = NULL;
    duty = NULL;
    fre_keyboard = NULL;
    duty_keyboard = NULL;
    servo_slider = NULL;
    servo_angle_text = NULL;
    servo_keyboard = NULL;
    uart_list = NULL;
    wireless_uart_list = NULL;
    uart_extarea = NULL;
    wireless_uart_extarea = NULL;
    i2c_mode_list = NULL;
    spi_extarea = NULL;
    pwm_btn = NULL;
    uart_btn = NULL;
    wireless_uart_btn = NULL;
    spicon = NULL;
    btn11 = NULL;
    btn_level = NULL;
    btn_environment = NULL;
    btn12 = NULL;
    btn13 = NULL;
    btn14 = NULL;
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(panel, 345, 480);
    lv_obj_set_pos(panel, 46, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_remove_style(panel, 0, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN); // 垂直排列子对象
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);

    static lv_style_t focused_style;
    ensure_main_menu_styles(&focused_style);

    btn1 = lv_btn_create(panel);
    lv_obj_set_size(btn1, 323, 160);
    lv_obj_align(btn1, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn1, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn1, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn1, btn1_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn1_label = lv_label_create(btn1);
    lv_obj_align(btn1_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn1_label, "Pin Map");
    lv_obj_set_style_text_font(btn1_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img1 = lv_img_create(btn1);
    lv_img_set_src(img1, &pinmap_png);                       // Replace with your image variable or path
    lv_obj_set_size(img1, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img1, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn2 = lv_btn_create(panel);
    lv_obj_set_size(btn2, 323, 160);
    lv_obj_align(btn2, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn2, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn2, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn2, btn2_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn2_label = lv_label_create(btn2);
    lv_obj_align(btn2_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn2_label, "DC POWER");
    lv_obj_set_style_text_font(btn2_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img2 = lv_img_create(btn2);
    lv_img_set_src(img2, &power_png);                        // Replace with your image variable or path
    lv_obj_set_size(img2, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img2, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn3 = lv_btn_create(panel);
    lv_obj_set_size(btn3, 323, 160);
    lv_obj_align(btn3, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn3, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_event_cb(btn3, btn3_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_style(btn3, &focused_style, LV_STATE_FOCUSED);
    lv_obj_t *btn3_label = lv_label_create(btn3);
    lv_obj_align(btn3_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn3_label, "PWM OUT");
    lv_obj_set_style_text_font(btn3_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img3 = lv_img_create(btn3);
    lv_img_set_src(img3, &pwm_png);                          // Replace with your image variable or path
    lv_obj_set_size(img3, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img3, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn4 = lv_btn_create(panel);
    lv_obj_set_size(btn4, 323, 160);
    lv_obj_align(btn4, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn4, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn4, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn4, btn4_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn4_label = lv_label_create(btn4);
    lv_obj_align(btn4_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn4_label, "Servo Ctrl");
    lv_obj_set_style_text_font(btn4_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img4 = lv_img_create(btn4);
    lv_img_set_src(img4, &servo_png);                        // Replace with your image variable or path
    lv_obj_set_size(img4, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img4, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn5 = lv_btn_create(panel);
    lv_obj_set_size(btn5, 323, 160);
    lv_obj_align(btn5, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn5, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn5, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn5, btn5_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn5_label = lv_label_create(btn5);
    lv_obj_align(btn5_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn5_label, "UART HELPER");
    lv_obj_set_style_text_font(btn5_label, &lv_font_montserrat_16, 0);
    lv_obj_t *img5 = lv_img_create(btn5);
    lv_img_set_src(img5, &usarthelper_png);                  // Replace with your image variable or path
    lv_obj_set_size(img5, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img5, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn6 = lv_btn_create(panel);
    lv_obj_set_size(btn6, 323, 160);
    lv_obj_align(btn6, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn6, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn6, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn6, btn6_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn6_label = lv_label_create(btn6);
    lv_obj_align(btn6_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn6_label, "I2C");
    lv_obj_set_style_text_font(btn6_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img6 = lv_img_create(btn6);
    lv_img_set_src(img6, &i2c_png);                          // Replace with your image variable or path
    lv_obj_set_size(img6, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img6, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn7 = lv_btn_create(panel);
    lv_obj_set_size(btn7, 323, 160);
    lv_obj_align(btn7, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn7, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn7, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn7, btn7_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn7_label = lv_label_create(btn7);
    lv_obj_align(btn7_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn7_label, "SPI");
    lv_obj_set_style_text_font(btn7_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img7 = lv_img_create(btn7);
    lv_img_set_src(img7, &spi_placeholder_png);              // Placeholder image
    lv_obj_set_size(img7, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img7, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn8 = lv_btn_create(panel);
    lv_obj_set_size(btn8, 323, 160);
    lv_obj_align(btn8, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn8, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn8, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn8, btn8_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn8_label = lv_label_create(btn8);
    lv_obj_align(btn8_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn8_label, "Voltmeter");
    lv_obj_set_style_text_font(btn8_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img8 = lv_img_create(btn8);
    lv_img_set_src(img8, &voltmeter_png);                    // Replace with your image variable or path
    lv_obj_set_size(img8, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img8, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn9 = lv_btn_create(panel);
    lv_obj_set_size(btn9, 323, 160);
    lv_obj_align(btn9, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn9, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn9, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn9, btn9_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn9_label = lv_label_create(btn9);
    lv_obj_align(btn9_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn9_label, "Simple DSO");
    lv_obj_set_style_text_font(btn9_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img9 = lv_img_create(btn9);
    lv_img_set_src(img9, &DSO_png);                          // Replace with your image variable or path
    lv_obj_set_size(img9, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img9, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn10 = lv_btn_create(panel);
    lv_obj_set_size(btn10, 323, 160);
    lv_obj_align(btn10, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn10, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn10, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn10, btn10_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn10_label = lv_label_create(btn10);
    lv_obj_align(btn10_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn10_label, "BLE Serial");
    lv_obj_set_style_text_font(btn10_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img10 = lv_img_create(btn10);
    lv_img_set_src(img10, &wireless_png);
    lv_obj_set_size(img10, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(img10, LV_ALIGN_OUT_RIGHT_MID, 189, 0);

    btn11 = lv_btn_create(panel);
    lv_obj_set_size(btn11, 323, 160);
    lv_obj_align(btn11, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn11, &style_rect, 0); // 将样式应用到矩形上
    lv_obj_add_style(btn11, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn11, btn11_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn11_label = lv_label_create(btn11);
    lv_obj_align(btn11_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn11_label, "FRE Count");
    lv_obj_set_style_text_font(btn11_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img11 = lv_img_create(btn11);
    lv_img_set_src(img11, &FREcounter_png);                     // Replace with your image variable or path
    lv_obj_set_size(img11, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_align(img11, LV_ALIGN_OUT_RIGHT_MID, 189, 0);      // Center the image within the button

    btn_level = lv_btn_create(panel);
    lv_obj_set_size(btn_level, 323, 160);
    lv_obj_add_style(btn_level, &style_rect, 0);
    lv_obj_add_style(btn_level, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_level, btn_level_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *level_label = lv_label_create(btn_level);
    lv_obj_set_pos(level_label, 0, 94);
    lv_label_set_text(level_label, "Level");
    lv_obj_set_style_text_font(level_label, &lv_font_montserrat_24, 0);
    lv_obj_t *level_preview = lv_obj_create(btn_level);
    lv_obj_remove_style_all(level_preview);
    lv_obj_set_pos(level_preview, 204, 12);
    lv_obj_set_size(level_preview, 82, 82);
    lv_obj_set_style_radius(level_preview, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(level_preview, 2, 0);
    lv_obj_set_style_border_color(level_preview, lv_color_hex(0xFF0000), 0);
    lv_obj_clear_flag(level_preview, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *level_mark = lv_label_create(level_preview);
    lv_label_set_text(level_mark, "+");
    lv_obj_set_style_text_font(level_mark, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(level_mark, lv_color_hex(0x666666), 0);
    lv_obj_center(level_mark);
    lv_obj_t *level_dot = lv_obj_create(level_preview);
    lv_obj_remove_style_all(level_dot);
    lv_obj_set_pos(level_dot, 40, 23);
    lv_obj_set_size(level_dot, 18, 18);
    lv_obj_set_style_radius(level_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(level_dot, lv_color_hex(0x00FF7F), 0);
    lv_obj_set_style_bg_opa(level_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(level_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    btn_environment = lv_btn_create(panel);
    lv_obj_set_size(btn_environment, 323, 160);
    lv_obj_add_style(btn_environment, &style_rect, 0);
    lv_obj_add_style(btn_environment, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_environment, btn_environment_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *environment_label = lv_label_create(btn_environment);
    lv_obj_set_pos(environment_label, 0, 96);
    lv_label_set_text(environment_label, "Environment");
    lv_obj_set_style_text_font(environment_label, &lv_font_montserrat_20, 0);
    lv_obj_t *environment_preview = lv_label_create(btn_environment);
    lv_obj_set_pos(environment_preview, 210, 10);
    lv_obj_set_width(environment_preview, 78);
    lv_obj_set_style_text_align(environment_preview, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(environment_preview, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(environment_preview, lv_color_hex(0xFF5555), 0);
    lv_label_set_text(environment_preview, "\xC2\xB0" "C\n%RH");

    btn12 = lv_btn_create(panel);
    lv_obj_set_size(btn12, 323, 160);
    lv_obj_align(btn12, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn12, &style_rect, 0);
    lv_obj_add_style(btn12, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn12, btn12_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn12_label = lv_label_create(btn12);
    lv_obj_align(btn12_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn12_label, "2048");
    lv_obj_set_style_text_font(btn12_label, &lv_font_montserrat_24, 0);

    lv_obj_t *preview = lv_obj_create(btn12);
    lv_obj_set_size(preview, 90, 120);
    lv_obj_align(preview, LV_ALIGN_OUT_RIGHT_MID, 189, 0);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_border_width(preview, 0, 0);
    lv_obj_set_style_radius(preview, 8, 0);
    lv_obj_set_style_pad_all(preview, 4, 0);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

    const lv_color_t preview_colors[4] = {
        lv_color_hex(0xEEE4DA),
        lv_color_hex(0xEDE0C8),
        lv_color_hex(0xF2B179),
        lv_color_hex(0xF59563)};
    const char *preview_values[4] = {"2", "4", "8", "16"};
    for (uint8_t preview_index = 0; preview_index < 4; ++preview_index)
    {
        lv_obj_t *tile = lv_obj_create(preview);
        lv_obj_set_size(tile, 36, 48);
        lv_obj_set_pos(tile, 6 + (preview_index % 2) * 42, 8 + (preview_index / 2) * 56);
        lv_obj_set_style_bg_color(tile, preview_colors[preview_index], 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_radius(tile, 4, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *tile_label = lv_label_create(tile);
        lv_label_set_text(tile_label, preview_values[preview_index]);
        lv_obj_set_style_text_color(tile_label, lv_color_hex(0x5F574F), 0);
        lv_obj_set_style_text_font(tile_label, &lv_font_montserrat_10, 0);
        lv_obj_center(tile_label);
    }

    btn13 = lv_btn_create(panel);
    lv_obj_set_size(btn13, 323, 160);
    lv_obj_align(btn13, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn13, &style_rect, 0);
    lv_obj_add_style(btn13, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn13, btn13_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn13_label = lv_label_create(btn13);
    lv_obj_align(btn13_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn13_label, "Settings");
    lv_obj_set_style_text_font(btn13_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img13 = lv_img_create(btn13);
    lv_img_set_src(img13, &settings_png);
    lv_obj_set_size(img13, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(img13, LV_ALIGN_OUT_RIGHT_MID, 189, 0);

    btn14 = lv_btn_create(panel);
    lv_obj_set_size(btn14, 323, 160);
    lv_obj_align(btn14, LV_ALIGN_OUT_RIGHT_TOP, 0, 0);
    lv_obj_add_style(btn14, &style_rect, 0);
    lv_obj_add_style(btn14, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn14, btn14_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn14_label = lv_label_create(btn14);
    lv_obj_align(btn14_label, LV_ALIGN_OUT_RIGHT_MID, 0, 36);
    lv_label_set_text(btn14_label, "Device INFO");
    lv_obj_set_style_text_font(btn14_label, &lv_font_montserrat_20, 0);
    lv_obj_t *img14 = lv_img_create(btn14);
    lv_img_set_src(img14, &readme_png);
    lv_obj_set_size(img14, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(img14, LV_ALIGN_OUT_RIGHT_MID, 189, 0);

    slider = lv_slider_create(lv_scr_act());
    lv_obj_set_size(slider, 23, 180);
    lv_obj_set_pos(slider, 413, 60);
    lv_slider_set_range(slider, 0, MENU_ITEM_COUNT - 1);
    lv_slider_set_value(slider, MENU_ITEM_COUNT - 1, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xFF0000), LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x333333), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    slider_update_timer = lv_timer_create(update_slider_timer, 300, NULL);
    lv_obj_update_snap(panel, LV_ANIM_OFF);

    create_battery_indicator();

    lv_obj_t *menu_focus_chain[] = {
        btn1,
        btn2,
        btn3,
        btn4,
        btn5,
        btn6,
        btn7,
        btn8,
        btn9,
        btn10,
        btn11,
        btn_level,
        btn_environment,
        btn12,
        btn13,
        btn14,
    };
    rebuild_focus_group(NULL, menu_focus_chain, sizeof(menu_focus_chain) / sizeof(menu_focus_chain[0]));
    restore_menu_selection();
    lv_obj_add_event_cb(lv_scr_act(), main_menu_longpress_event_handler, LV_EVENT_LONG_PRESSED, NULL);
}

void pinmap_init(void)
{

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *name_label1 = lv_label_create(lv_scr_act());         // 将文本标签添加到圆角矩形上
    lv_label_set_text(name_label1, "#FFD700 ROW1:MCU and power#"); // 设置文本内容
    lv_obj_set_style_text_font(name_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(name_label1, 53, 52);
    lv_label_set_recolor(name_label1, true);

    lv_obj_t *name_label2 = lv_label_create(lv_scr_act());           // 将文本标签添加到圆角矩形上
    lv_label_set_text(name_label2, "#00FFFF ROW2:DLA and DAPlink#"); // 设置文本内容
    lv_obj_set_style_text_font(name_label2, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(name_label2, 53, 120);
    lv_label_set_recolor(name_label2, true);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 32, 130);
    lv_obj_set_pos(rounded_rect1, 47, 220);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "GD");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "1");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 32, 130);
    lv_obj_set_pos(rounded_rect2, 79, 220);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0xA52A2A), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "IO");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "2");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect3 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect3, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect3, 32, 130);
    lv_obj_set_pos(rounded_rect3, 110, 220);
    lv_obj_set_style_radius(rounded_rect3, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect3, lv_color_hex(0xFF0000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect3_label1 = lv_label_create(rounded_rect3);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label1, "3V");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect3_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect3_label2 = lv_label_create(rounded_rect3); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label2, "3");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect3_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect4 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect4, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect4, 32, 130);
    lv_obj_set_pos(rounded_rect4, 142, 220);
    lv_obj_set_style_radius(rounded_rect4, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect4, lv_color_hex(0xFF0000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect4_label1 = lv_label_create(rounded_rect4);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect4_label1, "5V");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect4_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect4_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect4_label2 = lv_label_create(rounded_rect4); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect4_label2, "4");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect4_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect4_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect5 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect5, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect5, 32, 130);
    lv_obj_set_pos(rounded_rect5, 173, 220);
    lv_obj_set_style_radius(rounded_rect5, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect5, lv_color_hex(0x008000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect5_label1 = lv_label_create(rounded_rect5);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect5_label1, "CO");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect5_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect5_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect5_label2 = lv_label_create(rounded_rect5); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect5_label2, "5");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect5_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect5_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect6 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect6, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect6, 32, 130);
    lv_obj_set_pos(rounded_rect6, 205, 220);
    lv_obj_set_style_radius(rounded_rect6, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect6, lv_color_hex(0x008000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect6_label1 = lv_label_create(rounded_rect6);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect6_label1, "PW");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect6_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect6_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect6_label2 = lv_label_create(rounded_rect6); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect6_label2, "6");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect6_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect6_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect7 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect7, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect7, 32, 130);
    lv_obj_set_pos(rounded_rect7, 236, 220);
    lv_obj_set_style_radius(rounded_rect7, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect7, lv_color_hex(0x0000FF), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect7_label1 = lv_label_create(rounded_rect7);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect7_label1, "SL");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect7_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect7_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect7_label2 = lv_label_create(rounded_rect7); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect7_label2, "7");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect7_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect7_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect8 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect8, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect8, 32, 130);
    lv_obj_set_pos(rounded_rect8, 268, 220);
    lv_obj_set_style_radius(rounded_rect8, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect8, lv_color_hex(0x0000FF), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect8_label1 = lv_label_create(rounded_rect8);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect8_label1, "SA");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect8_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect8_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect8_label2 = lv_label_create(rounded_rect8); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect8_label2, "8");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect8_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect8_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect9 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect9, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect9, 32, 130);
    lv_obj_set_pos(rounded_rect9, 299, 220);
    lv_obj_set_style_radius(rounded_rect9, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect9, lv_color_hex(0xC71585), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect9_label1 = lv_label_create(rounded_rect9);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect9_label1, "DI");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect9_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect9_label1, LV_ALIGN_TOP_MID, 0, 0);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect9_label2 = lv_label_create(rounded_rect9); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect9_label2, "9");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect9_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect9_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect10 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect10, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect10, 32, 130);
    lv_obj_set_pos(rounded_rect10, 331, 220);
    lv_obj_set_style_radius(rounded_rect10, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect10, lv_color_hex(0xC71585), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect10_label1 = lv_label_create(rounded_rect10);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect10_label1, "CL");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect10_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect10_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect10_label2 = lv_label_create(rounded_rect10); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect10_label2, "10");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect10_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect10_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect11 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect11, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect11, 32, 130);
    lv_obj_set_pos(rounded_rect11, 362, 220);
    lv_obj_set_style_radius(rounded_rect11, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect11, lv_color_hex(0xC71585), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect11_label1 = lv_label_create(rounded_rect11);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect11_label1, "RS");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect11_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect11_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect11_label2 = lv_label_create(rounded_rect11); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect11_label2, "11");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect11_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect11_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect12 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect12, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect12, 32, 130);
    lv_obj_set_pos(rounded_rect12, 394, 220);
    lv_obj_set_style_radius(rounded_rect12, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect12, lv_color_hex(0xFF8C00), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect12_label1 = lv_label_create(rounded_rect12);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect12_label1, "RX");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect12_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect12_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect12_label2 = lv_label_create(rounded_rect12); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect12_label2, "12");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect12_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect12_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect13 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect13, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect13, 32, 130);
    lv_obj_set_pos(rounded_rect13, 425, 220);
    lv_obj_set_style_radius(rounded_rect13, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect13, lv_color_hex(0xFF8C00), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect13_label1 = lv_label_create(rounded_rect13);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect13_label1, "TX");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect13_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect13_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect13_label2 = lv_label_create(rounded_rect13); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect13_label2, "13");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect13_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect13_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect14 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect14, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect14, 32, 130);
    lv_obj_set_pos(rounded_rect14, 47, 350);
    lv_obj_set_style_radius(rounded_rect14, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect14, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect14_label1 = lv_label_create(rounded_rect14);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect14_label1, "GD");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect14_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect14_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect14_label2 = lv_label_create(rounded_rect14); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect14_label2, "14");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect14_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect14_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect15 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect15, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect15, 32, 130);
    lv_obj_set_pos(rounded_rect15, 79, 350);
    lv_obj_set_style_radius(rounded_rect15, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect15, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect15_label1 = lv_label_create(rounded_rect15);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect15_label1, "C0");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect15_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect15_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect15_label2 = lv_label_create(rounded_rect15); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect15_label2, "15");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect15_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect15_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect16 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect16, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect16, 32, 130);
    lv_obj_set_pos(rounded_rect16, 110, 350);
    lv_obj_set_style_radius(rounded_rect16, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect16, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect16_label1 = lv_label_create(rounded_rect16);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect16_label1, "C1");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect16_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect16_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect16_label2 = lv_label_create(rounded_rect16); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect16_label2, "16");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect16_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect16_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect17 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect17, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect17, 32, 130);
    lv_obj_set_pos(rounded_rect17, 142, 350);
    lv_obj_set_style_radius(rounded_rect17, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect17, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect17_label1 = lv_label_create(rounded_rect17);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect17_label1, "C2");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect17_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect17_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect17_label2 = lv_label_create(rounded_rect17); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect17_label2, "17");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect17_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect17_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect18 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect18, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect18, 32, 130);
    lv_obj_set_pos(rounded_rect18, 173, 350);
    lv_obj_set_style_radius(rounded_rect18, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect18, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect18_label1 = lv_label_create(rounded_rect18);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect18_label1, "C3");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect18_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect18_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect18_label2 = lv_label_create(rounded_rect18); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect18_label2, "18");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect18_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect18_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect19 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect19, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect19, 32, 130);
    lv_obj_set_pos(rounded_rect19, 205, 350);
    lv_obj_set_style_radius(rounded_rect19, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect19, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect19_label1 = lv_label_create(rounded_rect19);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect19_label1, "C4");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect19_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect19_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect19_label2 = lv_label_create(rounded_rect19); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect19_label2, "19");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect19_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect19_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect20 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect20, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect20, 32, 130);
    lv_obj_set_pos(rounded_rect20, 236, 350);
    lv_obj_set_style_radius(rounded_rect20, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect20, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect20_label1 = lv_label_create(rounded_rect20);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect20_label1, "C5");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect20_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect20_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect20_label2 = lv_label_create(rounded_rect20); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect20_label2, "20");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect20_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect20_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect21 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect21, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect21, 32, 130);
    lv_obj_set_pos(rounded_rect21, 268, 350);
    lv_obj_set_style_radius(rounded_rect21, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect21, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect21_label1 = lv_label_create(rounded_rect21);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect21_label1, "C6");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect21_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect21_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect21_label2 = lv_label_create(rounded_rect21); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect21_label2, "21");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect21_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect21_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect22 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect22, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect22, 32, 130);
    lv_obj_set_pos(rounded_rect22, 299, 350);
    lv_obj_set_style_radius(rounded_rect22, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect22, lv_color_hex(0x20B2AA), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect22_label1 = lv_label_create(rounded_rect22);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect22_label1, "C7");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect22_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect22_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect22_label2 = lv_label_create(rounded_rect22); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect22_label2, "22");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect22_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect22_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect23 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect23, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect23, 32, 130);
    lv_obj_set_pos(rounded_rect23, 331, 350);
    lv_obj_set_style_radius(rounded_rect23, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect23, lv_color_hex(0x006400), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect23_label1 = lv_label_create(rounded_rect23);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect23_label1, "DI");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect23_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect23_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect23_label2 = lv_label_create(rounded_rect23); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect23_label2, "23");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect23_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect23_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect24 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect24, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect24, 32, 130);
    lv_obj_set_pos(rounded_rect24, 362, 350);
    lv_obj_set_style_radius(rounded_rect24, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect24, lv_color_hex(0x006400), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect24_label1 = lv_label_create(rounded_rect24);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect24_label1, "CL");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect24_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect24_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect24_label2 = lv_label_create(rounded_rect24); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect24_label2, "24");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect24_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect24_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect25 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect25, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect25, 32, 130);
    lv_obj_set_pos(rounded_rect25, 394, 350);
    lv_obj_set_style_radius(rounded_rect25, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect25, lv_color_hex(0xFF1493), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect25_label1 = lv_label_create(rounded_rect25);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect25_label1, "RX");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect25_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect25_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect25_label2 = lv_label_create(rounded_rect25); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect25_label2, "25");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect25_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect25_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形

    lv_obj_t *rounded_rect26 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect26, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect26, 32, 130);
    lv_obj_set_pos(rounded_rect26, 425, 350);
    lv_obj_set_style_radius(rounded_rect26, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect26, lv_color_hex(0xFF1493), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect26_label1 = lv_label_create(rounded_rect26);               // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect26_label1, "TX");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect26_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect26_label1, LV_ALIGN_TOP_MID, 0, 0);       // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect26_label2 = lv_label_create(rounded_rect26); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect26_label2, "26");                    // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect26_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect26_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
}

void power_init(void)
{
    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    ina266_flag = 0;
    digitalWrite(1, LOW);
    ina266_reset_measurement();

    lv_obj_t *volt = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(volt, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(volt, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(volt, 68, 20);
    lv_label_set_text(volt, "OFF");

    lv_obj_t *cur = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(cur, lv_color_hex(0x00FF7F), 0);
    lv_obj_set_style_text_font(cur, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(cur, 68, 70);
    lv_label_set_text(cur, "OFF");

    lv_obj_t *watt = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(watt, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_text_font(watt, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(watt, 68, 120);
    lv_label_set_text(watt, "OFF");

    updatelabel_timer1 = lv_timer_create(update_label_timer1, 100, volt);
    updatelabel_timer2 = lv_timer_create(update_label_timer2, 100, cur);
    updatelabel_timer3 = lv_timer_create(update_label_timer3, 100, watt);

    power_chart_note = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(power_chart_note, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(power_chart_note, lv_color_hex(0xC0C0C0), 0);
    lv_obj_set_pos(power_chart_note, 53, 170);
    lv_label_set_text(power_chart_note, "OFF - zero baseline");

    lv_obj_t *V = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(V, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(V, 197, 20);
    lv_label_set_text(V, "#FF0000 V#"); // 设置文本内容
    lv_label_set_recolor(V, true);

    lv_obj_t *A = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(A, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(A, 197, 70);
    lv_label_set_text(A, "#00FF7F A#"); // 设置文本内容
    lv_label_set_recolor(A, true);

    lv_obj_t *W = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(W, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(W, 190, 120);
    lv_label_set_text(W, "#00FFFF W#"); // 设置文本内容
    lv_label_set_recolor(W, true);

    volt_chart = lv_chart_create(lv_scr_act());
    lv_obj_set_style_bg_color(volt_chart, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_line_color(volt_chart, lv_color_hex(0x696969), LV_PART_MAIN);
    lv_obj_set_size(volt_chart, 390, 100);
    lv_obj_set_pos(volt_chart, 53, 200);
    lv_chart_set_point_count(volt_chart, 15);
    lv_chart_set_axis_tick(volt_chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 3, 2, false, 30);
    lv_chart_set_range(volt_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 12);
    lv_obj_set_style_width(volt_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(volt_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_update_mode(volt_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_series_t *ser1 = lv_chart_add_series(volt_chart, lv_color_hex(0xFF0000), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(volt_chart, ser1, 0);
    adddata_timer = lv_timer_create(add_data, 100, ser1);
    lv_chart_refresh(volt_chart);
    lv_obj_add_event_cb(volt_chart, event_handler_back, LV_EVENT_ALL, NULL);

    cur_chart = lv_chart_create(lv_scr_act());
    lv_obj_set_style_bg_color(cur_chart, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_line_color(cur_chart, lv_color_hex(0x696969), LV_PART_MAIN);
    lv_obj_set_size(cur_chart, 390, 100);
    lv_obj_set_pos(cur_chart, 53, 306);
    lv_chart_set_point_count(cur_chart, 15);
    lv_chart_set_axis_tick(cur_chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 3, 2, false, 30);
    lv_chart_set_range(cur_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_obj_set_style_width(cur_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(cur_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_update_mode(cur_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_series_t *ser2 = lv_chart_add_series(cur_chart, lv_color_hex(0x00FF7F), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(cur_chart, ser2, 0);
    adddata_timer2 = lv_timer_create(add_data2, 100, ser2);
    lv_chart_refresh(cur_chart);
    lv_obj_add_event_cb(cur_chart, event_handler_back, LV_EVENT_ALL, NULL);

    // 设置聚焦状态的边框
    static lv_style_t focused_style;
    static lv_style_t powerbtn_style;
    static bool power_styles_ready = false;
    if (!power_styles_ready)
    {
        lv_style_init(&focused_style);
        lv_style_set_border_color(&focused_style, lv_color_hex(0xFFD700));
        lv_style_set_border_width(&focused_style, 4);
        lv_style_set_outline_color(&focused_style, lv_color_hex(0xFFD700));
        lv_style_set_outline_width(&focused_style, 3);
        lv_style_set_outline_pad(&focused_style, 2);
        lv_style_set_shadow_color(&focused_style, lv_color_hex(0xFFD700));
        lv_style_set_shadow_width(&focused_style, 18);
        lv_style_set_shadow_opa(&focused_style, LV_OPA_50);

        lv_style_init(&powerbtn_style);
        lv_style_set_bg_color(&powerbtn_style, lv_color_hex(0x000000));
        lv_style_set_border_color(&powerbtn_style, lv_color_hex(0x808080));
        lv_style_set_border_width(&powerbtn_style, 2);
        power_styles_ready = true;
    }

    lv_obj_t *poweron = lv_btn_create(lv_scr_act());
    lv_obj_set_size(poweron, 62, 166);
    lv_obj_set_pos(poweron, 382, 20);
    lv_obj_add_style(poweron, &powerbtn_style, 0); // 将样式应用到矩形上
    lv_obj_add_style(poweron, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(poweron, poweronbtn_event_cb, LV_EVENT_CLICKED, NULL);
    poweron_label = lv_label_create(poweron);
    lv_obj_set_style_text_color(poweron_label, lv_color_hex(0xFF0000), 0);
    lv_obj_align(poweron_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(poweron_label, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(poweron_label, &lv_font_montserrat_28, 0);

    lv_obj_t *VUP = lv_btn_create(lv_scr_act());
    lv_obj_set_size(VUP, 60, 80);
    lv_obj_set_pos(VUP, 313, 20);
    lv_obj_add_style(VUP, &powerbtn_style, 0); // 将样式应用到矩形上
    lv_obj_add_style(VUP, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(VUP, VUPbtn_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_style(poweron, &focused_style, LV_STATE_FOCUSED);
    // lv_obj_add_event_cb(poweron, btn1_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *VUP_label = lv_label_create(VUP);
    lv_obj_set_style_text_color(VUP_label, lv_color_hex(0x87CEEB), 0);
    lv_obj_align(VUP_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(VUP_label, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(VUP_label, &lv_font_montserrat_24, 0);

    lv_obj_t *VDOWN = lv_btn_create(lv_scr_act());
    lv_obj_set_size(VDOWN, 60, 80);
    lv_obj_set_pos(VDOWN, 313, 102);
    lv_obj_add_style(VDOWN, &powerbtn_style, 0); // 将样式应用到矩形上
    lv_obj_add_style(VDOWN, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(VDOWN, VDOWNbtn_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_style(poweron, &focused_style, LV_STATE_FOCUSED);
    // lv_obj_add_event_cb(poweron, btn1_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *VDOWN_label = lv_label_create(VDOWN);
    lv_obj_set_style_text_color(VDOWN_label, lv_color_hex(0x87CEEB), 0);
    lv_obj_align(VDOWN_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(VDOWN_label, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(VDOWN_label, &lv_font_montserrat_24, 0);

    lv_obj_t *V11 = lv_btn_create(lv_scr_act());
    lv_obj_set_size(V11, 63, 52);
    lv_obj_set_pos(V11, 241, 20);
    lv_obj_add_style(V11, &powerbtn_style, 0); // 将样式应用到矩形上
    lv_obj_add_style(V11, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(V11, V11btn_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_style(poweron, &focused_style, LV_STATE_FOCUSED);
    // lv_obj_add_event_cb(poweron, btn1_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *V11_label = lv_label_create(V11);
    lv_obj_set_style_text_color(V11_label, lv_color_hex(0xFFD700), 0);
    lv_obj_align(V11_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(V11_label, "11V");
    lv_obj_set_style_text_font(V11_label, &lv_font_montserrat_20, 0);

    lv_obj_t *V5 = lv_btn_create(lv_scr_act());
    lv_obj_set_size(V5, 63, 52);
    lv_obj_set_pos(V5, 241, 74);
    lv_obj_add_style(V5, &powerbtn_style, 0); // 将样式应用到矩形上
    lv_obj_add_style(V5, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(V5, V5btn_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_style(poweron, &focused_style, LV_STATE_FOCUSED);
    // lv_obj_add_event_cb(poweron, btn1_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *V5_label = lv_label_create(V5);
    lv_obj_set_style_text_color(V5_label, lv_color_hex(0xFFD700), 0);
    lv_obj_align(V5_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(V5_label, "5V");
    lv_obj_set_style_text_font(V5_label, &lv_font_montserrat_20, 0);

    lv_obj_t *V3 = lv_btn_create(lv_scr_act());
    lv_obj_set_size(V3, 63, 52);
    lv_obj_set_pos(V3, 241, 128);
    lv_obj_add_style(V3, &powerbtn_style, 0); // 将样式应用到矩形上
    lv_obj_add_style(V3, &focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(V3, V3btn_event_cb, LV_EVENT_CLICKED, NULL);
    // lv_obj_add_style(poweron, &focused_style, LV_STATE_FOCUSED);
    // lv_obj_add_event_cb(poweron, btn1_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *V3_label = lv_label_create(V3);
    lv_obj_set_style_text_color(V3_label, lv_color_hex(0xFFD700), 0);
    lv_obj_align(V3_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(V3_label, "3V3");
    lv_obj_set_style_text_font(V3_label, &lv_font_montserrat_18, 0);

    lv_obj_t *power_focus_chain[] = {
        V11,
        V5,
        V3,
        VUP,
        VDOWN,
        poweron,
    };
    rebuild_focus_group_in_order(V11, power_focus_chain, sizeof(power_focus_chain) / sizeof(power_focus_chain[0]));

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x8B0000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "2");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "OUT");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *volt_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(volt_label1, 308, 420);
    lv_label_set_recolor(volt_label1, true);
}

void pwm_init(void)
{
    servo_flag = 0;

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *pwm = lv_img_create(lv_scr_act());
    lv_img_set_src(pwm, &pwmint_png);                       // Replace with your image variable or path
    lv_obj_set_size(pwm, LV_SIZE_CONTENT, LV_SIZE_CONTENT); // Set image size
    lv_obj_set_pos(pwm, 98, 220);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x8B0000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "6");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "PWM");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *volt_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(volt_label1, 308, 420);
    lv_label_set_recolor(volt_label1, true);

    fre = lv_textarea_create(lv_scr_act()); /* 创建文本框 */
    lv_textarea_set_placeholder_text(fre, "0-100K");
    lv_obj_set_style_text_font(fre, &lv_font_montserrat_20, LV_PART_MAIN); /* 设置字体 */
    lv_textarea_set_one_line(fre, true);                                   /* 设置单行模式 */
    lv_obj_set_size(fre, 150, 90);
    lv_obj_set_pos(fre, 146, 20);
    {
        char pwm_freq_str[16];
        snprintf(pwm_freq_str, sizeof(pwm_freq_str), "%d", pwm_Freq);
        lv_textarea_set_text(fre, pwm_freq_str);
    }
    suppress_ui_input_focus(fre);

    duty = lv_textarea_create(lv_scr_act()); /* 创建文本框 */
    lv_textarea_set_placeholder_text(duty, "0-100");
    lv_obj_set_style_text_font(duty, &lv_font_montserrat_20, LV_PART_MAIN); /* 设置字体 */
    lv_textarea_set_one_line(duty, true);                                   /* 设置单行模式 */
    lv_obj_set_size(duty, 150, 90);
    lv_obj_set_pos(duty, 146, 120);
    {
        char pwm_duty_str[8];
        snprintf(pwm_duty_str, sizeof(pwm_duty_str), "%d", pwm_Duty);
        lv_textarea_set_text(duty, pwm_duty_str);
    }
    suppress_ui_input_focus(duty);

    lv_obj_t *fre_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(fre_label1, "#FFD700 FRE#");        // 设置文本内容
    lv_obj_set_style_text_font(fre_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(fre_label1, 53, 40);
    lv_label_set_recolor(fre_label1, true);

    lv_obj_t *duty_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(duty_label1, "#87CEFA DUTY#");       // 设置文本内容
    lv_obj_set_style_text_font(duty_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(duty_label1, 53, 140);
    lv_label_set_recolor(duty_label1, true);

    lv_obj_t *fre_label2 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(fre_label2, "#FFD700 Hz#");         // 设置文本内容
    lv_obj_set_style_text_font(fre_label2, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(fre_label2, 308, 40);
    lv_label_set_recolor(fre_label2, true);

    lv_obj_t *duty_label2 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(duty_label2, "#87CEFA %#");          // 设置文本内容
    lv_obj_set_style_text_font(duty_label2, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(duty_label2, 308, 130);
    lv_label_set_recolor(duty_label2, true);

    fre_keyboard = lv_keyboard_create(lv_scr_act());
    duty_keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(fre_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_mode(duty_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(fre_keyboard, fre);
    lv_keyboard_set_textarea(duty_keyboard, duty);
    lv_obj_add_flag(fre_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(duty_keyboard, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(fre, fretext_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(duty, dutytext_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(fre_keyboard, clear_keyboard_event_handler, LV_EVENT_VALUE_CHANGED, NULL);  /* 设置键盘事件回调 */
    lv_obj_add_event_cb(duty_keyboard, clear_keyboard_event_handler, LV_EVENT_VALUE_CHANGED, NULL); /* 设置键盘事件回调 */

    static lv_style_t pwm_style;
    static bool pwm_style_ready = false;
    if (!pwm_style_ready)
    {
        lv_style_init(&pwm_style);
        lv_style_set_bg_color(&pwm_style, lv_color_hex(0xFF0000));
        lv_style_set_border_color(&pwm_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&pwm_style, 2);
        pwm_style_ready = true;
    }

    pwm_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(pwm_btn, &pwm_style, 0); // 将样式应用到矩形上
    lv_obj_set_size(pwm_btn, 104, 100);
    lv_obj_set_pos(pwm_btn, 353, 60);
    lv_obj_add_event_cb(pwm_btn, pwm_btn_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(pwm_btn);
    lv_obj_t *pwm_btn_label = lv_label_create(pwm_btn);
    lv_obj_set_style_text_color(pwm_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(pwm_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(pwm_btn_label, "OPEN");
    lv_obj_set_style_text_font(pwm_btn_label, &lv_font_montserrat_20, 0);

    lv_obj_t *pwm_focus_chain[] = {pwm_btn};
    rebuild_focus_group(pwm_btn, pwm_focus_chain, sizeof(pwm_focus_chain) / sizeof(pwm_focus_chain[0]));
}

void servo_init(void)
{
    pwm_flag = 0;
    servo_flag = 0;

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *servo_plus_mark = lv_label_create(lv_scr_act());
    lv_label_set_text(servo_plus_mark, "+");
    lv_obj_set_style_text_color(servo_plus_mark, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(servo_plus_mark, &lv_font_montserrat_44, 0);
    lv_obj_set_pos(servo_plus_mark, 425, 164);

    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "#00FF7F Servo Control#");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 56, 16);
    lv_label_set_recolor(title, true);

    lv_obj_t *angle_label = lv_label_create(lv_scr_act());
    lv_label_set_text(angle_label, "#FFD700 Angle#");
    lv_obj_set_style_text_font(angle_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(angle_label, 56, 96);
    lv_label_set_recolor(angle_label, true);

    servo_angle_text = lv_textarea_create(lv_scr_act());
    lv_textarea_set_one_line(servo_angle_text, true);
    lv_textarea_set_placeholder_text(servo_angle_text, "0-180");
    lv_obj_set_size(servo_angle_text, 111, 80);
    lv_obj_set_pos(servo_angle_text, 188, 84);
    lv_obj_set_style_text_font(servo_angle_text, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(servo_angle_text, servo_angle_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(servo_angle_text, servo_angle_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(servo_angle_text, servo_angle_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(servo_angle_text, servo_angle_event_cb, LV_EVENT_DEFOCUSED, NULL);
    suppress_ui_input_focus(servo_angle_text);

    lv_obj_t *angle_unit = lv_label_create(lv_scr_act());
    lv_label_set_text(angle_unit, "#FFD700 deg#");
    lv_obj_set_style_text_font(angle_unit, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(angle_unit, 308, 100);
    lv_label_set_recolor(angle_unit, true);

    static lv_style_t servo_btn_style;
    static bool servo_btn_style_ready = false;
    if (!servo_btn_style_ready)
    {
        lv_style_init(&servo_btn_style);
        lv_style_set_bg_color(&servo_btn_style, lv_color_hex(0xFF0000));
        lv_style_set_border_color(&servo_btn_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&servo_btn_style, 2);
        lv_style_set_radius(&servo_btn_style, 8);
        servo_btn_style_ready = true;
    }

    lv_obj_t *minus_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(minus_btn, &servo_btn_style, 0);
    lv_obj_set_size(minus_btn, 102, 64);
    lv_obj_set_pos(minus_btn, 56, 252);
    lv_obj_add_event_cb(minus_btn, servo_minus_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(minus_btn);
    lv_obj_t *minus_label = lv_label_create(minus_btn);
    lv_label_set_text(minus_label, "-");
    lv_obj_set_style_text_font(minus_label, &lv_font_montserrat_22, 0);
    lv_obj_align(minus_label, LV_ALIGN_CENTER, 0, -2);

    lv_obj_t *plus_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(plus_btn, &servo_btn_style, 0);
    lv_obj_set_size(plus_btn, 102, 64);
    lv_obj_set_pos(plus_btn, 170, 252);
    lv_obj_add_event_cb(plus_btn, servo_plus_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(plus_btn);
    lv_obj_t *plus_label = lv_label_create(plus_btn);
    lv_label_set_text(plus_label, "+");
    lv_obj_set_style_text_font(plus_label, &lv_font_montserrat_22, 0);
    lv_obj_align(plus_label, LV_ALIGN_CENTER, 0, -2);

    servo_slider = lv_slider_create(lv_scr_act());
    lv_obj_set_size(servo_slider, 318, 24);
    lv_obj_set_pos(servo_slider, 56, 188);
    lv_slider_set_range(servo_slider, 0, 180);
    lv_obj_set_style_bg_color(servo_slider, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_bg_color(servo_slider, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(servo_slider, lv_color_hex(0xFFD700), LV_PART_KNOB);
    lv_obj_add_event_cb(servo_slider, servo_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(servo_slider);

    lv_obj_t *slider_min = lv_label_create(lv_scr_act());
    lv_label_set_text(slider_min, "0");
    lv_obj_set_style_text_font(slider_min, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(slider_min, 56, 216);

    lv_obj_t *slider_max = lv_label_create(lv_scr_act());
    lv_label_set_text(slider_max, "180");
    lv_obj_set_style_text_font(slider_max, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(slider_max, 320, 216);

    lv_obj_t *preset0_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(preset0_btn, &servo_btn_style, 0);
    lv_obj_set_size(preset0_btn, 138, 64);
    lv_obj_set_pos(preset0_btn, 56, 332);
    lv_obj_add_event_cb(preset0_btn, servo_preset_0_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(preset0_btn);
    lv_obj_t *preset0_label = lv_label_create(preset0_btn);
    lv_label_set_text(preset0_label, "0 deg");
    lv_obj_set_style_text_font(preset0_label, &lv_font_montserrat_14, 0);
    lv_obj_center(preset0_label);

    lv_obj_t *preset_center_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(preset_center_btn, &servo_btn_style, 0);
    lv_obj_set_size(preset_center_btn, 138, 64);
    lv_obj_set_pos(preset_center_btn, 203, 332);
    lv_obj_add_event_cb(preset_center_btn, servo_preset_center_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(preset_center_btn);
    lv_obj_t *preset_center_label = lv_label_create(preset_center_btn);
    lv_label_set_text(preset_center_label, "Center");
    lv_obj_set_style_text_font(preset_center_label, &lv_font_montserrat_14, 0);
    lv_obj_center(preset_center_label);

    lv_obj_t *preset180_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(preset180_btn, &servo_btn_style, 0);
    lv_obj_set_size(preset180_btn, 138, 64);
    lv_obj_set_pos(preset180_btn, 350, 332);
    lv_obj_add_event_cb(preset180_btn, servo_preset_180_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(preset180_btn);
    lv_obj_t *preset180_label = lv_label_create(preset180_btn);
    lv_label_set_text(preset180_label, "180 deg");
    lv_obj_set_style_text_font(preset180_label, &lv_font_montserrat_14, 0);
    lv_obj_center(preset180_label);

    lv_obj_t *servo_start = create_runtime_toggle(&servo_flag, 290, 252, 150, 64);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN);
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);
    lv_label_set_text(rounded_rect1_label1, "1");
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1);
    lv_label_set_text(rounded_rect1_label2, "GND");
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x8B0000), LV_PART_MAIN);
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);
    lv_label_set_text(rounded_rect2_label1, "6");
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2);
    lv_label_set_text(rounded_rect2_label2, "SER");
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *row_label = lv_label_create(lv_scr_act());
    lv_label_set_text(row_label, "#FFD700 ROW1#");
    lv_obj_set_style_text_font(row_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(row_label, 308, 420);
    lv_label_set_recolor(row_label, true);

    servo_keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(servo_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(servo_keyboard, servo_angle_text);
    lv_obj_add_flag(servo_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(servo_keyboard, clear_keyboard_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *servo_focus_chain[] = {
        servo_start,
        minus_btn,
        plus_btn,
        servo_slider,
        preset0_btn,
        preset_center_btn,
        preset180_btn,
    };
    rebuild_focus_group(servo_start, servo_focus_chain, sizeof(servo_focus_chain) / sizeof(servo_focus_chain[0]));

    {
        char angle_str[8];
        snprintf(angle_str, sizeof(angle_str), "%d", servo_angle);
        lv_textarea_set_text(servo_angle_text, angle_str);
    }
    lv_slider_set_value(servo_slider, servo_angle, LV_ANIM_OFF);
}

void uarthelper_init(void)
{

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0xFFA500), LV_PART_MAIN); // 背景色为橙色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "12");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "RX");                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect3 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect3, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect3, 75, 70);
    lv_obj_set_pos(rounded_rect3, 197, 410);
    lv_obj_set_style_radius(rounded_rect3, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect3, lv_color_hex(0xFFA500), LV_PART_MAIN); // 背景色为橙色
    lv_obj_t *rounded_rect3_label1 = lv_label_create(rounded_rect3);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label1, "13");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect3_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect3_label2 = lv_label_create(rounded_rect3); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label2, "TX");                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect3_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *volt_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(volt_label1, 308, 420);
    lv_label_set_recolor(volt_label1, true);

    lv_obj_t *volt_label2 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label2, "#00FF7F UART#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label2, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(volt_label2, 61, 64);
    lv_label_set_recolor(volt_label2, true);

    lv_obj_t *volt_label3 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label3, "#00FFFF baudRate#");   // 设置文本内容
    lv_obj_set_style_text_font(volt_label3, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(volt_label3, 293, 10);
    lv_label_set_text(volt_label3, "#00FFFF MODE#");
    lv_label_set_recolor(volt_label3, true);

    uart_extarea = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(uart_extarea, 404, 252);
    lv_obj_set_pos(uart_extarea, 47, 150);
    lv_textarea_set_placeholder_text(uart_extarea, "Use '\\n' as the end of the usart information");

    uart_list = lv_dropdown_create(lv_scr_act());
    lv_obj_set_size(uart_list, 158, 80);
    lv_obj_set_pos(uart_list, 293, 60);
    lv_dropdown_set_options(uart_list,
                            "1200\n"
                            "2400\n"
                            "4800\n"
                            "9600\n"
                            "19200\n"
                            "43000\n"
                            "76800\n"
                            "115200\n"
                            "128000\n"
                            "230400\n"
                            "256000\n"
                            "460800\n"
                            "921600");
    lv_dropdown_set_selected(uart_list, get_baud_dropdown_index(currentBaudRate));
    apply_ui_focus_emphasis(uart_list);

    static lv_style_t uart_style;
    static bool uart_style_ready = false;
    if (!uart_style_ready)
    {
        lv_style_init(&uart_style);
        lv_style_set_bg_color(&uart_style, lv_color_hex(0xFF0000));
        lv_style_set_border_color(&uart_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&uart_style, 2);
        uart_style_ready = true;
    }

    uart_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(uart_btn, &uart_style, 0); // 将样式应用到矩形上
    lv_obj_set_size(uart_btn, 98, 104);
    lv_obj_set_pos(uart_btn, 188, 40);
    lv_obj_add_event_cb(uart_btn, uart_btn_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(uart_btn);
    lv_obj_t *uart_btn_label = lv_label_create(uart_btn);
    lv_obj_set_style_text_color(uart_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(uart_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(uart_btn_label, "OPEN");
    lv_obj_set_style_text_font(uart_btn_label, &lv_font_montserrat_20, 0);

    lv_obj_t *uart_focus_chain[] = {uart_list, uart_btn};
    rebuild_focus_group(uart_list, uart_focus_chain, sizeof(uart_focus_chain) / sizeof(uart_focus_chain[0]));
}

void i2c_init(void)
{

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x1E90FF), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "7");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "SCL");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect3 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect3, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect3, 75, 70);
    lv_obj_set_pos(rounded_rect3, 197, 410);
    lv_obj_set_style_radius(rounded_rect3, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect3, lv_color_hex(0x1E90FF), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect3_label1 = lv_label_create(rounded_rect3);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label1, "8");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect3_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect3_label2 = lv_label_create(rounded_rect3); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label2, "SDA");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect3_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *volt_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(volt_label1, 308, 420);
    lv_label_set_recolor(volt_label1, true);

    lv_obj_t *volt_label2 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label2, "#00FFFF I2C#"); // 设置文本内容
    lv_obj_set_style_text_font(volt_label2, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(volt_label2, 61, 52);
    lv_label_set_recolor(volt_label2, true);

    i2c_extarea = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(i2c_extarea, 404, 240);
    lv_obj_set_pos(i2c_extarea, 47, 150);
    lv_textarea_set_placeholder_text(i2c_extarea, "A dev_addr / SCAN / W dev_addr data... / R reg [len] / T dev_addr read_len ... / RAW ...");

    i2c_mode_list = lv_dropdown_create(lv_scr_act());
    lv_obj_set_size(i2c_mode_list, 123, 80);
    lv_obj_set_pos(i2c_mode_list, 176, 48);
    lv_dropdown_set_options(i2c_mode_list, "Bridge\nListen");
    lv_dropdown_set_selected(i2c_mode_list, 0);
    apply_ui_focus_emphasis(i2c_mode_list);

    static lv_style_t i2con_style;
    static bool i2con_style_ready = false;
    if (!i2con_style_ready)
    {
        lv_style_init(&i2con_style);
        lv_style_set_bg_color(&i2con_style, lv_color_hex(0xFF0000));
        lv_style_set_border_color(&i2con_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&i2con_style, 2);
        i2con_style_ready = true;
    }

    i2con = lv_btn_create(lv_scr_act());
    lv_obj_add_style(i2con, &i2con_style, 0); // 将样式应用到矩形上
    lv_obj_set_size(i2con, 135, 90);
    lv_obj_set_pos(i2con, 308, 40);
    lv_obj_add_event_cb(i2con, i2conbtn_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(i2con);
    lv_obj_t *i2con_label = lv_label_create(i2con);
    lv_obj_set_style_text_color(i2con_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(i2con_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(i2con_label, "OPEN");
    lv_obj_set_style_text_font(i2con_label, &lv_font_montserrat_24, 0);

    lv_obj_t *i2c_focus_chain[] = {i2c_mode_list, i2con};
    rebuild_focus_group(i2c_mode_list, i2c_focus_chain, sizeof(i2c_focus_chain) / sizeof(i2c_focus_chain[0]));
}

void spi_init(void)
{
    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN);
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);
    lv_label_set_text(rounded_rect1_label1, "1");
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1);
    lv_label_set_text(rounded_rect1_label2, "GND");
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x0000FF), LV_PART_MAIN);
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);
    lv_label_set_text(rounded_rect2_label1, "7");
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2);
    lv_label_set_text(rounded_rect2_label2, "CLK");
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *rounded_rect3 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect3, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect3, 75, 70);
    lv_obj_set_pos(rounded_rect3, 197, 410);
    lv_obj_set_style_radius(rounded_rect3, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect3, lv_color_hex(0x1E90FF), LV_PART_MAIN);
    lv_obj_t *rounded_rect3_label1 = lv_label_create(rounded_rect3);
    lv_label_set_text(rounded_rect3_label1, "8");
    lv_obj_set_style_text_font(rounded_rect3_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect3_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect3_label2 = lv_label_create(rounded_rect3);
    lv_label_set_text(rounded_rect3_label2, "CS");
    lv_obj_set_style_text_font(rounded_rect3_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect3_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *rounded_rect4 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect4, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect4, 75, 70);
    lv_obj_set_pos(rounded_rect4, 272, 410);
    lv_obj_set_style_radius(rounded_rect4, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect4, lv_color_hex(0xFF8C00), LV_PART_MAIN);
    lv_obj_t *rounded_rect4_label1 = lv_label_create(rounded_rect4);
    lv_label_set_text(rounded_rect4_label1, "12");
    lv_obj_set_style_text_font(rounded_rect4_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect4_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect4_label2 = lv_label_create(rounded_rect4);
    lv_label_set_text(rounded_rect4_label2, "MISO");
    lv_obj_set_style_text_font(rounded_rect4_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect4_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *rounded_rect5 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect5, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect5, 75, 70);
    lv_obj_set_pos(rounded_rect5, 347, 410);
    lv_obj_set_style_radius(rounded_rect5, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rounded_rect5, lv_color_hex(0xFF8C00), LV_PART_MAIN);
    lv_obj_t *rounded_rect5_label1 = lv_label_create(rounded_rect5);
    lv_label_set_text(rounded_rect5_label1, "13");
    lv_obj_set_style_text_font(rounded_rect5_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect5_label1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *rounded_rect5_label2 = lv_label_create(rounded_rect5);
    lv_label_set_text(rounded_rect5_label2, "MOSI");
    lv_obj_set_style_text_font(rounded_rect5_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect5_label2, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "#00FFFF SPI#");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(title, 61, 52);
    lv_label_set_recolor(title, true);

    spi_extarea = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(spi_extarea, 404, 240);
    lv_obj_set_pos(spi_extarea, 47, 150);
    lv_textarea_set_placeholder_text(spi_extarea, "x 0x9f");

    static lv_style_t spiopen_style;
    static bool spiopen_style_ready = false;
    if (!spiopen_style_ready)
    {
        lv_style_init(&spiopen_style);
        lv_style_set_bg_color(&spiopen_style, lv_color_hex(0xFF0000));
        lv_style_set_border_color(&spiopen_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&spiopen_style, 2);
        spiopen_style_ready = true;
    }

    spicon = lv_btn_create(lv_scr_act());
    lv_obj_add_style(spicon, &spiopen_style, 0);
    lv_obj_set_size(spicon, 135, 90);
    lv_obj_set_pos(spicon, 308, 40);
    lv_obj_add_event_cb(spicon, spiconbtn_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(spicon);
    lv_obj_t *spicon_label = lv_label_create(spicon);
    lv_obj_set_style_text_color(spicon_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(spicon_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(spicon_label, "OPEN");
    lv_obj_set_style_text_font(spicon_label, &lv_font_montserrat_24, 0);

    lv_obj_t *spi_focus_chain[] = {spicon};
    rebuild_focus_group(spicon, spi_focus_chain, sizeof(spi_focus_chain) / sizeof(spi_focus_chain[0]));
}

void voltmeter_init(void)
{

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);
    ina266_flag = 0;
    digitalWrite(1, LOW);
    lv_obj_t *label1 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(label1, 181, 40);
    updatelabel_timer1 = lv_timer_create(update_label_timer1, 100, label1);
    lv_obj_t *label2 = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(label2, 338, 40);
    lv_label_set_text(label2, "#FF0000 V#"); // 设置文本内容
    lv_label_set_recolor(label2, true);

    volt_chart = lv_chart_create(lv_scr_act());
    lv_obj_set_style_bg_color(volt_chart, lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_line_color(volt_chart, lv_color_hex(0x696969), LV_PART_MAIN);
    lv_obj_set_size(volt_chart, 390, 280);
    lv_obj_set_pos(volt_chart, 91, 120);
    lv_chart_set_point_count(volt_chart, 15);
    lv_chart_set_axis_tick(volt_chart, LV_CHART_AXIS_PRIMARY_Y, 4, 2, 6, 2, true, 30);
    lv_chart_set_range(volt_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 20);
    lv_obj_set_style_width(volt_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(volt_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_update_mode(volt_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_series_t *ser1 = lv_chart_add_series(volt_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    adddata_timer = lv_timer_create(add_data, 100, ser1);
    lv_chart_refresh(volt_chart);
    lv_obj_add_event_cb(volt_chart, event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x8B0000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "2");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "VIN");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *volt_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(volt_label1, 308, 420);
    lv_label_set_recolor(volt_label1, true);
    lv_obj_t *start = create_runtime_toggle(&ina266_flag, 47, 30, 120, 52);
    lv_obj_t *focus_chain[] = {start};
    rebuild_focus_group(start, focus_chain, 1);
}

void DSO_init(void)
{
    scope_page_init();
}

void wirelessuart_init(void)
{

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0xFFA500), LV_PART_MAIN); // 背景色为橙色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "12");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "RX");                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect3 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect3, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect3, 75, 70);
    lv_obj_set_pos(rounded_rect3, 197, 410);
    lv_obj_set_style_radius(rounded_rect3, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect3, lv_color_hex(0xFFA500), LV_PART_MAIN); // 背景色为橙色
    lv_obj_t *rounded_rect3_label1 = lv_label_create(rounded_rect3);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label1, "13");                                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect3_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect3_label2 = lv_label_create(rounded_rect3); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect3_label2, "TX");                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect3_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect3_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *volt_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(volt_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(volt_label1, 308, 420);
    lv_label_set_recolor(volt_label1, true);

    lv_obj_t *volt_label2 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_obj_set_style_text_color(volt_label2, lv_color_hex(0x1E90FF), 0);
    lv_label_set_text(volt_label2, LV_SYMBOL_BLUETOOTH "BLE"); // 设置文本内容
    lv_obj_set_style_text_font(volt_label2, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(volt_label2, 47, 64);
    lv_label_set_recolor(volt_label2, true);

    lv_obj_t *volt_label3 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(volt_label3, "#00FFFF baudRate#");   // 设置文本内容
    lv_obj_set_style_text_font(volt_label3, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(volt_label3, 293, 10);
    lv_label_set_recolor(volt_label3, true);
    lv_label_set_text(volt_label3, "#00FFFF MODE#");

    wireless_uart_list = lv_dropdown_create(lv_scr_act());
    lv_obj_set_size(wireless_uart_list, 180, 80);
    lv_obj_set_pos(wireless_uart_list, 293, 60);
    lv_dropdown_set_options(wireless_uart_list,
                            "1200\n"
                            "2400\n"
                            "4800\n"
                            "9600\n"
                            "19200\n"
                            "43000\n"
                            "76800\n"
                            "115200\n"
                            "128000\n"
                            "230400\n"
                            "256000\n"
                            "460800\n"
                            "921600\n"
                            "I2C Bridge\n"
                            "I2C Listen\n"
                            "SPI");
    lv_dropdown_set_selected(wireless_uart_list, get_baud_dropdown_index(currentBaudRate));
    lv_obj_add_event_cb(wireless_uart_list, ble_mode_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(wireless_uart_list);

    wireless_uart_extarea = lv_textarea_create(lv_scr_act());
    lv_obj_set_size(wireless_uart_extarea, 404, 240);
    lv_obj_set_pos(wireless_uart_extarea, 47, 150);
    lv_textarea_set_placeholder_text(wireless_uart_extarea, "UART baud / I2C Bridge / I2C Listen / SPI");

    static lv_style_t wireless_uart_style;
    static bool wireless_uart_style_ready = false;
    if (!wireless_uart_style_ready)
    {
        lv_style_init(&wireless_uart_style);
        lv_style_set_bg_color(&wireless_uart_style, lv_color_hex(0xFF0000));
        lv_style_set_border_color(&wireless_uart_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&wireless_uart_style, 2);
        wireless_uart_style_ready = true;
    }

    wireless_uart_btn = lv_btn_create(lv_scr_act());
    lv_obj_add_style(wireless_uart_btn, &wireless_uart_style, 0); // 将样式应用到矩形上
    lv_obj_set_size(wireless_uart_btn, 98, 104);
    lv_obj_set_pos(wireless_uart_btn, 188, 40);
    lv_obj_add_event_cb(wireless_uart_btn, wireless_uart_btn_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(wireless_uart_btn);
    lv_obj_t *wireless_uart_btn_label = lv_label_create(wireless_uart_btn);
    lv_obj_set_style_text_color(wireless_uart_btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(wireless_uart_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(wireless_uart_btn_label, "OPEN");
    lv_obj_set_style_text_font(wireless_uart_btn_label, &lv_font_montserrat_20, 0);

    static const lv_coord_t pin_x_positions[5] = {47, 122, 197, 272, 347};
    for (uint8_t idx = 0; idx < 5; ++idx)
    {
        ble_pin_blocks[idx] = lv_obj_create(lv_scr_act());
        lv_obj_remove_style(ble_pin_blocks[idx], 0, LV_PART_SCROLLBAR);
        lv_obj_set_size(ble_pin_blocks[idx], 75, 70);
        lv_obj_set_pos(ble_pin_blocks[idx], pin_x_positions[idx], 410);
        lv_obj_set_style_radius(ble_pin_blocks[idx], 5, LV_PART_MAIN);

        ble_pin_number_labels[idx] = lv_label_create(ble_pin_blocks[idx]);
        lv_obj_set_style_text_font(ble_pin_number_labels[idx], &lv_font_montserrat_12, 0);
        lv_obj_align(ble_pin_number_labels[idx], LV_ALIGN_TOP_MID, 0, 4);

        ble_pin_name_labels[idx] = lv_label_create(ble_pin_blocks[idx]);
        lv_obj_set_style_text_font(ble_pin_name_labels[idx], &lv_font_montserrat_16, 0);
        lv_obj_align(ble_pin_name_labels[idx], LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    ble_mode_hint_label = lv_label_create(lv_scr_act());
    lv_label_set_text(ble_mode_hint_label, "#FFD700 ROW1#");
    lv_obj_set_style_text_font(ble_mode_hint_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(ble_mode_hint_label, 308, 420);
    lv_label_set_recolor(ble_mode_hint_label, true);

    ble_mode_title_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_color(ble_mode_title_label, lv_color_hex(0x1E90FF), 0);
    lv_label_set_text(ble_mode_title_label, LV_SYMBOL_BLUETOOTH "BLE");
    lv_obj_set_style_text_font(ble_mode_title_label, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(ble_mode_title_label, 47, 64);
    lv_label_set_recolor(ble_mode_title_label, true);

    update_ble_mode_ui_from_selection(wireless_uart_list);

    lv_obj_t *ble_focus_chain[] = {wireless_uart_list, wireless_uart_btn};
    rebuild_focus_group(wireless_uart_list, ble_focus_chain, sizeof(ble_focus_chain) / sizeof(ble_focus_chain[0]));
}

void FREcount_init(void)
{

    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    FREcount_flag = 0;

    lv_obj_t *rounded_rect1 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect1, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect1, 75, 70);
    lv_obj_set_pos(rounded_rect1, 47, 410);
    lv_obj_set_style_radius(rounded_rect1, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect1, lv_color_hex(0x696969), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect1_label1 = lv_label_create(rounded_rect1);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label1, "1");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect1_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect1_label2 = lv_label_create(rounded_rect1); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect1_label2, "GND");                  // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect1_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect1_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *rounded_rect2 = lv_obj_create(lv_scr_act());
    lv_obj_remove_style(rounded_rect2, 0, LV_PART_SCROLLBAR);
    lv_obj_set_size(rounded_rect2, 75, 70);
    lv_obj_set_pos(rounded_rect2, 122, 410);
    lv_obj_set_style_radius(rounded_rect2, 5, LV_PART_MAIN);                        // 圆角半径20
    lv_obj_set_style_bg_color(rounded_rect2, lv_color_hex(0x8B0000), LV_PART_MAIN); // 背景色为绿色
    lv_obj_t *rounded_rect2_label1 = lv_label_create(rounded_rect2);                // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label1, "6");                                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label1, &lv_font_montserrat_12, 0);
    lv_obj_align(rounded_rect2_label1, LV_ALIGN_TOP_MID, 0, 4);      // 将文本居中对齐到圆角矩形
    lv_obj_t *rounded_rect2_label2 = lv_label_create(rounded_rect2); // 将文本标签添加到圆角矩形上
    lv_label_set_text(rounded_rect2_label2, "IN");                   // 设置文本内容
    lv_obj_set_style_text_font(rounded_rect2_label2, &lv_font_montserrat_16, 0);
    lv_obj_align(rounded_rect2_label2, LV_ALIGN_BOTTOM_MID, 0, 0); // 将文本居中对齐到圆角矩形
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);

    lv_obj_t *FREcount_label1 = lv_label_create(lv_scr_act()); // 将文本标签添加到圆角矩形上
    lv_label_set_text(FREcount_label1, "#FFD700 ROW1#");       // 设置文本内容
    lv_obj_set_style_text_font(FREcount_label1, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(FREcount_label1, 308, 420);
    lv_label_set_recolor(FREcount_label1, true);

    lv_obj_t *FREcount_label2 = lv_label_create(lv_scr_act());        // 将文本标签添加到圆角矩形上
    lv_label_set_text(FREcount_label2, "#00FFFF Frequency Counter#"); // 设置文本内容
    lv_obj_set_style_text_font(FREcount_label2, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(FREcount_label2, 61, 52);
    lv_label_set_recolor(FREcount_label2, true);

    FRE_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(FRE_label, &lv_font_montserrat_42, 0);
    lv_obj_set_pos(FRE_label, 83, 200);
    lv_label_set_text(FRE_label, "0");
    FRE_label_update_timer = lv_timer_create(FRE_label_update, 1000, NULL);

    lv_obj_t *Hz_label = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(Hz_label, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(Hz_label, 392, 220);
    lv_label_set_text(Hz_label, "#FF0000 Hz#"); // 设置文本内容
    lv_label_set_recolor(Hz_label, true);
    lv_obj_t *start = create_runtime_toggle(&FREcount_flag, 188, 320, 135, 60);
    lv_obj_t *focus_chain[] = {start};
    rebuild_focus_group(start, focus_chain, 1);
}

void settings_init(void)
{
    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *settings_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_style_text_font(settings_panel, &lv_font_montserrat_30, LV_PART_MAIN);
    // Native viewport: center the 432px settings column.
    lv_obj_set_size(settings_panel, 432, 480);
    lv_obj_align(settings_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(settings_panel, 0, 0);
    lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_style_border_width(settings_panel, 0, 0);
    lv_obj_set_style_pad_all(settings_panel, 0, 0);
    lv_obj_set_style_pad_row(settings_panel, 0, 0);
    lv_obj_set_style_pad_column(settings_panel, 0, 0);
    lv_obj_set_scroll_dir(settings_panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_panel, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(0x444444), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_70, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(settings_panel, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(settings_panel, 2, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(settings_panel, event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(settings_panel, longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    char preset_11v_str[8];
    char preset_5v_str[8];
    char preset_3v3_str[8];
    char preset_default_str[8];
    char buzzer_str[8];
    uint16_t log_channel_index = 1;
    uint16_t log_level_index = 2;
    snprintf(preset_11v_str, sizeof(preset_11v_str), "%d", power_preset_11v);
    snprintf(preset_5v_str, sizeof(preset_5v_str), "%d", power_preset_5v);
    snprintf(preset_3v3_str, sizeof(preset_3v3_str), "%d", power_preset_3v3);
    snprintf(preset_default_str, sizeof(preset_default_str), "%d", power_boot_preset);
    snprintf(buzzer_str, sizeof(buzzer_str), "%.1f", buzzer_volume_percent);
    switch (get_app_log_channel())
    {
    case APP_LOG_CHANNEL_UART0:
        log_channel_index = 0;
        break;
    case APP_LOG_CHANNEL_USB:
        log_channel_index = 1;
        break;
    case APP_LOG_CHANNEL_NONE:
        log_channel_index = 2;
        break;
    default:
        log_channel_index = 1;
        break;
    }
    switch (get_app_log_level())
    {
    case APP_LOG_LEVEL_ERROR:
        log_level_index = 0;
        break;
    case APP_LOG_LEVEL_WARN:
        log_level_index = 1;
        break;
    case APP_LOG_LEVEL_INFO:
        log_level_index = 2;
        break;
    case APP_LOG_LEVEL_DEBUG:
        log_level_index = 3;
        break;
    default:
        log_level_index = 2;
        break;
    }

    static bool settings_styles_ready = false;
    static lv_style_t settings_card_style;
    static lv_style_t settings_btn_style;
    static lv_style_t settings_input_style;
    static lv_style_t settings_dropdown_style;
    if (!settings_styles_ready)
    {
        lv_style_init(&settings_card_style);
        lv_style_set_bg_color(&settings_card_style, lv_color_hex(0x101010));
        lv_style_set_border_color(&settings_card_style, lv_color_hex(0xFF0000));
        lv_style_set_border_width(&settings_card_style, 3);
        lv_style_set_radius(&settings_card_style, 20);
        lv_style_set_pad_all(&settings_card_style, 0);
        lv_style_set_shadow_width(&settings_card_style, 0);

        lv_style_init(&settings_btn_style);
        lv_style_set_bg_color(&settings_btn_style, lv_color_hex(0x000000));
        lv_style_set_border_color(&settings_btn_style, lv_color_hex(0xFF0000));
        lv_style_set_border_width(&settings_btn_style, 3);
        lv_style_set_radius(&settings_btn_style, 16);
        lv_style_set_pad_left(&settings_btn_style, 10);
        lv_style_set_pad_right(&settings_btn_style, 10);
        lv_style_set_pad_top(&settings_btn_style, 6);
        lv_style_set_pad_bottom(&settings_btn_style, 6);
        lv_style_set_text_color(&settings_btn_style, lv_color_hex(0xFFFFFF));

        lv_style_init(&settings_input_style);
        lv_style_set_bg_color(&settings_input_style, lv_color_hex(0x050505));
        lv_style_set_border_color(&settings_input_style, lv_color_hex(0xFF0000));
        lv_style_set_border_width(&settings_input_style, 2);
        lv_style_set_radius(&settings_input_style, 14);
        lv_style_set_pad_left(&settings_input_style, 8);
        lv_style_set_pad_right(&settings_input_style, 8);
        lv_style_set_pad_top(&settings_input_style, 7);
        lv_style_set_pad_bottom(&settings_input_style, 7);
        lv_style_set_text_color(&settings_input_style, lv_color_hex(0xFFFFFF));

        lv_style_init(&settings_dropdown_style);
        lv_style_set_bg_color(&settings_dropdown_style, lv_color_hex(0x050505));
        lv_style_set_border_color(&settings_dropdown_style, lv_color_hex(0xFF0000));
        lv_style_set_border_width(&settings_dropdown_style, 2);
        lv_style_set_radius(&settings_dropdown_style, 14);
        lv_style_set_pad_left(&settings_dropdown_style, 8);
        lv_style_set_pad_right(&settings_dropdown_style, 8);
        lv_style_set_pad_top(&settings_dropdown_style, 7);
        lv_style_set_pad_bottom(&settings_dropdown_style, 7);
        lv_style_set_text_color(&settings_dropdown_style, lv_color_hex(0xFFFFFF));

        settings_styles_ready = true;
    }

    lv_obj_t *title = lv_label_create(settings_panel);
    lv_label_set_text(title, "#00FFFF Settings#");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);
    lv_obj_set_pos(title, 18, 12);
    lv_label_set_recolor(title, true);

    lv_obj_t *title_note = lv_label_create(settings_panel);
    lv_label_set_text(title_note, "Scroll for more options.");
    lv_obj_set_width(title_note, 360);
    lv_label_set_long_mode(title_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(title_note, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_font(title_note, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(title_note, 18, 68);

    lv_obj_t *audio_card = lv_obj_create(settings_panel);
    lv_obj_add_style(audio_card, &settings_card_style, 0);
    lv_obj_remove_style(audio_card, NULL, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(audio_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(audio_card, 408, 144);
    lv_obj_set_pos(audio_card, 12, 108);

    lv_obj_t *audio_title = lv_label_create(audio_card);
    lv_label_set_text(audio_title, "#00FFFF Audio#");
    lv_obj_set_style_text_font(audio_title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(audio_title, 15, 16);
    lv_label_set_recolor(audio_title, true);

    lv_obj_t *audio_label = lv_label_create(audio_card);
    lv_label_set_text(audio_label, "Buzzer %");
    lv_obj_set_style_text_font(audio_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(audio_label, 15, 76);

    buzzer_volume = lv_textarea_create(audio_card);
    lv_obj_add_style(buzzer_volume, &settings_input_style, 0);
    lv_textarea_set_one_line(buzzer_volume, true);
    lv_textarea_set_text(buzzer_volume, buzzer_str);
    lv_textarea_set_placeholder_text(buzzer_volume, "0.0-100.0");
    lv_obj_set_size(buzzer_volume, 120, 60);
    lv_obj_set_pos(buzzer_volume, 135, 68);
    lv_obj_set_style_text_font(buzzer_volume, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(buzzer_volume, buzzertext_event_handler, LV_EVENT_CLICKED, NULL);
    suppress_ui_input_focus(buzzer_volume);

    lv_obj_t *buzzer_apply = lv_btn_create(audio_card);
    lv_obj_add_style(buzzer_apply, &settings_btn_style, 0);
    lv_obj_set_size(buzzer_apply, 108, 60);
    lv_obj_set_pos(buzzer_apply, 279, 68);
    lv_obj_add_event_cb(buzzer_apply, buzzer_apply_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(buzzer_apply);
    lv_obj_t *buzzer_apply_label = lv_label_create(buzzer_apply);
    lv_label_set_text(buzzer_apply_label, "APPLY");
    lv_obj_set_style_text_font(buzzer_apply_label, &lv_font_montserrat_20, 0);
    lv_obj_center(buzzer_apply_label);

    lv_obj_t *power_card = lv_obj_create(settings_panel);
    lv_obj_add_style(power_card, &settings_card_style, 0);
    lv_obj_remove_style(power_card, NULL, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(power_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(power_card, 408, 372);
    lv_obj_set_pos(power_card, 12, 268);

    lv_obj_t *power_title = lv_label_create(power_card);
    lv_label_set_text(power_title, "#00FFFF Power Presets#");
    lv_obj_set_style_text_font(power_title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(power_title, 15, 16);
    lv_label_set_recolor(power_title, true);

    lv_obj_t *power_note = lv_label_create(power_card);
    lv_label_set_text(power_note, "Raw value range: 1-126");
    lv_obj_set_style_text_color(power_note, lv_color_hex(0xC8C8C8), 0);
    lv_obj_set_style_text_font(power_note, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(power_note, 15, 56);

    lv_obj_t *preset3_label = lv_label_create(power_card);
    lv_label_set_text(preset3_label, "3V3");
    lv_obj_set_style_text_font(preset3_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(preset3_label, 15, 112);

    power_value_3v3 = lv_textarea_create(power_card);
    lv_obj_add_style(power_value_3v3, &settings_input_style, 0);
    lv_textarea_set_one_line(power_value_3v3, true);
    lv_textarea_set_text(power_value_3v3, preset_3v3_str);
    lv_textarea_set_placeholder_text(power_value_3v3, "1-126");
    lv_obj_set_size(power_value_3v3, 114, 60);
    lv_obj_set_pos(power_value_3v3, 117, 96);
    lv_obj_set_style_text_font(power_value_3v3, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(power_value_3v3, power_value_text_event_handler, LV_EVENT_CLICKED, NULL);
    suppress_ui_input_focus(power_value_3v3);

    lv_obj_t *preset3_btn = lv_btn_create(power_card);
    lv_obj_add_style(preset3_btn, &settings_btn_style, 0);
    lv_obj_set_size(preset3_btn, 123, 60);
    lv_obj_set_pos(preset3_btn, 264, 96);
    lv_obj_add_event_cb(preset3_btn, save_3v3_preset_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(preset3_btn);
    lv_obj_t *preset3_btn_label = lv_label_create(preset3_btn);
    lv_label_set_text(preset3_btn_label, "SAVE");
    lv_obj_set_style_text_font(preset3_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_center(preset3_btn_label);

    lv_obj_t *preset5_label = lv_label_create(power_card);
    lv_label_set_text(preset5_label, "5V");
    lv_obj_set_style_text_font(preset5_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(preset5_label, 15, 184);

    power_value_5v = lv_textarea_create(power_card);
    lv_obj_add_style(power_value_5v, &settings_input_style, 0);
    lv_textarea_set_one_line(power_value_5v, true);
    lv_textarea_set_text(power_value_5v, preset_5v_str);
    lv_textarea_set_placeholder_text(power_value_5v, "1-126");
    lv_obj_set_size(power_value_5v, 114, 60);
    lv_obj_set_pos(power_value_5v, 117, 168);
    lv_obj_set_style_text_font(power_value_5v, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(power_value_5v, power_value_text_event_handler, LV_EVENT_CLICKED, NULL);
    suppress_ui_input_focus(power_value_5v);

    lv_obj_t *preset5_btn = lv_btn_create(power_card);
    lv_obj_add_style(preset5_btn, &settings_btn_style, 0);
    lv_obj_set_size(preset5_btn, 123, 60);
    lv_obj_set_pos(preset5_btn, 264, 168);
    lv_obj_add_event_cb(preset5_btn, save_5v_preset_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(preset5_btn);
    lv_obj_t *preset5_btn_label = lv_label_create(preset5_btn);
    lv_label_set_text(preset5_btn_label, "SAVE");
    lv_obj_set_style_text_font(preset5_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_center(preset5_btn_label);

    lv_obj_t *preset11_label = lv_label_create(power_card);
    lv_label_set_text(preset11_label, "11V");
    lv_obj_set_style_text_font(preset11_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(preset11_label, 15, 256);

    power_value_11v = lv_textarea_create(power_card);
    lv_obj_add_style(power_value_11v, &settings_input_style, 0);
    lv_textarea_set_one_line(power_value_11v, true);
    lv_textarea_set_text(power_value_11v, preset_11v_str);
    lv_textarea_set_placeholder_text(power_value_11v, "1-126");
    lv_obj_set_size(power_value_11v, 114, 60);
    lv_obj_set_pos(power_value_11v, 117, 240);
    lv_obj_set_style_text_font(power_value_11v, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(power_value_11v, power_value_text_event_handler, LV_EVENT_CLICKED, NULL);
    suppress_ui_input_focus(power_value_11v);

    lv_obj_t *preset11_btn = lv_btn_create(power_card);
    lv_obj_add_style(preset11_btn, &settings_btn_style, 0);
    lv_obj_set_size(preset11_btn, 123, 60);
    lv_obj_set_pos(preset11_btn, 264, 240);
    lv_obj_add_event_cb(preset11_btn, save_11v_preset_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(preset11_btn);
    lv_obj_t *preset11_btn_label = lv_label_create(preset11_btn);
    lv_label_set_text(preset11_btn_label, "SAVE");
    lv_obj_set_style_text_font(preset11_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_center(preset11_btn_label);

    lv_obj_t *default_label = lv_label_create(power_card);
    lv_label_set_text(default_label, "Default");
    lv_obj_set_style_text_font(default_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(default_label, 15, 328);

    power_value_default = lv_textarea_create(power_card);
    lv_obj_add_style(power_value_default, &settings_input_style, 0);
    lv_textarea_set_one_line(power_value_default, true);
    lv_textarea_set_text(power_value_default, preset_default_str);
    lv_textarea_set_placeholder_text(power_value_default, "3V3 raw");
    lv_obj_set_size(power_value_default, 114, 60);
    lv_obj_set_pos(power_value_default, 117, 312);
    lv_obj_set_style_text_font(power_value_default, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(power_value_default, power_value_text_event_handler, LV_EVENT_CLICKED, NULL);
    suppress_ui_input_focus(power_value_default);

    lv_obj_t *default_btn = lv_btn_create(power_card);
    lv_obj_add_style(default_btn, &settings_btn_style, 0);
    lv_obj_set_size(default_btn, 123, 60);
    lv_obj_set_pos(default_btn, 264, 312);
    lv_obj_add_event_cb(default_btn, save_default_preset_event_cb, LV_EVENT_CLICKED, NULL);
    apply_ui_focus_emphasis(default_btn);
    lv_obj_t *default_btn_label = lv_label_create(default_btn);
    lv_label_set_text(default_btn_label, "SAVE");
    lv_obj_set_style_text_font(default_btn_label, &lv_font_montserrat_20, 0);
    lv_obj_center(default_btn_label);

    lv_obj_t *transport_card = lv_obj_create(settings_panel);
    lv_obj_add_style(transport_card, &settings_card_style, 0);
    lv_obj_remove_style(transport_card, NULL, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(transport_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(transport_card, 408, 192);
    lv_obj_set_pos(transport_card, 12, 656);

    lv_obj_t *transport_title = lv_label_create(transport_card);
    lv_label_set_text(transport_title, "#00FFFF Transport#");
    lv_obj_set_style_text_font(transport_title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(transport_title, 15, 16);
    lv_label_set_recolor(transport_title, true);

    lv_obj_t *ble_usb_label = lv_label_create(transport_card);
    lv_label_set_text(ble_usb_label, "BLE+USB link");
    lv_obj_set_style_text_font(ble_usb_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(ble_usb_label, 15, 72);

    lv_obj_t *ble_usb_switch = lv_switch_create(transport_card);
    lv_obj_set_size(ble_usb_switch, 81, 52);
    lv_obj_set_pos(ble_usb_switch, 279, 60);
    if (get_ble_usb_passthrough_enabled())
    {
        lv_obj_add_state(ble_usb_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(ble_usb_switch, ble_usb_passthrough_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(ble_usb_switch);

    lv_obj_t *ble_usb_note = lv_label_create(transport_card);
    lv_label_set_text(ble_usb_note, "When ON, BLE debug also claims USB for the same protocol path.");
    lv_obj_set_width(ble_usb_note, 372);
    lv_label_set_long_mode(ble_usb_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(ble_usb_note, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_font(ble_usb_note, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(ble_usb_note, 15, 116);

    lv_obj_t *log_card = lv_obj_create(settings_panel);
    lv_obj_add_style(log_card, &settings_card_style, 0);
    lv_obj_remove_style(log_card, NULL, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(log_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(log_card, 408, 320);
    lv_obj_set_pos(log_card, 12, 864);

    lv_obj_t *log_title = lv_label_create(log_card);
    lv_label_set_text(log_title, "#00FFFF Logging#");
    lv_obj_set_style_text_font(log_title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(log_title, 15, 16);
    lv_label_set_recolor(log_title, true);

    lv_obj_t *log_enable_label = lv_label_create(log_card);
    lv_label_set_text(log_enable_label, "Enable");
    lv_obj_set_style_text_font(log_enable_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(log_enable_label, 15, 68);

    lv_obj_t *log_enable_switch = lv_switch_create(log_card);
    lv_obj_set_size(log_enable_switch, 81, 52);
    lv_obj_set_pos(log_enable_switch, 279, 56);
    if (get_app_log_enabled())
    {
        lv_obj_add_state(log_enable_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(log_enable_switch, log_enabled_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(log_enable_switch);

    lv_obj_t *log_channel_label = lv_label_create(log_card);
    lv_label_set_text(log_channel_label, "Channel");
    lv_obj_set_style_text_font(log_channel_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(log_channel_label, 15, 136);

    lv_obj_t *log_channel_list = lv_dropdown_create(log_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(log_channel_list), &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_style(log_channel_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(log_channel_list, "UART0\nUSB\nNONE");
    lv_dropdown_set_selected(log_channel_list, log_channel_index);
    lv_obj_set_size(log_channel_list, 204, 60);
    lv_obj_set_pos(log_channel_list, 183, 120);
    lv_obj_set_style_text_font(log_channel_list, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_font(log_channel_list, &lv_font_montserrat_20, LV_PART_SELECTED);
    lv_obj_set_style_text_font(log_channel_list, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_add_event_cb(log_channel_list, log_channel_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(log_channel_list);

    lv_obj_t *log_level_label = lv_label_create(log_card);
    lv_label_set_text(log_level_label, "Level");
    lv_obj_set_style_text_font(log_level_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(log_level_label, 15, 204);

    lv_obj_t *log_level_list = lv_dropdown_create(log_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(log_level_list), &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_style(log_level_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(log_level_list, "ERROR\nWARN\nINFO\nDEBUG");
    lv_dropdown_set_selected(log_level_list, log_level_index);
    lv_obj_set_size(log_level_list, 204, 60);
    lv_obj_set_pos(log_level_list, 183, 188);
    lv_obj_set_style_text_font(log_level_list, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_font(log_level_list, &lv_font_montserrat_20, LV_PART_SELECTED);
    lv_obj_set_style_text_font(log_level_list, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_add_event_cb(log_level_list, log_level_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(log_level_list);

    lv_obj_t *log_note = lv_label_create(log_card);
    lv_label_set_text(log_note, "Bridge pages temporarily mute logs and restore this config when they exit.");
    lv_obj_set_width(log_note, 372);
    lv_label_set_long_mode(log_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(log_note, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(log_note, 15, 260);

    lv_obj_t *sleep_card = lv_obj_create(settings_panel);
    lv_obj_add_style(sleep_card, &settings_card_style, 0);
    lv_obj_clear_flag(sleep_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sleep_card, 408, 384);
    lv_obj_set_pos(sleep_card, 12, 1248);

    lv_obj_t *sleep_title = lv_label_create(sleep_card);
    lv_label_set_text(sleep_title, "Display & sleep");
    lv_obj_set_style_text_font(sleep_title, &lv_font_montserrat_30, 0);
    lv_obj_set_pos(sleep_title, 15, 16);

    lv_obj_t *sleep_label = lv_label_create(sleep_card);
    lv_label_set_text(sleep_label, "Auto sleep");
    lv_obj_set_style_text_font(sleep_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(sleep_label, 15, 76);
    lv_obj_t *sleep_switch = lv_switch_create(sleep_card);
    lv_obj_set_size(sleep_switch, 81, 52);
    lv_obj_set_pos(sleep_switch, 303, 64);
    if (screen_sleep_enabled) lv_obj_add_state(sleep_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sleep_switch, screen_sleep_enabled_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(sleep_switch);

    lv_obj_t *timeout_label = lv_label_create(sleep_card);
    lv_label_set_text(timeout_label, "Idle time");
    lv_obj_set_style_text_font(timeout_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(timeout_label, 15, 152);
    lv_obj_t *timeout_list = lv_dropdown_create(sleep_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(timeout_list), &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_add_style(timeout_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(timeout_list, "Never\n15 sec\n30 sec\n1 min\n2 min\n5 min");
    lv_obj_set_size(timeout_list, 204, 60);
    lv_obj_set_pos(timeout_list, 183, 136);
    lv_dropdown_set_selected(timeout_list,
        timeout_choice_index(screen_timeout_choices,
                             sizeof(screen_timeout_choices) / sizeof(screen_timeout_choices[0]),
                             screen_idle_timeout_s, 2));
    lv_obj_add_event_cb(timeout_list, screen_timeout_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(timeout_list);

    lv_obj_t *brightness_label = lv_label_create(sleep_card);
    lv_label_set_text(brightness_label, "Brightness");
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(brightness_label, 15, 228);
    lv_obj_t *brightness_list = lv_dropdown_create(sleep_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(brightness_list), &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_add_style(brightness_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(brightness_list, "5%\n10%\n20%\n30%\n40%\n50%\n60%\n70%\n80%\n90%\n100%");
    lv_obj_set_size(brightness_list, 204, 60);
    lv_obj_set_pos(brightness_list, 183, 212);
    const size_t brightness_choice_count = sizeof(display_brightness_choices) / sizeof(display_brightness_choices[0]);
    uint16_t brightness_index = brightness_choice_count - 1;
    const uint8_t brightness_percent = get_display_brightness_percent();
    for (uint16_t n = 0; n < brightness_choice_count; ++n)
        if (display_brightness_choices[n] == brightness_percent) brightness_index = n;
    lv_dropdown_set_selected(brightness_list, brightness_index);
    lv_obj_add_event_cb(brightness_list, display_brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(brightness_list);

    lv_obj_t *photo_duration_label = lv_label_create(sleep_card);
    lv_label_set_text(photo_duration_label, "Photo time");
    lv_obj_set_style_text_font(photo_duration_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(photo_duration_label, 15, 304);
    lv_obj_t *photo_duration_list = lv_dropdown_create(sleep_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(photo_duration_list), &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_add_style(photo_duration_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(photo_duration_list, "5 sec\n10 sec\n15 sec\n30 sec\n60 sec");
    lv_obj_set_size(photo_duration_list, 204, 60);
    lv_obj_set_pos(photo_duration_list, 183, 288);
    lv_dropdown_set_selected(photo_duration_list,
        timeout_choice_index(screen_photo_duration_choices,
                             sizeof(screen_photo_duration_choices) / sizeof(screen_photo_duration_choices[0]),
                             screen_photo_duration_s, 0));
    lv_obj_add_event_cb(photo_duration_list, screen_photo_duration_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(photo_duration_list);

    lv_obj_t *phone_card = lv_obj_create(settings_panel);
    lv_obj_add_style(phone_card, &settings_card_style, 0);
    lv_obj_remove_style(phone_card, NULL, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(phone_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(phone_card, 408, 380);
    lv_obj_set_pos(phone_card, 12, 1648);

    lv_obj_t *phone_title = lv_label_create(phone_card);
    lv_label_set_text(phone_title, "#00FFFF Phone connection#");
    lv_obj_set_style_text_font(phone_title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(phone_title, 15, 16);
    lv_label_set_recolor(phone_title, true);

    lv_obj_t *phone_enable_label = lv_label_create(phone_card);
    lv_label_set_text(phone_enable_label, "App link");
    lv_obj_set_style_text_font(phone_enable_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(phone_enable_label, 15, 76);

    phone_ble_switch = lv_switch_create(phone_card);
    lv_obj_set_size(phone_ble_switch, 81, 52);
    lv_obj_set_pos(phone_ble_switch, 303, 60);
    if (get_phone_ble_enabled()) lv_obj_add_state(phone_ble_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(phone_ble_switch, phone_ble_enabled_cb, LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(phone_ble_switch);

    lv_obj_t *advertising_timeout_label = lv_label_create(phone_card);
    lv_label_set_text(advertising_timeout_label, "Broadcast");
    lv_obj_set_style_text_font(advertising_timeout_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(advertising_timeout_label, 15, 148);

    lv_obj_t *advertising_timeout_list = lv_dropdown_create(phone_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(advertising_timeout_list), &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_add_style(advertising_timeout_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(advertising_timeout_list, "30 sec\n60 sec\n120 sec\n300 sec");
    lv_dropdown_set_selected(advertising_timeout_list,
        timeout_choice_index(ble_advertising_timeout_choices, 4,
                             get_ble_advertising_timeout_s(), 2));
    lv_obj_set_size(advertising_timeout_list, 189, 60);
    lv_obj_set_pos(advertising_timeout_list, 198, 132);
    lv_obj_add_event_cb(advertising_timeout_list, ble_advertising_timeout_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(advertising_timeout_list);

    lv_obj_t *connection_timeout_label = lv_label_create(phone_card);
    lv_label_set_text(connection_timeout_label, "Inactive");
    lv_obj_set_style_text_font(connection_timeout_label, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(connection_timeout_label, 15, 220);

    lv_obj_t *connection_timeout_list = lv_dropdown_create(phone_card);
    lv_obj_set_style_text_font(lv_dropdown_get_list(connection_timeout_list), &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_add_style(connection_timeout_list, &settings_dropdown_style, 0);
    lv_dropdown_set_options(connection_timeout_list, "60 sec\n120 sec\n300 sec\n600 sec");
    lv_dropdown_set_selected(connection_timeout_list,
        timeout_choice_index(ble_connection_timeout_choices, 4,
                             get_ble_connection_timeout_s(), 1));
    lv_obj_set_size(connection_timeout_list, 189, 60);
    lv_obj_set_pos(connection_timeout_list, 198, 204);
    lv_obj_add_event_cb(connection_timeout_list, ble_connection_timeout_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    apply_ui_focus_emphasis(connection_timeout_list);

    lv_obj_t *phone_note = lv_label_create(phone_card);
    lv_label_set_text(phone_note,
                      "App mode replaces BLE Serial debug.\nApp reads count as connection activity.");
    lv_obj_set_width(phone_note, 372);
    lv_label_set_long_mode(phone_note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(phone_note, lv_color_hex(0xB8B8B8), 0);
    lv_obj_set_style_text_font(phone_note, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(phone_note, 15, 280);

    buzzer_keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(buzzer_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(buzzer_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(buzzer_keyboard, clear_keyboard_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    power_keyboard = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(power_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(power_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(power_keyboard, clear_keyboard_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *settings_focus_chain[] = {
        buzzer_apply,
        preset3_btn,
        preset5_btn,
        preset11_btn,
        default_btn,
        ble_usb_switch,
        log_enable_switch,
        log_channel_list,
        log_level_list,
        sleep_switch,
        timeout_list,
        brightness_list,
        photo_duration_list,
        phone_ble_switch,
        advertising_timeout_list,
        connection_timeout_list,
    };
    rebuild_focus_group(buzzer_apply, settings_focus_chain, sizeof(settings_focus_chain) / sizeof(settings_focus_chain[0]));
}

void readme_init(void)
{
    ui_prepare_page();
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(lv_scr_act(), event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "#00FFFF Device INFO#");
    lv_label_set_recolor(title, true);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(title, 56, 12);

    lv_obj_t *subtitle = lv_label_create(lv_scr_act());
    lv_label_set_text(subtitle, "当前固件与关键运行参数快照");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xB0B0B0), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(subtitle, 56, 68);

    char info_text[640];
    build_device_info_snapshot(info_text, sizeof(info_text));

    static lv_style_t info_card_style;
    static lv_style_t info_box_style;
    static bool info_styles_ready = false;
    if (!info_styles_ready)
    {
        lv_style_init(&info_card_style);
        lv_style_set_bg_color(&info_card_style, lv_color_hex(0x000000));
        lv_style_set_border_color(&info_card_style, lv_color_hex(0xFF0000));
        lv_style_set_border_width(&info_card_style, 3);
        lv_style_set_radius(&info_card_style, 20);
        lv_style_set_pad_all(&info_card_style, 0);

        lv_style_init(&info_box_style);
        lv_style_set_bg_color(&info_box_style, lv_color_hex(0x000000));
        lv_style_set_border_width(&info_box_style, 0);
        lv_style_set_outline_width(&info_box_style, 0);
        lv_style_set_shadow_width(&info_box_style, 0);
        lv_style_set_radius(&info_box_style, 16);
        lv_style_set_pad_left(&info_box_style, 6);
        lv_style_set_pad_right(&info_box_style, 6);
        lv_style_set_pad_top(&info_box_style, 6);
        lv_style_set_pad_bottom(&info_box_style, 6);
        lv_style_set_text_color(&info_box_style, lv_color_hex(0xFFFFFF));

        info_styles_ready = true;
    }

    lv_obj_t *info_card = lv_obj_create(lv_scr_act());
    lv_obj_add_style(info_card, &info_card_style, 0);
    lv_obj_remove_style(info_card, NULL, LV_PART_SCROLLBAR);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(info_card, 408, 364);
    lv_obj_set_pos(info_card, 50, 108);
    lv_obj_add_event_cb(info_card, event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(info_card, longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    lv_obj_t *info_box = lv_textarea_create(info_card);
    lv_obj_add_style(info_box, &info_box_style, 0);
    lv_obj_set_size(info_box, 384, 332);
    lv_obj_set_pos(info_box, 12, 16);
    lv_textarea_set_cursor_click_pos(info_box, false);
    lv_obj_clear_flag(info_box, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_set_style_text_font(info_box, &lv_font_montserrat_14, 0);
    lv_textarea_set_text(info_box, info_text);
    lv_obj_add_event_cb(info_box, event_handler_back, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(info_box, longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);
}

void ui_init(void)
{
    // 设置显示主题
    lv_theme_default_init(NULL, lv_color_hex(0x000000), lv_color_hex(0xFF0000), LV_THEME_DEFAULT_DARK, NULL);
    create_boot_animation();

    // lv_obj_set_style_border_color(lv_scr_act(), lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_width(lv_scr_act(), 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_opa(lv_scr_act(), LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);

    // lv_obj_t* obj = lv_obj_create(lv_scr_act());
    // lv_obj_set_style_border_color(obj, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_opa(obj, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_size(obj, lv_obj_get_width(lv_scr_act()), lv_obj_get_height(lv_scr_act()));

    // lv_obj_t *p_label = lv_label_create(obj);
    // lv_obj_align(p_label, LV_ALIGN_CENTER, 0, 0);
    // lv_label_set_text(p_label, "Hello World");
}
