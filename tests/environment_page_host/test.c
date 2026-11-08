/* Production environment_page.c on real LVGL with a simulated cache bridge.
 * This verifies page behavior and object lifetime, not physical sensor data. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "display_geometry.h"
#include "environment_page.h"

lv_group_t *group;
lv_indev_t *indev_keypad;
static lv_color_t framebuffer[EXLINK_DISPLAY_WIDTH * EXLINK_DISPLAY_HEIGHT];
static lv_color_t draw_pixels[EXLINK_DISPLAY_WIDTH * 32];
static environment_reading_t simulated = {
    true, true, 25.6f, 58.2f, ENVIRONMENT_ERROR_NONE
};
static bool simulated_fresh = true, simulated_active;
static unsigned read_calls, activate_calls, deactivate_calls, flush_calls;
static unsigned menu_entries, back_calls;
static bool back_pending, back_from_key_read;
static lv_indev_state_t key_state = LV_INDEV_STATE_REL;
static uint32_t key_code = LV_KEY_ENTER;

static void menu_init(void);

bool environment_sensor_read(environment_reading_t *reading)
{
    assert(simulated_active);
    ++read_calls;
    *reading = simulated;
    return simulated_fresh;
}

void environment_sensor_set_active(bool active)
{
    simulated_active = active;
    if (active) ++activate_calls;
    else ++deactivate_calls;
}

static void back_async(void *user_data)
{
    LV_UNUSED(user_data);
    back_pending = false;
    menu_init();
}

void longpress_event_handler_back(lv_event_t *event)
{
    LV_UNUSED(event);
    ++back_calls;
    if (!back_pending) {
        back_pending = true;
        assert(lv_async_call(back_async, NULL) == LV_RES_OK);
    }
}

/* Only shared navigation is a test stand-in. The production page and its
 * DELETE handler own every sensor activation, widget and refresh timer. The
 * exact firmware cleanup implementation is covered by level_page_host. */
void ui_prepare_page(void)
{
    while (lv_obj_remove_event_cb(lv_scr_act(), longpress_event_handler_back)) {}
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_pad_all(lv_scr_act(), 0, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    assert(area->x1 >= 0 && area->y1 >= 0);
    assert(area->x2 < EXLINK_DISPLAY_WIDTH && area->y2 < EXLINK_DISPLAY_HEIGHT);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * EXLINK_DISPLAY_WIDTH + x] = *pixels++;
    ++flush_calls;
    lv_disp_flush_ready(driver);
}

