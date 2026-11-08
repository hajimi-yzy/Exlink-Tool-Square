#include "level_page.h"
#include "display_geometry.h"

#include <math.h>
#include <stdio.h>
#include <lvgl.h>

/* All pages use native panel pixels; the level dial stays circular. */
extern void ui_prepare_page(void);
extern void longpress_event_handler_back(lv_event_t *event);
extern lv_group_t *group;
extern lv_indev_t *indev_keypad;

enum {
    LEVEL_CENTER_X = 240,
    LEVEL_CENTER_Y = 222,
    LEVEL_DIAL_SIZE = 240,
    LEVEL_BUBBLE_SIZE = 30,
    LEVEL_BUBBLE_TRAVEL = 98,
    LEVEL_REFRESH_MS = 50
};

static lv_obj_t *level_root;
static lv_obj_t *level_bubble;
static lv_obj_t *level_roll_value;
static lv_obj_t *level_pitch_value;
static lv_obj_t *level_status;
static lv_obj_t *level_zero_button;
static lv_timer_t *level_timer;
static lv_group_t *level_group;
static const char *level_feedback;
static uint32_t level_feedback_until;
static uint32_t level_feedback_color;

static void level_status_set(const char *text, uint32_t color)
{
    lv_label_set_text(level_status, text);
    lv_obj_set_style_text_color(level_status, lv_color_hex(color), 0);
}

static void level_show_feedback(const char *text, uint32_t color)
{
    level_feedback = text;
    level_feedback_color = color;
    level_feedback_until = lv_tick_get() + 2000U;
    level_status_set(text, color);
}

static lv_obj_t *level_label(lv_obj_t *parent, const char *text,
                             lv_coord_t x, lv_coord_t y, lv_coord_t width,
                             const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *level_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                             lv_coord_t width, lv_coord_t height,
                             uint32_t color)
{
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_remove_style_all(shape);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return shape;
}

static bool level_get_reading(level_reading_t *reading)
{
    *reading = (level_reading_t){0};
    if (!level_sensor_read(reading)) return false;
    return !reading->available ||
           (isfinite(reading->roll_deg) && isfinite(reading->pitch_deg));
}

