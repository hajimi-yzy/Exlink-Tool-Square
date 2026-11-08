/* Run complete, unchanged production tool constructors with real LVGL. Only
 * hardware, non-power event actions, acquisition and the decorative PWM image
 * are stubbed. Power reset/readout/chart/switch logic is extracted unchanged. */
static lv_obj_t *volt_chart, *cur_chart, *DSO_chart, *FRE_label, *pwm_btn;
static lv_chart_series_t *DSO_ser;
static lv_obj_t *ble_mode_title_label, *ble_mode_hint_label;
static lv_obj_t *ble_pin_blocks[5], *ble_pin_number_labels[5], *ble_pin_name_labels[5];
static bool ui_focus_styles_ready;
static lv_style_t ui_focus_emphasis_style, ui_input_focus_suppressed_style;
static const lv_coord_t UI_FOCUS_ROW_TOLERANCE = 8;
static int ina266_flag, pwm_flag, servo_flag, DSO_flag, FREcount_flag;
static bool ina266_sample_valid, ina_sample_ready;
static uint32_t ina_pending_id;
static char voltageStr[20], currentStr[20], powerStr[20];
static double v, a, w;
static unsigned gpio_writes, gpio_high_writes;
static int gpio_level;
static const uint8_t image_pixel[2] = {0, 0};
static const lv_img_dsc_t pwmint_png = {
    .header = {.cf = LV_IMG_CF_TRUE_COLOR, .w = 1, .h = 1},
    .data_size = sizeof(image_pixel), .data = image_pixel,
};
#define LOW 0
#define HIGH 1
#define APP_LOGI(...) ((void)0)
static void digitalWrite(int pin, int level)
{
    assert(pin == 1 && (level == LOW || level == HIGH));
    gpio_level = level;
    ++gpio_writes;
    if (level == HIGH) ++gpio_high_writes;
}
static int digitalRead(int pin) { assert(pin == 1); return gpio_level; }

/* Scope behavior is validated separately by tests/scope_page_host. This suite
 * only invokes its entry point while checking other tool-page lifetimes. */
static void scope_page_init(void) {}

#define STUB_EVENT(name) static void name(lv_event_t *event) { LV_UNUSED(event); }
STUB_EVENT(VUPbtn_event_cb)
STUB_EVENT(VDOWNbtn_event_cb)
STUB_EVENT(V11btn_event_cb)
STUB_EVENT(V5btn_event_cb)
STUB_EVENT(V3btn_event_cb)
STUB_EVENT(fretext_event_handler)
STUB_EVENT(dutytext_event_handler)
STUB_EVENT(clear_keyboard_event_handler)
STUB_EVENT(pwm_btn_event_cb)
STUB_EVENT(servo_angle_event_cb)
STUB_EVENT(servo_minus_event_cb)
STUB_EVENT(servo_plus_event_cb)
STUB_EVENT(servo_preset_0_event_cb)
STUB_EVENT(servo_preset_center_event_cb)
STUB_EVENT(servo_preset_180_event_cb)
STUB_EVENT(servo_slider_event_cb)
STUB_EVENT(uart_btn_event_cb)
STUB_EVENT(i2conbtn_event_cb)
STUB_EVENT(spiconbtn_event_cb)
STUB_EVENT(ble_mode_dropdown_event_cb)
STUB_EVENT(wireless_uart_btn_event_cb)
#undef STUB_EVENT

#define STUB_TIMER(name) static void name(lv_timer_t *timer) { LV_UNUSED(timer); }
STUB_TIMER(FRE_label_update)
#undef STUB_TIMER
static void update_ble_mode_ui_from_selection(lv_obj_t *dropdown) { LV_UNUSED(dropdown); }

#include "production_tool_pages.inc"

