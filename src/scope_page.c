#include "scope_capture.h"
#include "scope_math.h"
#include "display_geometry.h"
#include <lvgl.h>
#include <stdio.h>

/* Native 480px geometry. Only this LVGL timer touches widgets; the ADC worker
 * publishes complete, timed frames and never calls into LVGL or touch I2C. */
extern void ui_prepare_page(void);
extern void longpress_event_handler_back(lv_event_t *event);
extern lv_group_t *group;
extern lv_indev_t *indev_keypad;
extern int DSO_flag;

static lv_obj_t *root, *chart, *start_button, *status, *rate_label;
static lv_obj_t *readouts[6], *y_labels[3], *x_labels[3];
static lv_obj_t *v_select, *time_select, *base_select, *trigger_select, *level_select;
static lv_chart_series_t *trace;
static lv_timer_t *refresh_timer;
static lv_group_t *page_group;
static scope_frame_t shown;
static scope_config_t config = {2, 1500, SCOPE_RISING};
static const uint16_t time_choices[] = {1, 2, 5, 10, 20, 50};
static const uint16_t v_choices[] = {100, 200, 500, 1000};
static uint16_t v_div = 500, base_mv;

static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width,
                      const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_label_set_text(obj, value);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void axes(void)
{
    char value[32];
    for (unsigned i = 0; i < 3; ++i) {
        snprintf(value, sizeof(value), "%.1fV", (base_mv + (2 - i) * 3 * v_div) / 1000.0);
        lv_label_set_text(y_labels[i], value);
        snprintf(value, sizeof(value), "%u ms", i * 5 * config.time_div_ms);
        lv_label_set_text(x_labels[i], value);
    }
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, base_mv, base_mv + 6 * v_div);
}

static void button_state(void)
{
    lv_label_set_text(lv_obj_get_child(start_button, 0), DSO_flag ? "STOP" : "START");
    lv_obj_set_style_bg_color(start_button,
        lv_color_hex(DSO_flag ? 0x00A660 : 0xB02020), 0);
}

static void clear_frame(void)
{
    shown.count = 0;
    for (unsigned i = 0; i < 6; ++i) {
        static const char *const empty[] = {"Min -- V", "Max -- V", "Vpp -- V",
                                          "Mean -- V", "RMS -- V", "Freq -- Hz"};
        lv_label_set_text(readouts[i], empty[i]);
    }
    lv_chart_set_all_value(chart, trace, LV_CHART_POINT_NONE);
}

static void render_frame(void)
{
    if (!shown.count) return;
    lv_chart_set_point_count(chart, shown.count);
    lv_coord_t *points = lv_chart_get_y_array(chart, trace);
    for (unsigned i = 0; i < shown.count; ++i) points[i] = shown.mv[i];
    lv_chart_set_x_start_point(chart, trace, 0);
    lv_chart_refresh(chart); /* One invalidation per frame, not per ADC sample. */
    char value[40];
    const double readings[] = {shown.min_mv / 1000.0, shown.max_mv / 1000.0,
        (shown.max_mv - shown.min_mv) / 1000.0, shown.mean_mv / 1000.0, shown.rms_mv / 1000.0};
    const char *const names[] = {"Min", "Max", "Vpp", "Mean", "RMS"};
    for (unsigned i = 0; i < 5; ++i) {
        snprintf(value, sizeof(value), "%s %.3f V", names[i], readings[i]);
        lv_label_set_text(readouts[i], value);
    }
    if (shown.frequency_hz > 0) snprintf(value, sizeof(value), "Freq %.1f Hz", (double)shown.frequency_hz);
    else snprintf(value, sizeof(value), "Freq -- Hz");
    lv_label_set_text(readouts[5], value);
}

