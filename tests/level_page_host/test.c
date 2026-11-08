/* Real LVGL rendering of src/level_page.c with a simulated cache-only sensor.
 * This checks UI behavior/lifetime, not MPU6050 hardware or firmware timing. */
/* Zig may define NDEBUG with optimized builds. These are executable checks,
 * including event sends and file writes, so keep them active in every mode. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "level_page.h"

lv_group_t *group;
lv_indev_t *indev_keypad;
static lv_color_t framebuffer[480 * 480];
static lv_color_t draw_pixels[480 * 32];
static level_reading_t simulated = {true, true, 7.5f, -3.2f, 0x68, 0x68, true, true};
static bool simulated_fresh = true, simulated_active, zero_success = true;
static unsigned read_calls, zero_calls, deactivate_calls, flush_calls;
static lv_indev_state_t key_state = LV_INDEV_STATE_REL;
static uint32_t key_code = LV_KEY_ENTER;
static bool back_from_key_read, back_pending;
static unsigned menu_entries;

/* Shared production functions are extracted unchanged by run.py. */
static bool ui_home_page;
static lv_obj_t *phone_ble_switch, *battery_voltage_label;
static lv_obj_t *runtime_toggle_button;
static int *runtime_toggle_active;
static lv_obj_t *power_chart_note;
static lv_obj_t *fre, *duty, *servo_angle_text, *buzzer_volume;
static lv_obj_t *power_value_11v, *power_value_5v, *power_value_3v3, *power_value_default;
static lv_obj_t *fre_keyboard, *duty_keyboard, *servo_keyboard, *buzzer_keyboard, *power_keyboard;
static lv_obj_t *servo_slider, *poweron_label, *bat_label;
static lv_obj_t *uart_list, *uart_extarea, *uart_btn;
static lv_obj_t *wireless_uart_list, *wireless_uart_extarea, *wireless_uart_btn;
static lv_obj_t *i2c_mode_list, *i2c_extarea, *i2con, *spi_extarea, *spicon;
static lv_timer_t *updatelabel_timer1, *updatelabel_timer2, *updatelabel_timer3;
static lv_timer_t *adddata_timer, *adddata_timer2, *DSO_adddata_timer;
static lv_timer_t *DSO_update_timer1, *DSO_update_timer2, *DSO_update_timer3;
static lv_timer_t *slider_update_timer, *FRE_label_update_timer;
static lv_style_t style_rect, main_menu_focused_style;
static lv_obj_t *panel, *slider;
static lv_obj_t *btn1, *btn2, *btn3, *btn4, *btn5, *btn6, *btn7, *btn8;
static lv_obj_t *btn9, *btn10, *btn11, *btn12, *btn13, *btn14;
static lv_obj_t *btn_level, *btn_environment;
static bool menu_slider_syncing;
static int menu_selected_index = 1;
#define MENU_ITEM_COUNT 16
#define UI_MENU_ITEM_COUNT MENU_ITEM_COUNT
static int pwm_Freq = 1000, pwm_Duty = 50, servo_angle = 90;
static float buzzer_volume_percent = 50;
static long currentBaudRate = 115200;
#define constrain(value, minimum, maximum) ((value) < (minimum) ? (minimum) : (value) > (maximum) ? (maximum) : (value))
static void release_transport_page_handles(void) {}
static void event_handler_back(lv_event_t *event) { LV_UNUSED(event); }
static void main_menu_longpress_event_handler(lv_event_t *event) { LV_UNUSED(event); }
static void update_battery_indicator(void) {}
void longpress_event_handler_back(lv_event_t *event);
#include "production_lifecycle.inc"
static void menu_init(void);

bool level_sensor_read(level_reading_t *reading)
{
    ++read_calls;
    *reading = simulated;
    return simulated_active && simulated_fresh;
}

void level_sensor_set_active(bool active)
{
    simulated_active = active;
    if (!active) ++deactivate_calls;
}

bool level_sensor_zero(void)
{
    ++zero_calls;
    if (!zero_success) return false;
    simulated.roll_deg = simulated.pitch_deg = 0;
    return true;
}

static void back_async(void *unused)
{
    LV_UNUSED(unused);
    back_pending = false;
    menu_init();
}

void longpress_event_handler_back(lv_event_t *event)
{
    LV_UNUSED(event);
    sync_persistent_inputs_from_ui();
    if (!back_pending) {
        back_pending = true;
        assert(lv_async_call(back_async, NULL) == LV_RES_OK);
    }
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < 480 && area->y2 < 480);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * 480 + x] = *pixels++;
    ++flush_calls;
    lv_disp_flush_ready(driver);
}