static void close_tool_page(unsigned baseline_timers)
{
    ui_prepare_page();
    if (group) lv_group_del(group);
    group = NULL;
    advance(60); /* Let LVGL reclaim animations and textarea cursor timers. */
    assert(!runtime_toggle_button && !runtime_toggle_active);
    assert(!indev_keypad->group);
    assert(!lv_scr_act()->spec_attr || lv_scr_act()->spec_attr->event_dsc_cnt == 0);
    assert(timer_count() == baseline_timers);
    assert(lv_mem_test() == LV_RES_OK);
}

static void expect_power_text(const char *volts, const char *amps, const char *watts)
{
    assert(!strcmp(lv_label_get_text(updatelabel_timer1->user_data), volts));
    assert(!strcmp(lv_label_get_text(updatelabel_timer2->user_data), amps));
    assert(!strcmp(lv_label_get_text(updatelabel_timer3->user_data), watts));
}

static void expect_zero_baselines(void)
{
    lv_coord_t *volts = lv_chart_get_y_array(volt_chart, adddata_timer->user_data);
    lv_coord_t *amps = lv_chart_get_y_array(cur_chart, adddata_timer2->user_data);
    for (unsigned i = 0; i < 15; ++i) assert(volts[i] == 0 && amps[i] == 0);
}

static void expect_visible_chart_line(lv_obj_t *chart, lv_color_t color)
{
    lv_area_t area;
    lv_obj_get_coords(chart, &area);
    unsigned matching = 0;
    for (int y = area.y1; y <= area.y2; ++y)
        for (int x = area.x1; x <= area.x2; ++x)
            if (framebuffer[y * 480 + x].full == color.full) ++matching;
    assert(matching > 10);
}

static void power_page_reading_tests(unsigned baseline_timers)
{
    const unsigned high_before = gpio_high_writes;
    power_init(); /* Cold-start buffers are empty, as in the actual firmware. */
    assert(gpio_level == LOW && gpio_high_writes == high_before && !ina266_flag);
    assert(!ina266_sample_valid);
    expect_power_text("OFF", "OFF", "OFF");
    expect_text("OFF - zero baseline");
    advance(250); /* Catch initial labels being overwritten by empty buffers. */
    expect_power_text("OFF", "OFF", "OFF");
    expect_zero_baselines();
    lv_refr_now(NULL);
    expect_visible_chart_line(volt_chart, lv_color_hex(0xFF0000));
    expect_visible_chart_line(cur_chart, lv_color_hex(0x00FF7F));

    lv_obj_t *button = lv_obj_get_parent(poweron_label);
    assert(lv_event_send(button, LV_EVENT_CLICKED, NULL) == LV_RES_OK);
    assert(gpio_level == HIGH && ina266_flag && !ina266_sample_valid);
    expect_power_text("---", "---", "---");
    expect_text("WAITING - zero baseline");
    v = 9; a = 0.9; /* An old numeric cache is not a valid new sample. */
    advance(220);
    expect_power_text("---", "---", "---");
    expect_zero_baselines();

    /* Only sensor input is simulated; real timers format the LVGL labels and
     * append chart points through the production callbacks. */
    v = 5; a = 0.25; w = 1.25;
    strcpy(voltageStr, "5.000"); strcpy(currentStr, "0.250"); strcpy(powerStr, "1.250");
    ina266_sample_valid = true;
    advance(120);
    expect_power_text("5.000", "0.250", "1.250");
    expect_text("LIVE - initial zero is reference");
    lv_coord_t *volts = lv_chart_get_y_array(volt_chart, adddata_timer->user_data);
    lv_coord_t *amps = lv_chart_get_y_array(cur_chart, adddata_timer2->user_data);
    unsigned paired_samples = 0;
    for (unsigned i = 0; i < 15; ++i)
        if (volts[i] == 5 && amps[i] == 25) ++paired_samples;
    assert(paired_samples > 0);

    ina_pending_id = 42; ina_sample_ready = true;
    assert(lv_event_send(button, LV_EVENT_CLICKED, NULL) == LV_RES_OK);
    assert(gpio_level == LOW && !ina266_flag && !ina266_sample_valid);
    assert(!ina_pending_id && !ina_sample_ready && !voltageStr[0] && !currentStr[0] && !powerStr[0]);
    expect_power_text("OFF", "OFF", "OFF");
    expect_text("OFF - zero baseline");
    advance(220);
    expect_zero_baselines();
    assert(lv_event_send(button, LV_EVENT_CLICKED, NULL) == LV_RES_OK);
    expect_power_text("---", "---", "---");
    advance(120);
    expect_zero_baselines();

    /* Re-enter directly while live: the old strings and chart history must go. */
    strcpy(voltageStr, "9.999"); ina266_sample_valid = true;
    ina_pending_id = 43; ina_sample_ready = true;
    power_init();
    assert(gpio_level == LOW && !ina266_flag && !ina266_sample_valid);
    assert(!ina_pending_id && !ina_sample_ready && !voltageStr[0]);
    expect_power_text("OFF", "OFF", "OFF");
    advance(220);
    expect_power_text("OFF", "OFF", "OFF");
    expect_zero_baselines();
    close_tool_page(baseline_timers);
    assert(!power_chart_note);
    puts("PASS: real Power callbacks/switch/reset; cold entry, waiting, live samples, stop/restart/reentry; visible zero baselines; timers cleaned.");
}

