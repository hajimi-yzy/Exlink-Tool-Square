/* Actual production scope UI, actual LVGL pointer input; ADC cache is mocked. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "scope_capture.h"
#include "scope_math.h"

lv_group_t *group;
lv_indev_t *indev_keypad;
int DSO_flag;
static lv_color_t framebuffer[480 * 480], draw_pixels[480 * 32];
static lv_indev_state_t pointer_state = LV_INDEV_STATE_REL;
static lv_point_t pointer = {400, 30};
static scope_state_t acq_state = SCOPE_STOPPED;
static scope_config_t acq_config;
static scope_frame_t latest;
static bool wanted, owned, reject_start;
static uint32_t next_frame, stop_ack;
static unsigned start_calls, stop_calls, read_calls, flushed, clicks, back_events;
void longpress_event_handler_back(lv_event_t *event);
int scope_test_route(bool pressed, int x, int y);

bool scope_acq_start(const scope_config_t *config)
{
    ++start_calls;
    if (reject_start) return false;
    acq_config = *config;
    wanted = owned = true;
    acq_state = SCOPE_STARTING;
    next_frame = lv_tick_get() + 10;
    latest.count = 0;
    return true;
}

void scope_acq_stop(void)
{
    ++stop_calls;
    wanted = false;
    stop_ack = lv_tick_get() + 15;
}

bool scope_adc_owned(void) { return owned || wanted; }
scope_state_t scope_acq_state(void) { return acq_state; }
bool scope_acq_quiesce(void) { scope_acq_stop(); owned = false; return true; }
bool scope_acq_resume(void) { return scope_acq_start(&acq_config); }
bool scope_acq_read(scope_frame_t *frame, uint32_t after_sequence)
{
    ++read_calls;
    if (!latest.count || latest.sequence == after_sequence) return false;
    *frame = latest;
    return true;
}

static void adc_tick(void)
{
    const uint32_t now = lv_tick_get();
    if (!wanted) {
        if (now >= stop_ack) {
            owned = false;
            if (acq_state != SCOPE_ERROR) acq_state = SCOPE_STOPPED;
        }
        return;
    }
    if (now < next_frame) return;
    next_frame = now + 20;
    acq_state = SCOPE_RUNNING;
    uint16_t input[2 * SCOPE_MAX_POINTS];
    const unsigned count = scope_point_count(acq_config.time_div_ms);
    const unsigned rate = scope_sample_hz(acq_config.time_div_ms);
    for (unsigned i = 0; i < 2 * count; ++i)
        input[i] = (uint16_t)lround(1500.0 + 800.0 * sin(2 * 3.141592653589793 * 250 * i / rate));
    scope_analyze(input, count, rate, &acq_config, &latest);
    ++latest.sequence;
    latest.calibrated = true;
}

static void advance(unsigned milliseconds)
{
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 5) {
        lv_tick_inc(5);
        adc_tick();
        lv_timer_handler();
    }
}

void ui_prepare_page(void)
{
    while (lv_obj_remove_event_cb(lv_scr_act(), longpress_event_handler_back)) {}
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_pad_all(lv_scr_act(), 0, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
}

static void back_async(void *unused) { LV_UNUSED(unused); ui_prepare_page(); }
void longpress_event_handler_back(lv_event_t *event)
{
    LV_UNUSED(event);
    ++back_events;
    assert(lv_async_call(back_async, NULL) == LV_RES_OK);
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < 480 && area->y2 < 480);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * 480 + x] = *pixels++;
    ++flushed;
    lv_disp_flush_ready(driver);
}

static void read_pointer(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    LV_UNUSED(driver);
    const int route = scope_test_route(pointer_state == LV_INDEV_STATE_PR, pointer.x, pointer.y);
    data->state = route == 0 ? pointer_state : LV_INDEV_STATE_REL;
    data->point = pointer;
    if (route == 2) longpress_event_handler_back(NULL);
}

static void read_key(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    LV_UNUSED(driver);
    data->state = LV_INDEV_STATE_REL;
    data->key = LV_KEY_ENTER;
}

static unsigned timer_count(void)
{
    unsigned count = 0;
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) ++count;
    return count;
}

static lv_obj_t *label_with_prefix(lv_obj_t *parent, const char *prefix)
{
    if (lv_obj_check_type(parent, &lv_label_class) &&
        strncmp(lv_label_get_text(parent), prefix, strlen(prefix)) == 0) return parent;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        lv_obj_t *found = label_with_prefix(lv_obj_get_child(parent, i), prefix);
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t *find_class(lv_obj_t *parent, const lv_obj_class_t *class_p)
{
    if (lv_obj_check_type(parent, class_p)) return parent;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        lv_obj_t *found = find_class(lv_obj_get_child(parent, i), class_p);
        if (found) return found;
    }
    return NULL;
}

static lv_obj_t *dropdown_with_options(const char *options)
{
    lv_obj_t *root = lv_obj_get_child(lv_scr_act(), 0);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        if (lv_obj_check_type(child, &lv_dropdown_class) &&
            strcmp(lv_dropdown_get_options(child), options) == 0) return child;
    }
    assert(!"dropdown not found");
    return NULL;
}

static void count_click(lv_event_t *event) { LV_UNUSED(event); ++clicks; }

static void tap(int x, int y)
{
    pointer.x = x; pointer.y = y;
    pointer_state = LV_INDEV_STATE_PR;
    advance(40);
    pointer_state = LV_INDEV_STATE_REL;
    advance(40);
}

static void assert_caption(const char *caption)
{
    lv_obj_t *label = label_with_prefix(lv_scr_act(), caption);
    assert(label && strcmp(lv_label_get_text(label), caption) == 0);
}

static void assert_no_points(void)
{
    lv_obj_t *chart = find_class(lv_scr_act(), &lv_chart_class);
    lv_chart_series_t *series = lv_chart_get_series_next(chart, NULL);
    lv_coord_t *points = lv_chart_get_y_array(chart, series);
    for (unsigned i = 0; i < lv_chart_get_point_count(chart); ++i)
        assert(points[i] == LV_CHART_POINT_NONE);
}

static void assert_interactive_bounds(lv_obj_t *obj)
{
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        assert(area.x1 >= 0 && area.x2 <= 447);
    }
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        assert_interactive_bounds(lv_obj_get_child(obj, i));
}

static void close_page(unsigned baseline)
{
    ui_prepare_page();
    advance(100);
    assert(DSO_flag == 0 && !scope_adc_owned());
    assert(group == NULL && indev_keypad->group == NULL);
    assert(timer_count() == baseline);
    assert(lv_obj_get_child_cnt(lv_scr_act()) == 0);
    assert(lv_mem_test() == LV_RES_OK);
}

static void pointer_toggle_tests(void)
{
    scope_page_init();
    advance(100);
    assert_caption("START");
    assert_no_points();
    assert_interactive_bounds(lv_obj_get_child(lv_scr_act(), 0));
    lv_obj_t *start = lv_obj_get_parent(label_with_prefix(lv_scr_act(), "START"));
    lv_obj_add_event_cb(start, count_click, LV_EVENT_CLICKED, NULL);
    unsigned original_start = start_calls, original_stop = stop_calls;
    static const int points[][2] = {{385, 30}, {337, 18}, {433, 18}, {337, 43}, {433, 43}};
    for (unsigned i = 0; i < 120; ++i) {
        const int *p = points[i % 5];
        tap(p[0], p[1]);
        if (DSO_flag != 1) {
            fprintf(stderr, "Start tap %u at (%d,%d): clicks=%u starts=%u stops=%u state=%d\n",
                    i, p[0], p[1], clicks, start_calls, stop_calls, (int)acq_state);
            fflush(stderr);
        }
        assert(DSO_flag == 1);
        assert(start_calls == original_start + i + 1);
        assert_caption("STOP");
        advance(120); /* Background frames remain active during click handling. */
        assert(label_with_prefix(lv_scr_act(), "Min 0.700 V"));
        tap(p[0], p[1]);
        assert(DSO_flag == 0);
        assert(stop_calls == original_stop + i + 1);
        assert_caption("START");
        lv_obj_t *chart = find_class(lv_scr_act(), &lv_chart_class);
        lv_chart_series_t *series = lv_chart_get_series_next(chart, NULL);
        const lv_coord_t held = lv_chart_get_y_array(chart, series)[0];
        advance(150);
        assert(lv_chart_get_y_array(chart, series)[0] == held);
    }
    assert(clicks == 240);
    printf("PASS: 240 actual pointer clicks, 120 RUN/STOP cycles, one transition/click while frames arrive.\n");
}