static void read_key(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    LV_UNUSED(driver);
    if (back_from_key_read) {
        back_from_key_read = false;
        /* main.cpp's long push sends this while reading the keypad. */
        lv_event_send(lv_scr_act(), LV_EVENT_LONG_PRESSED, NULL);
    }
    data->state = key_state;
    data->key = key_code;
}

static void advance(unsigned milliseconds)
{
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

static unsigned timer_count(void)
{
    unsigned count = 0;
    for (lv_timer_t *timer = lv_timer_get_next(NULL); timer; timer = lv_timer_get_next(timer))
        ++count;
    return count;
}

static void menu_open_level(lv_event_t *event)
{
    LV_UNUSED(event);
    ++menu_entries;
    level_init();
}

static void menu_init(void)
{
    ui_prepare_page();
    if (group) lv_group_del(group);
    group = lv_group_create();
    assert(group);
    lv_indev_set_group(indev_keypad, group);
    ensure_main_menu_styles(&main_menu_focused_style);
    lv_obj_t *button = lv_btn_create(lv_scr_act());
    lv_obj_set_size(button, 160, 60);
    lv_obj_add_style(button, &style_rect, 0);
    lv_obj_add_style(button, &main_menu_focused_style, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(button, menu_open_level, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(button), "Open Level");
    lv_group_add_obj(group, button);
    ui_home_page = true;
}

static void key(uint32_t code)
{
    key_code = code;
    key_state = LV_INDEV_STATE_PR;
    advance(40);
    key_state = LV_INDEV_STATE_REL;
    advance(40);
}

static void wrong_widget_tests(void)
{
    /* A freed textarea's address may become a valid different widget. */
    fre = lv_textarea_create(lv_scr_act());
    lv_textarea_set_text(fre, "222");
    assert(read_textarea_int(fre, 7, 0, 1000) == 222);
    ui_prepare_page();
    assert(!fre);
    /* Deterministically model that allocator alias with a real label. */
    lv_obj_t *replacement = lv_label_create(lv_scr_act());
    lv_label_set_text(replacement, "333");
    fre = servo_angle_text = buzzer_volume = replacement;
    uart_list = replacement;
    assert(lv_obj_is_valid(replacement));
    assert(read_textarea_int(replacement, 7, 0, 1000) == 7);
    assert(read_textarea_float(replacement, 12.5f, 0, 100) == 12.5f);
    assert(read_dropdown_baud(replacement, 9600) == 9600);
    sync_persistent_inputs_from_ui();
    assert(pwm_Freq == 1000 && servo_angle == 90 && buzzer_volume_percent == 50 && currentBaudRate == 115200);
    ui_prepare_page();
    assert(!fre && !servo_angle_text && !buzzer_volume && !uart_list);
}

static void enter_level_from_page_timer(lv_timer_t *timer)
{
    assert(timer == updatelabel_timer1);
    level_init(); /* Production cleanup must safely delete this current timer. */
    assert(!updatelabel_timer1);
}

static void menu_focus_tests(unsigned baseline_timers)
{
    menu_init();
    ui_prepare_page();
    panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(panel, 345, 480);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_t **slots[] = {&btn1, &btn2, &btn3, &btn4, &btn5, &btn6, &btn7,
                         &btn8, &btn9, &btn10, &btn11, &btn_level, &btn_environment,
                         &btn12, &btn13, &btn14};
    assert(sizeof(slots) / sizeof(slots[0]) == MENU_ITEM_COUNT);
    for (unsigned n = 0; n < MENU_ITEM_COUNT; ++n) {
        *slots[n] = lv_btn_create(panel);
        lv_obj_set_size(*slots[n], 322, 160);
        lv_obj_add_style(*slots[n], &style_rect, 0);
        lv_obj_add_style(*slots[n], &main_menu_focused_style, LV_STATE_FOCUSED);
        lv_obj_add_flag(*slots[n], LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_group_add_obj(group, *slots[n]);
    }
    slider = lv_slider_create(lv_scr_act());
    lv_slider_set_range(slider, 0, MENU_ITEM_COUNT - 1);
    lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    slider_update_timer = lv_timer_create(update_slider_timer, 300, NULL);
    lv_obj_update_layout(panel);
    /* Exercise every production mapping and warm the renderer's shape cache. */
    for (int index = 1; index <= MENU_ITEM_COUNT; ++index) {
        assert(get_menu_button_by_index(index) == *slots[index - 1]);
        assert(get_menu_index_by_button(*slots[index - 1]) == index);
        lv_slider_set_value(slider, MENU_ITEM_COUNT - index, LV_ANIM_OFF);
        assert(lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL) == LV_RES_OK);
        assert(lv_group_get_focused(group) == get_menu_button_by_index(index));
        advance(600);
    }
    lv_slider_set_value(slider, MENU_ITEM_COUNT - 1, LV_ANIM_OFF);
    assert(lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL) == LV_RES_OK);
    advance(1000);
    assert(get_menu_index_by_button(lv_group_get_focused(group)) == 1);
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (unsigned n = 0; n < 100; ++n) {
        key(LV_KEY_NEXT);
        key(LV_KEY_PREV);
        key_code = n % 2 ? LV_KEY_NEXT : LV_KEY_PREV;
        key_state = LV_INDEV_STATE_PR;
        advance(700); /* Actual LVGL long-press repeat, overlapping scrolling. */
        key_state = LV_INDEV_STATE_REL;
        advance(40);
        assert(group && indev_keypad->group == group);
        assert(lv_group_get_obj_count(group) == MENU_ITEM_COUNT);
        assert(get_menu_index_by_button(lv_group_get_focused(group)) > 0);
        assert(lv_mem_test() == LV_RES_OK);
    }
    lv_slider_set_value(slider, MENU_ITEM_COUNT - 1, LV_ANIM_OFF);
    assert(lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL) == LV_RES_OK);
    advance(1000);
    const int selected = get_menu_index_by_button(lv_group_get_focused(group));
    assert(menu_selected_index == selected);
    assert(lv_slider_get_value(slider) == MENU_ITEM_COUNT - selected);
    lv_mem_monitor(&after);
    fprintf(stderr, "Menu heap: free %u -> %u, blocks %u -> %u; pending animations %u\n",
            (unsigned)before.free_size, (unsigned)after.free_size,
            (unsigned)before.used_cnt, (unsigned)after.used_cnt,
            (unsigned)lv_anim_count_running());
    assert(before.free_size == after.free_size && before.used_cnt == after.used_cnt);
    ui_prepare_page();
    lv_group_del(group);
    group = NULL;
    assert(!indev_keypad->group && timer_count() == baseline_timers);
    key(LV_KEY_NEXT);
    key(LV_KEY_PREV);
    puts("PASS: 100 menu NEXT/PREV taps plus held-key repeat bursts; exact production slider mapping/timer; stable heap.");
}