static void read_key(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    LV_UNUSED(driver);
    if (back_from_key_read) {
        back_from_key_read = false;
        assert(lv_event_send(lv_scr_act(), LV_EVENT_LONG_PRESSED, NULL) == LV_RES_OK);
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
    lv_obj_update_layout(lv_scr_act());
    lv_refr_now(NULL);
}

static void key(uint32_t code)
{
    key_code = code;
    key_state = LV_INDEV_STATE_PR;
    advance(40);
    key_state = LV_INDEV_STATE_REL;
    advance(40);
}

static unsigned timer_count(void)
{
    unsigned count = 0;
    for (lv_timer_t *timer = lv_timer_get_next(NULL); timer; timer = lv_timer_get_next(timer))
        ++count;
    return count;
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

static unsigned count_labels(lv_obj_t *object, const char *text)
{
    unsigned count = lv_obj_check_type(object, &lv_label_class) &&
                     !strcmp(lv_label_get_text(object), text);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
        count += count_labels(lv_obj_get_child(object, i), text);
    return count;
}

static void expect_text(const char *text)
{
    assert(find_label(lv_scr_act(), text));
}

static void expect_unavailable(const char *status)
{
    advance(260);
    expect_text(status);
    assert(count_labels(lv_scr_act(), "--.-") == 2);
    assert(!find_label(lv_scr_act(), "25.6") && !find_label(lv_scr_act(), "58.2"));
    assert(!find_label(lv_scr_act(), "LIVE"));
}

static void check_content_bounds(lv_obj_t *object)
{
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    /* These are panel/gesture contracts, not copies of page card positions. */
    assert(area.x1 >= 0 && area.x2 < EXLINK_DISPLAY_WIDTH - 32);
    assert(area.y1 >= 0 && area.y2 < EXLINK_DISPLAY_HEIGHT);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(object); ++i)
        check_content_bounds(lv_obj_get_child(object, i));
}

static void write_ppm(const char *path)
{
    lv_refr_now(NULL);
    assert(flush_calls);
    FILE *output = fopen(path, "wb");
    assert(output);
    fprintf(output, "P6\n# Actual LVGL; SIMULATED AHT20: 25.6 C, 58.2 percent RH\n%d %d\n255\n",
            EXLINK_DISPLAY_WIDTH, EXLINK_DISPLAY_HEIGHT);
    unsigned nonblack = 0;
    for (unsigned y = 0; y < EXLINK_DISPLAY_HEIGHT; ++y) {
        for (unsigned x = 0; x < EXLINK_DISPLAY_WIDTH; ++x) {
            const uint32_t color = lv_color_to32(framebuffer[y * EXLINK_DISPLAY_WIDTH + x]);
            const unsigned char rgb[] = {(color >> 16) & 255, (color >> 8) & 255, color & 255};
            if (rgb[0] || rgb[1] || rgb[2]) ++nonblack;
            if (x >= EXLINK_DISPLAY_WIDTH - 32) assert(!rgb[0] && !rgb[1] && !rgb[2]);
            assert(fwrite(rgb, 1, 3, output) == 3);
        }
    }
    assert(fclose(output) == 0);
    assert(nonblack > 1000);
}

static void menu_open_environment(lv_event_t *event)
{
    LV_UNUSED(event);
    ++menu_entries;
    environment_init();
}

static void menu_init(void)
{
    ui_prepare_page();
    if (group) lv_group_del(group);
    group = lv_group_create();
    assert(group);
    lv_indev_set_group(indev_keypad, group);
    lv_obj_t *button = lv_btn_create(lv_scr_act());
    lv_obj_add_event_cb(button, menu_open_environment, LV_EVENT_CLICKED, NULL);
    lv_label_set_text(lv_label_create(button), "Open Environment");
    lv_group_add_obj(group, button);
}

static void lifetime_tests(unsigned baseline_timers)
{
    /* Warm the menu and Environment drawing caches before comparing heap use. */
    menu_init();
    key(LV_KEY_ENTER);
    assert(simulated_active && group == NULL && !indev_keypad->group);
    back_from_key_read = true;
    advance(80);
    assert(!simulated_active && group && !back_pending);
    const unsigned initial_entries = menu_entries;
    const unsigned initial_back_calls = back_calls;
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);

    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        key(LV_KEY_ENTER); /* Actual keypad timer dispatches CLICKED. */
        assert(simulated_active && group == NULL && !indev_keypad->group);
        assert(menu_entries == initial_entries + cycle + 1);
        assert(timer_count() == baseline_timers + 1);
        if (cycle % 2 == 0) {
            const unsigned old_activations = activate_calls;
            const unsigned old_deactivations = deactivate_calls;
            environment_init(); /* Direct reentry must clean the old instance. */
            assert(activate_calls == old_activations + 1);
            assert(deactivate_calls == old_deactivations + 1);
            assert(timer_count() == baseline_timers + 1);
        }
        key(LV_KEY_NEXT);
        key(LV_KEY_PREV);
        back_from_key_read = true;
        advance(80); /* Back is queued from the real input read callback. */
        assert(!simulated_active && group && !back_pending);
        assert(indev_keypad->group == group);
        assert(lv_group_get_obj_count(group) == 1);
        expect_text("Open Environment");
        assert(timer_count() == baseline_timers);
        assert(back_calls == initial_back_calls + cycle + 1);
        assert(lv_mem_test() == LV_RES_OK);
    }
    lv_mem_monitor(&after);
    assert(before.free_size == after.free_size && before.used_cnt == after.used_cnt);
    ui_prepare_page();
    lv_group_del(group);
    group = NULL;
    assert(!indev_keypad->group);
    assert(timer_count() == baseline_timers && activate_calls == deactivate_calls);
    printf("PASS: 100 keypad menu/Environment/async-back cycles, 50 direct reentries; heap free %u -> %u; no group/timer leak.\n",
           (unsigned)before.free_size, (unsigned)after.free_size);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, draw_pixels, NULL, EXLINK_DISPLAY_WIDTH * 32);
    static lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = EXLINK_DISPLAY_WIDTH;
    driver.ver_res = EXLINK_DISPLAY_HEIGHT;
    driver.draw_buf = &draw;
    driver.flush_cb = flush;
    lv_disp_t *display = lv_disp_drv_register(&driver);
    assert(display);
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

    menu_init();
    const unsigned reads_before_open = read_calls;
    environment_init();
    assert(read_calls == reads_before_open + 1); /* Initial render is immediate. */
    advance(245);
    assert(read_calls == reads_before_open + 1);
    advance(5);
    assert(read_calls == reads_before_open + 2);
    assert(simulated_active && group == NULL && !indev_keypad->group);
    assert(timer_count() == baseline_timers + 1);
    expect_text("ENVIRONMENT");
    expect_text("LIVE");
    expect_text("25.6");
    expect_text("58.2");
    expect_text("\xC2\xB0" "C");
    expect_text("%RH");
    check_content_bounds(lv_obj_get_child(lv_scr_act(), 0));
    write_ppm(argv[1]);

    simulated.available = false;
    expect_unavailable("WAITING FOR FIRST SAMPLE");
    simulated.error = ENVIRONMENT_ERROR_MISSING;
    expect_unavailable("SENSOR NOT FOUND");
    simulated.error = ENVIRONMENT_ERROR_INITIALIZING;
    simulated.calibrated = false;
    expect_unavailable("INITIALIZING SENSOR");
    simulated.error = ENVIRONMENT_ERROR_READ_FAILED;
    expect_unavailable("SENSOR READ FAILED");
    simulated_fresh = false;
    expect_unavailable("DATA STALE - WAITING");
    simulated_fresh = true;
    simulated.available = simulated.calibrated = true;
    simulated.error = ENVIRONMENT_ERROR_NONE;
    simulated.temperature_c = NAN;
    expect_unavailable("SENSOR READ FAILED");
    simulated.temperature_c = 25.6f;
    simulated.humidity_rh = INFINITY;
    expect_unavailable("SENSOR READ FAILED");
    simulated.humidity_rh = 100.1f;
    expect_unavailable("SENSOR READ FAILED");
    simulated.temperature_c = -50.1f;
    simulated.humidity_rh = 58.2f;
    expect_unavailable("SENSOR READ FAILED");
    simulated.temperature_c = 25.6f;
    simulated.calibrated = false;
    expect_unavailable("INITIALIZING SENSOR");
    simulated.calibrated = true;
    simulated.error = ENVIRONMENT_ERROR_READ_FAILED;
    expect_unavailable("SENSOR READ FAILED");

    simulated = (environment_reading_t){true, true, -50.0f, 100.0f, ENVIRONMENT_ERROR_NONE};
    advance(260);
    expect_text("-50.0");
    expect_text("100.0");
    expect_text("LIVE");
    assert(count_labels(lv_scr_act(), "--.-") == 0);
    check_content_bounds(lv_obj_get_child(lv_scr_act(), 0));
    simulated.temperature_c = simulated.humidity_rh = 0.0f;
    advance(260);
    assert(count_labels(lv_scr_act(), "0.0") == 2); /* A valid zero stays valid. */
    expect_text("LIVE");
    simulated = (environment_reading_t){true, true, 25.6f, 58.2f, ENVIRONMENT_ERROR_NONE};
    advance(260);
    expect_text("25.6");
    expect_text("58.2");

    ui_prepare_page();
    assert(!simulated_active && timer_count() == baseline_timers);
    const unsigned reads_after_close = read_calls;
    advance(1000);
    assert(read_calls == reads_after_close);
    puts("PASS: production page render, units, edge strip, 250ms cache refresh, both invalid placeholders and recovery.");
    lifetime_tests(baseline_timers);
    puts("Preview contains SIMULATED AHT20 readings; physical sensor/display output is not validated here.");
    return 0;
}
