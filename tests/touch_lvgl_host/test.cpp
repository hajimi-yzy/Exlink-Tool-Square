// Actual LVGL + TouchSample and touch callbacks extracted from production.
// Mock only the asynchronous sample queue, clock and application hooks.
#include <lvgl.h>
#include "interaction_policy.h"
#include "touch_lvgl_cancel.h"
#include "production_touch_sample.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool touch_input_enabled = true;
static bool touch_contact_down, touch_contact_cancelled;
static bool suppress_touch_until_release, display_idle_active;
static uint16_t touch_contact_x, touch_contact_y;
static uint32_t touch_contact_sample_ms, touch_consumed_sequence;
static EdgeBackGesture edge_back_gesture;
static unsigned activity, backs;
static uint32_t millis() { return lv_tick_get(); }
static void notify_user_activity() { ++activity; }
static bool ui_edge_back_allowed() { return false; }
static void ui_request_back() { ++backs; }
#define APP_LOGI(...) ((void)0)

static struct MockInternalIo {
    exlink::TouchSample queue[32];
    unsigned read = 0, write = 0;
    bool takeTouch(exlink::TouchSample &sample) {
        if (!touchPending()) return false;
        sample = queue[read++ % 32];
        return true;
    }
    bool touchPending() const { return read != write; }
    void push(const exlink::TouchSample &sample) {
        assert(write - read < 32);
        queue[write++ % 32] = sample;
    }
    void clear() { read = write = 0; }
} internal_io;

#include "production_touch_functions.inc"

static lv_color_t pixels[480 * 10];
static lv_indev_t *pointer;
static lv_obj_t *button, *scroller;
static unsigned clicks, releases, losses;
static bool delete_on_loss;
static uint32_t sequence;

static void event(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_CLICKED: ++clicks; break;
    case LV_EVENT_RELEASED: ++releases; break;
    case LV_EVENT_PRESS_LOST:
        ++losses;
        if (delete_on_loss) {
            delete_on_loss = false;
            lv_obj_del(lv_event_get_target(e));
            button = NULL;
        }
        break;
    default: break;
    }
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *)
{ lv_disp_flush_ready(driver); }