static void capture_status(void)
{
    const scope_state_t state = scope_acq_state();
    const char *message = !DSO_flag ? "STOP / HOLD" : state == SCOPE_STARTING ? "STARTING" :
        !shown.count ? "WAITING FOR SAMPLES" : config.trigger == SCOPE_FREE ? "RUN / FREE" :
        shown.triggered ? "RUN / TRIGGERED" : "RUN / AUTO (NO EDGE)";
    if (state == SCOPE_ERROR) message = scope_adc_owned() ?
        "ADC RELEASE ERROR - REBOOT" : "ADC ERROR - RETRY START";
    lv_label_set_text(status, message);
    char value[96];
    const unsigned long hz = shown.count ? shown.sample_hz : scope_sample_hz(config.time_div_ms);
    snprintf(value, sizeof(value), "%.1f kSa/s | %s%s | gaps %lu",
        hz / 1000.0, shown.count && shown.calibrated ? "eFuse cal" : "awaiting cal",
        shown.count && shown.clipped ? " | ADC range!" : "", (unsigned long)shown.dropped);
    lv_label_set_text(rate_label, value);
}

static void refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if (DSO_flag && scope_acq_state() == SCOPE_ERROR) {
        DSO_flag = 0;
        button_state();
    }
    if (DSO_flag && scope_acq_read(&shown, shown.sequence)) render_frame();
    capture_status();
}

static void start_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    /* Exactly one transition on a complete click. No delays or repeat events:
     * STOP freezes the trace and returns before DMA teardown finishes. */
    if (DSO_flag) {
        DSO_flag = 0;
        scope_acq_stop();
    } else if (scope_acq_start(&config)) {
        DSO_flag = 1;
        clear_frame();
    }
    button_state();
    capture_status();
}

static void settings_changed(lv_event_t *event)
{
    lv_obj_t *target = lv_event_get_target(event);
    v_div = v_choices[lv_dropdown_get_selected(v_select)];
    base_mv = 500 * lv_dropdown_get_selected(base_select);
    config.time_div_ms = time_choices[lv_dropdown_get_selected(time_select)];
    config.trigger = (scope_trigger_t)lv_dropdown_get_selected(trigger_select);
    config.trigger_mv = 500 * (1 + lv_dropdown_get_selected(level_select));
    axes();
    if (target == time_select || target == trigger_select || target == level_select) {
        /* A stopped frame retains its original time/trigger provenance. Clear
         * it when those settings change instead of relabelling old samples. */
        clear_frame();
        if (DSO_flag && !scope_acq_start(&config)) DSO_flag = 0;
    }
    button_state();
    capture_status();
}

static void auto_fit(lv_event_t *event)
{
    LV_UNUSED(event);
    if (!shown.count) return;
    base_mv = (shown.min_mv / 500) * 500;
    if (base_mv > 2000) base_mv = 2000;
    unsigned index = 0;
    while (index < 3 && base_mv + 6 * v_choices[index] < shown.max_mv + 50) ++index;
    v_div = v_choices[index];
    lv_dropdown_set_selected(v_select, index);
    lv_dropdown_set_selected(base_select, base_mv / 500);
    axes();
}

static lv_obj_t *button(const char *value, int x, int y, int width, lv_event_cb_t callback)
{
    lv_obj_t *obj = lv_btn_create(root);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 46);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFD700), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202020), 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = text(obj, value, 0, 0, width - 6, &lv_font_montserrat_18, 0xFFFFFF);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return obj;
}

static lv_obj_t *select_option(const char *caption, const char *options, int x, int y,
                               unsigned selected)
{
    text(root, caption, x, y, 140, &lv_font_montserrat_14, 0xAAAAAA);
    lv_obj_t *obj = lv_dropdown_create(root);
    lv_obj_set_pos(obj, x, y + 19);
    lv_obj_set_size(obj, 134, 40);
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(obj, 9, 0);
    lv_obj_set_style_pad_left(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0xFFD700), LV_STATE_FOCUSED);
    lv_dropdown_set_options(obj, options);
    lv_dropdown_set_selected(obj, selected);
    lv_obj_t *list = lv_dropdown_get_list(obj);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_16, 0);
    lv_dropdown_set_dir(obj, LV_DIR_TOP);
    lv_obj_add_event_cb(obj, settings_changed, LV_EVENT_VALUE_CHANGED, NULL);
    return obj;
}

static void deleted(lv_event_t *event)
{
    LV_UNUSED(event);
    DSO_flag = 0;
    scope_acq_stop();
    if (refresh_timer) { lv_timer_del(refresh_timer); refresh_timer = NULL; }
    if (page_group && group == page_group) { lv_group_del(page_group); group = NULL; }
    page_group = NULL;
    root = chart = start_button = status = rate_label = NULL;
    shown.count = 0;
}

