#include "environment_page.h"
#include "display_geometry.h"

#include <math.h>
#include <stdio.h>
#include <lvgl.h>

extern void ui_prepare_page(void);
extern void longpress_event_handler_back(lv_event_t *event);
extern lv_group_t *group;
extern lv_indev_t *indev_keypad;

enum { ENVIRONMENT_REFRESH_MS = 250 };

static lv_obj_t *environment_root;
static lv_obj_t *temperature_value;
static lv_obj_t *humidity_value;
static lv_obj_t *environment_status;
static lv_timer_t *environment_timer;

static lv_obj_t *environment_label(lv_obj_t *parent, const char *text,
                                   lv_coord_t x, lv_coord_t y, lv_coord_t width,
                                   const lv_font_t *font, uint32_t color,
                                   lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_text(label, text);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static lv_obj_t *environment_card(const char *title, const char *unit,
                                  lv_coord_t y)
{
    lv_obj_t *card = lv_obj_create(environment_root);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 24, y);
    lv_obj_set_size(card, 400, 126);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0C0C0C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xFF0000), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    environment_label(card, title, 20, 15, 360, &lv_font_montserrat_16,
                       0xBBBBBB, LV_TEXT_ALIGN_LEFT);
    environment_label(card, unit, 302, 63, 74, &lv_font_montserrat_24,
                       0xFF5555, LV_TEXT_ALIGN_RIGHT);
    return environment_label(card, "--.-", 20, 47, 270,
                              &lv_font_montserrat_48, 0x777777,
                              LV_TEXT_ALIGN_LEFT);
}

static void environment_status_set(const char *text, uint32_t color)
{
    lv_label_set_text(environment_status, text);
    lv_obj_set_style_text_color(environment_status, lv_color_hex(color), 0);
}

static void environment_refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    environment_reading_t reading = {0};
    const bool fresh = environment_sensor_read(&reading);
    const bool valid_values = isfinite(reading.temperature_c) &&
                              isfinite(reading.humidity_rh) &&
                              reading.temperature_c >= -50.0f &&
                              reading.temperature_c <= 150.0f &&
                              reading.humidity_rh >= 0.0f &&
                              reading.humidity_rh <= 100.0f;
    if (!fresh || !reading.available || !reading.calibrated ||
        reading.error != ENVIRONMENT_ERROR_NONE || !valid_values) {
        /* A failed or disconnected sensor must never look like a real zero. */
        lv_label_set_text(temperature_value, "--.-");
        lv_label_set_text(humidity_value, "--.-");
        lv_obj_set_style_text_color(temperature_value, lv_color_hex(0x777777), 0);
        lv_obj_set_style_text_color(humidity_value, lv_color_hex(0x777777), 0);
        if (!fresh) {
            environment_status_set("DATA STALE - WAITING", 0xFFD700);
        } else if (reading.error == ENVIRONMENT_ERROR_MISSING) {
            environment_status_set("SENSOR NOT FOUND", 0xFF5555);
        } else if (reading.error == ENVIRONMENT_ERROR_READ_FAILED ||
                   (reading.available && !valid_values)) {
            environment_status_set("SENSOR READ FAILED", 0xFF5555);
        } else if (!reading.calibrated ||
                   reading.error == ENVIRONMENT_ERROR_INITIALIZING) {
            environment_status_set("INITIALIZING SENSOR", 0xFFD700);
        } else {
            environment_status_set("WAITING FOR FIRST SAMPLE", 0xFFD700);
        }
        return;
    }

    char value[24];
    const float temperature = fabsf(reading.temperature_c) < 0.05f ?
                               0.0f : reading.temperature_c;
    snprintf(value, sizeof(value), "%.1f", (double)temperature);
    lv_label_set_text(temperature_value, value);
    snprintf(value, sizeof(value), "%.1f", (double)reading.humidity_rh);
    lv_label_set_text(humidity_value, value);
    lv_obj_set_style_text_color(temperature_value, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(humidity_value, lv_color_hex(0xFFFFFF), 0);
    environment_status_set("LIVE", 0x00FF7F);
}

static void environment_page_deleted(lv_event_t *event)
{
    LV_UNUSED(event);
    if (environment_timer) {
        lv_timer_del(environment_timer);
        environment_timer = NULL;
    }
    environment_sensor_set_active(false);
    environment_root = temperature_value = humidity_value = environment_status = NULL;
}

void environment_init(void)
{
    ui_prepare_page();
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_add_event_cb(screen, longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);

    /* This read-only page has no focus targets. Detach the previous menu group
     * so the keypad cannot keep pointing at deleted menu cards. */
    if (indev_keypad) lv_indev_set_group(indev_keypad, NULL);
    if (group) {
        lv_group_del(group);
        group = NULL;
    }

    environment_root = lv_obj_create(screen);
    lv_obj_remove_style_all(environment_root);
    lv_obj_set_pos(environment_root, 0, 0);
    lv_obj_set_size(environment_root, 448, EXLINK_DISPLAY_HEIGHT);
    lv_obj_clear_flag(environment_root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(environment_root, environment_page_deleted, LV_EVENT_DELETE, NULL);

    environment_label(environment_root, "ENVIRONMENT", 24, 25, 400,
                       &lv_font_montserrat_28, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
    environment_label(environment_root, "AHT20  |  0x38", 24, 66, 400,
                       &lv_font_montserrat_14, 0x888888, LV_TEXT_ALIGN_CENTER);
    environment_status = environment_label(environment_root, "INITIALIZING SENSOR",
                       24, 98, 400, &lv_font_montserrat_14, 0xFFD700,
                       LV_TEXT_ALIGN_CENTER);
    temperature_value = environment_card("TEMPERATURE", "\xC2\xB0" "C", 132);
    humidity_value = environment_card("RELATIVE HUMIDITY", "%RH", 274);
    environment_label(environment_root, "Updates about once per second", 24, 421, 400,
                       &lv_font_montserrat_14, 0x888888, LV_TEXT_ALIGN_CENTER);
    environment_label(environment_root, "Right edge: BACK", 24, 448, 400,
                       &lv_font_montserrat_14, 0xBBBBBB, LV_TEXT_ALIGN_CENTER);

    environment_timer = lv_timer_create(environment_refresh, ENVIRONMENT_REFRESH_MS, NULL);
    if (!environment_timer) {
        environment_status_set("DISPLAY TIMER ERROR", 0xFF5555);
        return;
    }
    environment_sensor_set_active(true);
    environment_refresh(NULL);
}