static void edge_route_tests(unsigned baseline)
{
    const unsigned starts = start_calls;
    tap(449, 30); /* The real firmware consumes the rightmost 32px. */
    assert(DSO_flag == 0 && start_calls == starts);
    const unsigned backs = back_events;
    pointer = (lv_point_t){470, 30};
    pointer_state = LV_INDEV_STATE_PR;
    advance(40);
    pointer.x = 400; /* Cross START while the edge owns this contact. */
    advance(40);
    pointer_state = LV_INDEV_STATE_REL;
    advance(100);
    assert(back_events == backs + 1 && start_calls == starts);
    assert(DSO_flag == 0 && timer_count() == baseline);
    assert(!group && !indev_keypad->group);
    assert(lv_obj_get_child_cnt(lv_scr_act()) == 0);
    scope_page_init();
    advance(100);
    assert_interactive_bounds(lv_obj_get_child(lv_scr_act(), 0));
    printf("PASS: real EdgeBackGesture routes pointer, reserves x>=448, back crossing START creates no ghost click.\n");
}

static void settings_and_failure_tests(void)
{
    tap(400, 30);
    advance(120);
    unsigned starts = start_calls;
    lv_obj_t *time = dropdown_with_options("1 ms\n2 ms\n5 ms\n10 ms\n20 ms\n50 ms");
    lv_dropdown_set_selected(time, 3);
    assert(lv_event_send(time, LV_EVENT_VALUE_CHANGED, NULL) == LV_RES_OK);
    assert(start_calls == starts + 1 && acq_config.time_div_ms == 10);
    assert_no_points();
    assert(label_with_prefix(lv_scr_act(), "Min -- V"));
    advance(150);
    assert(label_with_prefix(lv_scr_act(), "100 ms"));
    lv_obj_t *chart = find_class(lv_scr_act(), &lv_chart_class);
    assert(lv_chart_get_point_count(chart) == 501);
    lv_obj_t *vdiv = dropdown_with_options("0.1 V\n0.2 V\n0.5 V\n1.0 V");
    lv_dropdown_set_selected(vdiv, 1);
    assert(lv_event_send(vdiv, LV_EVENT_VALUE_CHANGED, NULL) == LV_RES_OK);
    assert(start_calls == starts + 1); /* View-only scale does not restart ADC. */
    assert(((lv_chart_t *)chart)->ymax[LV_CHART_AXIS_PRIMARY_Y] == 1200);
    tap(392, 436); /* AUTO FIT through real pointer */
    assert(((lv_chart_t *)chart)->ymax[LV_CHART_AXIS_PRIMARY_Y] >= 2300);

    wanted = owned = false;
    acq_state = SCOPE_ERROR;
    advance(120);
    assert(DSO_flag == 0);
    assert_caption("START");
    assert(label_with_prefix(lv_scr_act(), "ADC ERROR"));
    reject_start = true;
    tap(400, 30);
    assert(DSO_flag == 0);
    reject_start = false;
    tap(400, 30);
    assert(DSO_flag == 1);
    tap(400, 30);
    assert(DSO_flag == 0);
    starts = start_calls;
    lv_dropdown_set_selected(time, 1);
    assert(lv_event_send(time, LV_EVENT_VALUE_CHANGED, NULL) == LV_RES_OK);
    assert_no_points();
    assert(start_calls == starts);
    printf("PASS: timebase clears old frame; view range/auto fit; ADC failure/retry; stopped settings.\n");
}