static lv_obj_t *find_label(lv_obj_t *object, const char *text)
{
    if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text))
        return object;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i) {
        lv_obj_t *found = find_label(lv_obj_get_child(object, i), text);
        if (found) return found;
    }
    return NULL;
}

static void expect_text(const char *text) { assert(find_label(lv_scr_act(), text)); }

static void click_zero(void)
{
    assert(group);
    lv_obj_t *button = lv_group_get_focused(group);
    assert(button && lv_obj_check_type(button, &lv_btn_class));
    assert(find_label(button, "ZERO"));
    assert(lv_event_send(button, LV_EVENT_CLICKED, NULL) == LV_RES_OK);
}

static void write_ppm(const char *path)
{
    lv_refr_now(NULL);
    assert(flush_calls);
    FILE *output = fopen(path, "wb");
    assert(output);
    fprintf(output, "P6\n# Actual LVGL; SIMULATED sensor: roll +7.5 deg, pitch -3.2 deg\n480 480\n255\n");
    unsigned nonblack = 0;
    for (unsigned y = 0; y < 480; ++y) {
        for (unsigned x = 0; x < 480; ++x) {
            uint32_t color = lv_color_to32(framebuffer[y * 480 + x]);
            unsigned char rgb[3] = {(color >> 16) & 255, (color >> 8) & 255, color & 255};
            if (rgb[0] || rgb[1] || rgb[2]) ++nonblack;
            /* The global edge-back input strip must remain unoccupied. */
            if (x >= 448) assert(!rgb[0] && !rgb[1] && !rgb[2]);
            assert(fwrite(rgb, 1, 3, output) == 3);
        }
    }
    assert(fclose(output) == 0);
    assert(nonblack > 1000);
}