static void level_render(const level_reading_t *reading, bool fresh)
{
    if (!fresh || !reading->available) {
        /* Never present a disconnected sensor as a level, zero-degree device. */
        lv_label_set_text(level_roll_value, "--.-\xC2\xB0");
        lv_label_set_text(level_pitch_value, "--.-\xC2\xB0");
        lv_obj_add_flag(level_bubble, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(level_roll_value, lv_color_hex(0x777777), 0);
        lv_obj_set_style_text_color(level_pitch_value, lv_color_hex(0x777777), 0);
        const char *status = !fresh ? "DATA STALE - WAITING" :
            reading->who_am_i != 0x68 ? "SENSOR NOT FOUND" :
            !reading->configured || !reading->has_sample ? "INITIALIZING SENSOR" :
            "HOLD STILL - NO GRAVITY";
        level_status_set(status,
                         fresh ? 0xFF5555 : 0xFFD700);
        return;
    }

    char value[24];
    const float roll = fabsf(reading->roll_deg) < 0.05f ? 0.0f : reading->roll_deg;
    const float pitch = fabsf(reading->pitch_deg) < 0.05f ? 0.0f : reading->pitch_deg;
    snprintf(value, sizeof(value), "%+.1f\xC2\xB0", (double)roll);
    lv_label_set_text(level_roll_value, value);
    snprintf(value, sizeof(value), "%+.1f\xC2\xB0", (double)pitch);
    lv_label_set_text(level_pitch_value, value);
    lv_obj_set_style_text_color(level_roll_value, lv_color_hex(0xFFD700), 0);
    lv_obj_set_style_text_color(level_pitch_value, lv_color_hex(0x87CEFA), 0);

    /* Display range is +/-45 degrees, with a radial limit that keeps the
     * complete bubble inside the dial even when both axes exceed that range. */
    float x = fmaxf(-45.0f, fminf(45.0f, roll)) / 45.0f;
    float y = fmaxf(-45.0f, fminf(45.0f, pitch)) / 45.0f;
    const float radius = sqrtf(x * x + y * y);
    if (radius > 1.0f) { x /= radius; y /= radius; }
    const lv_coord_t bubble_x = (lv_coord_t)lroundf(x * LEVEL_BUBBLE_TRAVEL);
    const lv_coord_t bubble_y = (lv_coord_t)lroundf(y * LEVEL_BUBBLE_TRAVEL);
    lv_obj_set_pos(level_bubble,
                   LEVEL_CENTER_X - LEVEL_BUBBLE_SIZE / 2 + bubble_x,
                   LEVEL_CENTER_Y - LEVEL_BUBBLE_SIZE / 2 + bubble_y);
    lv_obj_set_style_bg_color(level_bubble,
                             lv_color_hex(reading->stable ? 0x00FF7F : 0xFFD700), 0);
    lv_obj_clear_flag(level_bubble, LV_OBJ_FLAG_HIDDEN);

    if (level_feedback && (int32_t)(lv_tick_get() - level_feedback_until) < 0) {
        level_status_set(level_feedback, level_feedback_color);
    } else {
        level_feedback = NULL;
        level_status_set(reading->stable ? "RELATIVE LEVEL | STABLE" : "HOLD STILL",
                         reading->stable ? 0x00FF7F : 0xFFD700);
    }
}

static void level_refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    level_reading_t reading;
    const bool fresh = level_get_reading(&reading);
    level_render(&reading, fresh);
}

static void level_zero_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    level_reading_t reading;
    const bool fresh = level_get_reading(&reading);
    level_render(&reading, fresh);
    if (!fresh || !reading.available) return;
    if (!reading.stable) {
        level_show_feedback("HOLD STILL TO ZERO", 0xFFD700);
        return;
    }
    if (!level_sensor_zero()) {
        level_show_feedback("ZERO FAILED - RETRY", 0xFF5555);
        return;
    }
    level_show_feedback("ZERO SET", 0x00FF7F);
    level_refresh(NULL);
}

static void level_page_deleted(lv_event_t *event)
{
    LV_UNUSED(event);
    if (level_timer) {
        lv_timer_del(level_timer);
        level_timer = NULL;
    }
    level_sensor_set_active(false);
    if (level_group && group == level_group) {
        lv_group_del(level_group);
        group = NULL;
    }
    level_group = NULL;
    level_root = level_bubble = level_roll_value = level_pitch_value = NULL;
    level_status = level_zero_button = NULL;
    level_feedback = NULL;
}