static void advance(unsigned milliseconds)
{
    for (unsigned i = 0; i < milliseconds; i += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

static void enqueue(bool down, bool released = false, bool cancelled = false,
                    uint16_t x = 100, uint16_t y = 100)
{
    exlink::TouchSample sample;
    sample.down = down;
    sample.released = released;
    sample.cancelled = cancelled;
    sample.x = x;
    sample.y = y;
    sample.sampled_ms = millis();
    sample.sequence = ++sequence;
    internal_io.push(sample);
}

static void make_button()
{
    button = lv_btn_create(lv_scr_act());
    lv_obj_set_pos(button, 70, 70);
    lv_obj_set_size(button, 70, 70);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(button, event, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(lv_scr_act());
}

static void reset()
{
    internal_io.clear();
    touch_input_enabled = true;
    display_idle_active = suppress_touch_until_release = false;
    touch_contact_down = touch_contact_cancelled = false;
    touch_contact_sample_ms = 0;
    edge_back_gesture = EdgeBackGesture();
    lv_obj_clean(lv_scr_act());
    button = scroller = NULL;
    lv_indev_reset(pointer, NULL);
    advance(35);
    clicks = releases = losses = activity = backs = 0;
    delete_on_loss = false;
    make_button();
}

static void start_press()
{
    enqueue(true);
    advance(30);
    assert(button && lv_obj_has_state(button, LV_STATE_PRESSED));
}

static void tap()
{
    // Down/up queued between two polls must survive continue_reading.
    enqueue(true);
    enqueue(false, true);
    advance(30);
}

static void make_scroller()
{
    lv_obj_clean(lv_scr_act());
    button = NULL;
    scroller = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(scroller, 40, 30);
    lv_obj_set_size(scroller, 320, 400);
    lv_obj_set_style_pad_all(scroller, 0, 0);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_clear_flag(scroller, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_t *content = lv_obj_create(scroller);
    lv_obj_set_size(content, 280, 1400);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scroller, event, LV_EVENT_ALL, NULL);
    lv_obj_update_layout(lv_scr_act());
}

static void drag()
{
    for (int y = 350; y >= 170; y -= 45) {
        enqueue(true, false, false, 150, (uint16_t)y);
        advance(30);
    }
    assert(lv_indev_get_scroll_obj(pointer) == scroller);
    assert(lv_obj_get_scroll_y(scroller) > 0);
}

int main(int argc, char **argv)
{
    lv_init();
    static lv_disp_draw_buf_t draw;
    lv_disp_draw_buf_init(&draw, pixels, NULL, 480 * 10);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = display.ver_res = 480;
    display.draw_buf = &draw;
    display.flush_cb = flush;
    assert(lv_disp_drv_register(&display));
    static lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = my_touchpad_read;
    pointer = lv_indev_drv_register(&input);
    assert(pointer);
    lv_obj_set_style_pad_all(lv_scr_act(), 0, 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    exlink::cancelLvglPointer(NULL);

    reset();
    start_press();
    advance(90);
    if (argc == 2 && strcmp(argv[1], "--legacy") == 0) {
        assert(clicks == 1 && releases == 1);
        assert(lv_obj_has_state(button, LV_STATE_CHECKED));
        puts("REPRODUCED: former timeout-as-release emits CLICKED/check toggle on silence.");
        return 0;
    }
    assert(clicks == 0 && releases == 0 && losses == 1);
    assert(!lv_obj_has_state(button, LV_STATE_PRESSED | LV_STATE_CHECKED));
    tap(); // No quarantine: a fresh touch immediately works.
    assert(clicks == 1 && releases == 1);
    assert(lv_obj_has_state(button, LV_STATE_CHECKED));

    reset();
    start_press();
    enqueue(false); // Empty mailbox is not a real release.
    advance(30);
    assert(lv_obj_has_state(button, LV_STATE_PRESSED) && !clicks);
    enqueue(false, false, true); // Worker missing-report timeout.
    advance(30);
    assert(clicks == 0 && releases == 0 && losses == 1);
    assert(!lv_obj_has_state(button, LV_STATE_PRESSED));
    start_press();
    enqueue(false, true);
    advance(30);
    assert(clicks == 1 && releases == 1);
    for (unsigned i = 0; i < 8; ++i) tap();
    assert(clicks == 9 && releases == 9);
    assert(!lv_obj_has_state(button, LV_STATE_PRESSED));

    reset();
    make_scroller();
    drag();
    enqueue(false, true);
    advance(30);
    assert(clicks == 0 && releases == 1);

    reset();
    start_press();
    advance(90);
    assert(clicks == 0 && losses == 1);
    make_scroller(); // Fresh swipe also recovers immediately after timeout.
    losses = releases = 0;
    drag();
    advance(90);
    assert(clicks == 0 && releases == 1 && losses == 0);
    const lv_coord_t at_release = lv_obj_get_scroll_y(scroller);
    advance(30);
    assert(lv_obj_get_scroll_y(scroller) > at_release);

    reset();
    clear_touch_contact();
    enqueue(true);
    advance(30);
    assert(!lv_obj_has_state(button, LV_STATE_PRESSED));
    enqueue(false, true);
    advance(30);
    assert(!suppress_touch_until_release && clicks == 0);
    tap();
    assert(clicks == 1);
    display_idle_active = true;
    tap();
    assert(clicks == 1);
    display_idle_active = false;
    tap();
    assert(clicks == 2);

    reset();
    touch_input_enabled = false;
    enqueue(true);
    advance(90);
    assert(clicks == 0 && internal_io.touchPending());
    assert(!lv_obj_has_state(button, LV_STATE_PRESSED));

    reset();
    start_press();
    delete_on_loss = true;
    enqueue(false, false, true);
    advance(30);
    assert(!button && clicks == 0 && losses == 1);
    make_button();
    tap();
    assert(clicks == 1);
    puts("PASS: actual LVGL + production callbacks: silence/worker cancellation, no false click/stuck press, quick taps, fresh-touch recovery, scrolling/inertia, wake suppression, disabled input, callback deletion.");
}