#include "tool_pages.h"

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, draw_pixels, NULL, 480 * 32);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = driver.ver_res = 480;
    driver.draw_buf = &draw;
    driver.flush_cb = flush;
    lv_disp_t *display = lv_disp_drv_register(&driver);
    assert(display);
    /* Match ui_init's primary/secondary colors; page fonts are explicit. */
    lv_theme_t *theme = lv_theme_default_init(display, lv_color_hex(0),
        lv_color_hex(0xFF0000), true, &lv_font_montserrat_20);
    lv_disp_set_theme(display, theme);
    static lv_indev_drv_t keypad;
    lv_indev_drv_init(&keypad);
    keypad.type = LV_INDEV_TYPE_KEYPAD;
    keypad.read_cb = read_key;
    indev_keypad = lv_indev_drv_register(&keypad);
    assert(indev_keypad);
    const unsigned baseline_timers = timer_count();

    level_init();
    advance(100);
    assert(simulated_active && timer_count() == baseline_timers + 1);
    expect_text("LEVEL");
    expect_text("RELATIVE LEVEL | STABLE");
    expect_text("+7.5\xC2\xB0");
    expect_text("-3.2\xC2\xB0");
    write_ppm(argv[1]);

    simulated.stable = false;
    advance(60);
    click_zero();
    assert(zero_calls == 0);
    expect_text("HOLD STILL TO ZERO");
    simulated.stable = true;
    zero_success = false;
    click_zero();
    assert(zero_calls == 1);
    expect_text("ZERO FAILED - RETRY");
    zero_success = true;
    click_zero();
    assert(zero_calls == 2);
    expect_text("ZERO SET");
    expect_text("+0.0\xC2\xB0");

    simulated.available = false;
    simulated.who_am_i = 0;
    advance(60);
    expect_text("SENSOR NOT FOUND");
    expect_text("--.-\xC2\xB0");
    click_zero();
    assert(zero_calls == 2);
    simulated.who_am_i = 0x68;
    simulated.configured = false;
    simulated.has_sample = false;
    advance(60);
    expect_text("INITIALIZING SENSOR");
    simulated.configured = true;
    advance(60);
    expect_text("INITIALIZING SENSOR");
    simulated.has_sample = true;
    advance(60);
    expect_text("HOLD STILL - NO GRAVITY");
    simulated_fresh = false;
    advance(60);
    expect_text("DATA STALE - WAITING");
    click_zero();
    assert(zero_calls == 2);

    ui_prepare_page();
    assert(!simulated_active && deactivate_calls == 1 && group == NULL);
    assert(timer_count() == baseline_timers);
    const unsigned reads_after_close = read_calls;
    advance(250);
    assert(read_calls == reads_after_close);
    level_init();
    advance(60);
    ui_prepare_page();
    assert(!simulated_active && deactivate_calls == 2 && group == NULL);
    assert(timer_count() == baseline_timers);

    wrong_widget_tests();
    updatelabel_timer1 = lv_timer_create(enter_level_from_page_timer, 5, NULL);
    advance(60);
    assert(!updatelabel_timer1 && simulated_active);
    ui_prepare_page();
    assert(!simulated_active && timer_count() == baseline_timers);
    puts("PASS: production cleanup safely deletes the currently executing page timer.");
    menu_focus_tests(baseline_timers);
    simulated.available = simulated.stable = simulated_fresh = true;
    simulated.configured = simulated.has_sample = true;
    menu_init();
    advance(100);
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    const unsigned zero_before_cycles = zero_calls;
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        key(LV_KEY_ENTER); /* Real input timer dispatches menu CLICKED. */
        assert(menu_entries == cycle + 1 && simulated_active);
        assert(group && indev_keypad->group == group && lv_group_get_obj_count(group) == 1);
        expect_text("ZERO");
        key(LV_KEY_NEXT);
        key(LV_KEY_PREV);
        key(LV_KEY_ENTER);
        assert(zero_calls == zero_before_cycles + cycle + 1);
        /* Back is queued from keypad read just like the real long push. */
        key_code = 0;
        key_state = LV_INDEV_STATE_PR;
        back_from_key_read = true;
        advance(80);
        key_state = LV_INDEV_STATE_REL;
        advance(80);
        assert(ui_home_page && !simulated_active && !back_pending);
        assert(group && indev_keypad->group == group && lv_group_get_obj_count(group) == 1);
        expect_text("Open Level");
        assert(timer_count() == baseline_timers);
        assert(lv_mem_test() == LV_RES_OK);
    }
    lv_mem_monitor(&after);
    assert(after.free_size == before.free_size && after.used_cnt == before.used_cnt);
    ui_prepare_page();
    lv_group_del(group);
    group = NULL;
    assert(!indev_keypad->group && timer_count() == baseline_timers);
    printf("PASS: 100 real keypad menu/Level/async-back cycles; heap free %u -> %u bytes; no group/timer leak.\n",
           (unsigned)before.free_size, (unsigned)after.free_size);
    power_page_reading_tests(baseline_timers);
    tool_page_lifetime_tests(baseline_timers);
    puts("PASS: production cleanup/readers reject stale wrong-widget handles; actual LVGL render/Zero/status guards passed.");
    puts("Preview uses SIMULATED sensor readings: roll +7.5 deg, pitch -3.2 deg; not hardware validation.");
    return 0;
}