static void lifetime_tests(unsigned baseline)
{
    close_page(baseline);
    /* Warm the persistent LVGL theme/style caches before comparing heap. */
    scope_page_init(); advance(100); close_page(baseline);
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (unsigned i = 0; i < 120; ++i) {
        scope_page_init();
        advance(40);
        tap(400, 30);
        assert(DSO_flag == 1);
        /* Delete a RUN page before asynchronous stop acknowledgment. */
        if (i & 1) {
            assert(lv_event_send(lv_scr_act(), LV_EVENT_LONG_PRESSED, NULL) == LV_RES_OK);
            advance(100);
        }
        close_page(baseline);
    }
    lv_mem_monitor(&after);
    assert(after.free_size == before.free_size && after.used_cnt == before.used_cnt);
    printf("PASS: 120 page reentries including active capture and async back; heap %u -> %u; timers/group clean.\n",
           (unsigned)before.free_size, (unsigned)after.free_size);
}

static void export_display(const char *path)
{
    scope_page_init();
    advance(100);
    tap(400, 30);
    advance(200);
    assert(flushed && read_calls);
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fprintf(file, "P6\n480 480\n255\n") > 0);
    for (unsigned i = 0; i < 480 * 480; ++i) {
        const uint32_t rgb = lv_color_to32(framebuffer[i]);
        const unsigned char pixel[] = {(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255};
        assert(fwrite(pixel, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, draw_pixels, NULL, 480 * 32);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = display.ver_res = 480;
    display.draw_buf = &buffer;
    display.flush_cb = flush;
    assert(lv_disp_drv_register(&display));
    static lv_indev_drv_t keypad, touch;
    lv_indev_drv_init(&keypad);
    keypad.type = LV_INDEV_TYPE_KEYPAD;
    keypad.read_cb = read_key;
    indev_keypad = lv_indev_drv_register(&keypad);
    lv_indev_drv_init(&touch);
    touch.type = LV_INDEV_TYPE_POINTER;
    touch.read_cb = read_pointer;
    assert(lv_indev_drv_register(&touch));
    advance(100);
    const unsigned baseline = timer_count();
    pointer_toggle_tests();
    edge_route_tests(baseline);
    settings_and_failure_tests();
    lifetime_tests(baseline);
    export_display(argv[1]);
    puts("PASS: production scope UI host checks; ADC mock only, no hardware validation.");
    return 0;
}