void scope_page_init(void)
{
    ui_prepare_page();
    DSO_flag = 0;
    scope_acq_stop();
    shown = (scope_frame_t){0};
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);
    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, EXLINK_DISPLAY_WIDTH, EXLINK_DISPLAY_HEIGHT);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, NULL);
    text(root, "SCOPE", 20, 10, 210, &lv_font_montserrat_24, 0xFFFFFF);
    status = text(root, "STOP / HOLD", 20, 43, 310, &lv_font_montserrat_14, 0xFFD700);
    /* x>=448 belongs to the global edge-back gesture, so keep every control
     * to its left. Taps on STOP must never become navigation contacts. */
    start_button = button("START", 326, 8, 118, start_clicked);
    chart = lv_chart_create(root);
    lv_obj_set_pos(chart, 64, 76);
    lv_obj_set_size(chart, 390, 180);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x151515), 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x454545), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_chart_set_div_line_count(chart, 7, 11);
    lv_chart_set_point_count(chart, scope_point_count(config.time_div_ms));
    trace = lv_chart_add_series(chart, lv_color_hex(0xFFD700), LV_CHART_AXIS_PRIMARY_Y);
    for (unsigned i = 0; i < 3; ++i) {
        y_labels[i] = text(root, "", 8, 68 + i * 90, 52, &lv_font_montserrat_14, 0xAAAAAA);
        static const int tick_x[] = {64, 219, 374};
        x_labels[i] = text(root, "", tick_x[i], 260, 80, &lv_font_montserrat_14, 0xAAAAAA);
        lv_obj_set_style_text_align(x_labels[i], i == 0 ? LV_TEXT_ALIGN_LEFT :
            i == 1 ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_RIGHT, 0);
    }
    for (unsigned i = 0; i < 6; ++i)
        readouts[i] = text(root, "", 18 + (i % 3) * 152, 282 + (i / 3) * 23,
                          150, &lv_font_montserrat_16, i == 2 ? 0xFFD700 : 0xFFFFFF);
    v_select = select_option("V / div", "0.1 V\n0.2 V\n0.5 V\n1.0 V", 18, 334,
                             v_div == 100 ? 0 : v_div == 200 ? 1 : v_div == 500 ? 2 : 3);
    unsigned ti = 0;
    while (ti < 5 && time_choices[ti] != config.time_div_ms) ++ti;
    time_select = select_option("Time / div", "1 ms\n2 ms\n5 ms\n10 ms\n20 ms\n50 ms", 164, 334, ti);
    base_select = select_option("Y base", "0.0 V\n0.5 V\n1.0 V\n1.5 V\n2.0 V", 310, 334, base_mv / 500);
    trigger_select = select_option("Trigger", "Free\nRise (auto)\nFall (auto)", 18, 400, config.trigger);
    level_select = select_option("Trig. level", "0.5 V\n1.0 V\n1.5 V\n2.0 V\n2.5 V", 164, 400,
                                 config.trigger_mv / 500 - 1);
    lv_obj_t *fit = button("AUTO FIT", 310, 414, 134, auto_fit);
    rate_label = text(root, "", 18, 318, 445, &lv_font_montserrat_12, 0xAAAAAA);
    text(root, "ADC pin / DC   |   ROW1: 1 GND / 5 IN", 18, 465, 445,
         &lv_font_montserrat_12, 0xAAAAAA);
    if (group) { lv_group_del(group); group = NULL; }
    page_group = lv_group_create();
    group = page_group;
    lv_obj_t *controls[] = {start_button, v_select, time_select, base_select, trigger_select, level_select, fit};
    if (page_group) {
        for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
            lv_group_add_obj(page_group, controls[i]);
        lv_group_focus_obj(start_button);
        if (indev_keypad) lv_indev_set_group(indev_keypad, page_group);
    }
    axes();
    clear_frame();
    button_state();
    capture_status();
    refresh_timer = lv_timer_create(refresh, 100, NULL);
    if (!refresh_timer) {
        lv_obj_add_state(start_button, LV_STATE_DISABLED);
        lv_label_set_text(status, "UI TIMER ERROR");
    }
}