void level_init(void)
{
    ui_prepare_page();
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(screen, longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    /* Leave x=448..479 free for the global edge-back gesture. The root is a
     * child so cleaning the shared screen reliably emits its DELETE event. */
    level_root = lv_obj_create(screen);
    lv_obj_remove_style_all(level_root);
    lv_obj_set_pos(level_root, 0, 0);
    lv_obj_set_size(level_root, 448, EXLINK_DISPLAY_HEIGHT);
    lv_obj_clear_flag(level_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(level_root, level_page_deleted, LV_EVENT_DELETE, NULL);
    level_feedback = NULL;

    level_label(level_root, "LEVEL", 112, 26, 256, &lv_font_montserrat_28, 0x00FF7F);
    level_status = level_label(level_root, "WAITING FOR SENSOR", 76, 67, 328,
                               &lv_font_montserrat_14, 0xFFD700);

    lv_obj_t *dial = level_shape(level_root,
        LEVEL_CENTER_X - LEVEL_DIAL_SIZE / 2,
        LEVEL_CENTER_Y - LEVEL_DIAL_SIZE / 2,
        LEVEL_DIAL_SIZE, LEVEL_DIAL_SIZE, 0x101010);
    lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dial, 3, 0);
    lv_obj_set_style_border_color(dial, lv_color_hex(0xFF0000), 0);

    lv_obj_t *inner = level_shape(level_root, LEVEL_CENTER_X - 60,
        LEVEL_CENTER_Y - 60, 120, 120, 0x101010);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(inner, 1, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x444444), 0);
    level_shape(level_root, LEVEL_CENTER_X - 110, LEVEL_CENTER_Y - 1, 220, 2, 0x444444);
    level_shape(level_root, LEVEL_CENTER_X - 1, LEVEL_CENTER_Y - 110, 2, 220, 0x444444);
    lv_obj_t *center = level_shape(level_root, LEVEL_CENTER_X - 3,
        LEVEL_CENTER_Y - 3, 6, 6, 0xFFD700);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);

    level_bubble = level_shape(level_root, LEVEL_CENTER_X - LEVEL_BUBBLE_SIZE / 2,
        LEVEL_CENTER_Y - LEVEL_BUBBLE_SIZE / 2,
        LEVEL_BUBBLE_SIZE, LEVEL_BUBBLE_SIZE, 0x00FF7F);
    lv_obj_set_style_radius(level_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(level_bubble, 2, 0);
    lv_obj_set_style_border_color(level_bubble, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(level_bubble, LV_OBJ_FLAG_HIDDEN);

    level_label(level_root, "ROLL", 88, 350, 140, &lv_font_montserrat_14, 0xBBBBBB);
    level_label(level_root, "PITCH", 252, 350, 140, &lv_font_montserrat_14, 0xBBBBBB);
    level_roll_value = level_label(level_root, "--.-\xC2\xB0", 80, 371, 156,
                                   &lv_font_montserrat_28, 0x777777);
    level_pitch_value = level_label(level_root, "--.-\xC2\xB0", 244, 371, 156,
                                    &lv_font_montserrat_28, 0x777777);

    level_zero_button = lv_btn_create(level_root);
    lv_obj_set_pos(level_zero_button, 168, 418);
    lv_obj_set_size(level_zero_button, 144, 46);
    lv_obj_set_style_bg_color(level_zero_button, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(level_zero_button, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_border_width(level_zero_button, 3, 0);
    lv_obj_set_style_radius(level_zero_button, 16, 0);
    lv_obj_set_style_outline_width(level_zero_button, 0, 0);
    lv_obj_set_style_shadow_width(level_zero_button, 0, 0);
    lv_obj_set_style_pad_all(level_zero_button, 0, 0);
    lv_obj_set_style_border_color(level_zero_button, lv_color_hex(0xFFD700), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(level_zero_button, 4, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(level_zero_button, lv_color_hex(0xFFD700), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(level_zero_button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(level_zero_button, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(level_zero_button, lv_color_hex(0x281A00), LV_STATE_PRESSED);
    lv_obj_add_event_cb(level_zero_button, level_zero_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *zero_label = level_label(level_zero_button, "ZERO", 0, 0, 134,
                                      &lv_font_montserrat_20, 0xFFFFFF);
    lv_obj_center(zero_label);

    if (group) { lv_group_del(group); group = NULL; }
    level_group = lv_group_create();
    group = level_group;
    if (level_group) {
        lv_group_add_obj(level_group, level_zero_button);
        lv_group_set_wrap(level_group, true);
        lv_group_focus_obj(level_zero_button);
        if (indev_keypad) lv_indev_set_group(indev_keypad, level_group);
    }

    level_timer = lv_timer_create(level_refresh, LEVEL_REFRESH_MS, NULL);
    if (!level_timer) {
        level_status_set("DISPLAY TIMER ERROR", 0xFF5555);
        return;
    }
    level_sensor_set_active(true);
    level_refresh(NULL);
}