static void tool_page_lifetime_tests(unsigned baseline_timers)
{
    const unsigned writes_before = gpio_writes, high_before = gpio_high_writes;
    void (*const pages[])(void) = {
        power_init, pwm_init, servo_init, uarthelper_init, i2c_init,
        spi_init, voltmeter_init, DSO_init, wirelessuart_init, FREcount_init,
    };
    /* A first visit owns each persistent shared style for the process lifetime. */
    for (unsigned page = 0; page < sizeof(pages) / sizeof(pages[0]); ++page) {
        pages[page]();
        close_tool_page(baseline_timers);
    }
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        for (unsigned page = 0; page < sizeof(pages) / sizeof(pages[0]); ++page) {
            pages[page]();
            if (runtime_toggle_button) {
                assert(*runtime_toggle_active == 0);
                lv_obj_t *button = runtime_toggle_button;
                assert(strcmp(lv_label_get_text(lv_obj_get_child(button, 0)), "START") == 0);
                assert(lv_event_send(button, LV_EVENT_CLICKED, NULL) == LV_RES_OK);
                assert(*runtime_toggle_active == 1);
                assert(strcmp(lv_label_get_text(lv_obj_get_child(button, 0)), "STOP") == 0);
                /* Match a runtime task's failed start without invoking CLICKED. */
                *runtime_toggle_active = 0;
                ui_refresh_runtime_control();
                assert(*runtime_toggle_active == 0);
                assert(strcmp(lv_label_get_text(lv_obj_get_child(button, 0)), "START") == 0);
                assert(lv_obj_get_style_bg_color(button, 0).full == lv_color_hex(0xB02020).full);
            }
            close_tool_page(baseline_timers);
            /* Refresh after deletion must be harmless even if the address is reused. */
            lv_obj_t *replacement = lv_btn_create(lv_scr_act());
            lv_obj_t *label = lv_label_create(replacement);
            lv_label_set_text(label, "UNRELATED");
            ui_refresh_runtime_control();
            assert(strcmp(lv_label_get_text(label), "UNRELATED") == 0);
            lv_obj_del(replacement);
        }
    }
    lv_mem_monitor(&after);
    assert(after.free_size == before.free_size && after.used_cnt == before.used_cnt);
    assert(gpio_writes - writes_before == 202 && gpio_high_writes == high_before);
    printf("PASS: 1000 actual tool page roundtrips; heap free %u -> %u bytes; screen descriptors 0; runtime failure refresh safe.\n",
           (unsigned)before.free_size, (unsigned)after.free_size);
}
