/* Whole production ui.c + scope_page.c on real LVGL, simulated hardware. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "level_page.h"
#include "environment_page.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    exit(EXIT_FAILURE); } } while (0)

enum { WIDTH = EXLINK_DISPLAY_WIDTH, HEIGHT = EXLINK_DISPLAY_HEIGHT };
static lv_color_t framebuffer[WIDTH * HEIGHT], draw_pixels[WIDTH * 32];

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    CHECK(area->x1 >= 0 && area->y1 >= 0 && area->x2 < WIDTH && area->y2 < HEIGHT);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) framebuffer[y * WIDTH + x] = *pixels++;
    lv_disp_flush_ready(driver);
}

static void key_read(lv_indev_drv_t *driver, lv_indev_data_t *data)
{ (void)driver; data->state = LV_INDEV_STATE_REL; data->key = LV_KEY_ENTER; }

static void advance(unsigned milliseconds)
{
    for (unsigned elapsed = 0; elapsed < milliseconds; elapsed += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
    lv_anim_refr_now();
    lv_obj_update_layout(lv_scr_act());
    lv_refr_now(NULL);
}

static const char *type(lv_obj_t *obj)
{
    if (lv_obj_check_type(obj, &lv_label_class)) return "label";
    if (lv_obj_check_type(obj, &lv_img_class)) return "image";
    if (lv_obj_check_type(obj, &lv_btn_class)) return "button";
    if (lv_obj_check_type(obj, &lv_chart_class)) return "chart";
    if (lv_obj_check_type(obj, &lv_dropdown_class)) return "dropdown";
    if (lv_obj_check_type(obj, &lv_textarea_class)) return "textarea";
    if (lv_obj_check_type(obj, &lv_switch_class)) return "switch";
    if (lv_obj_check_type(obj, &lv_slider_class)) return "slider";
    if (lv_obj_check_type(obj, &lv_keyboard_class)) return "keyboard";
    return "object";
}

static void geometry(FILE *file, lv_obj_t *obj, const char *path, unsigned *count)
{
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    fprintf(file, "%s{\"path\":\"%s\",\"type\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"hidden\":%s,\"font_height\":%u",
        (*count)++ ? ",\n" : "", path, type(obj), area.x1, area.y1,
        lv_obj_get_width(obj), lv_obj_get_height(obj),
        lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) ? "true" : "false",
        lv_obj_get_style_text_font(obj, LV_PART_MAIN)->line_height);
    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        lv_obj_t *list = lv_dropdown_get_list(obj);
        fprintf(file, ",\"selected_font_height\":%u,\"items_font_height\":%u,\"list_font_height\":%u",
            lv_obj_get_style_text_font(obj, LV_PART_SELECTED)->line_height,
            lv_obj_get_style_text_font(obj, LV_PART_ITEMS)->line_height,
            list ? lv_obj_get_style_text_font(list, LV_PART_MAIN)->line_height : 0);
    }
    fputc('}', file);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i) {
        char child[256];
        CHECK(snprintf(child, sizeof(child), "%s/%u", path, i) < sizeof(child));
        geometry(file, lv_obj_get_child(obj, i), child, count);
    }
}

static void export_page(const char *directory, const char *name)
{
    char path[1024];
    CHECK(snprintf(path, sizeof(path), "%s/%s.json", directory, name) < sizeof(path));
    FILE *file = fopen(path, "wb");
    CHECK(file);
    unsigned count = 0;
    fputs("[\n", file);
    geometry(file, lv_scr_act(), "screen", &count);
    fputs("\n]\n", file);
    CHECK(fclose(file) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/%s.ppm", directory, name) < sizeof(path));
    file = fopen(path, "wb");
    CHECK(file);
    fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
        const uint32_t rgb = lv_color_to32(framebuffer[i]);
        const unsigned char pixel[] = {(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255};
        CHECK(fwrite(pixel, 1, 3, file) == 3);
    }
    CHECK(fclose(file) == 0);
    printf("PASS: complete production %s page, %u objects at %dx%d\n", name, count, WIDTH, HEIGHT);
}

static void boot_geometry(void)
{
    create_boot_animation();
    advance(2200);
    CHECK(lv_obj_get_x(ui_Image1) == 17 && lv_obj_get_y(ui_Image1) == 12);
    CHECK(lv_obj_get_width(ui_Image1) == 200 && lv_obj_get_height(ui_Image1) == 87);
    CHECK(lv_obj_get_x(ui_Image2) == 12 && lv_obj_get_y(ui_Image2) == 105);
    CHECK(lv_obj_get_width(ui_Image2) == 210 && lv_obj_get_height(ui_Image2) == 33);
    lv_obj_t *caption = lv_obj_get_child(lv_scr_act(), 2);
    CHECK(lv_obj_get_x(caption) == 12 && lv_obj_get_y(caption) == 150);
    CHECK(lv_obj_get_width(caption) == 154 && lv_obj_get_height(caption) == 34);
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, draw_pixels, NULL, WIDTH * 32);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = WIDTH; display.ver_res = HEIGHT;
    display.draw_buf = &buffer; display.flush_cb = flush;
    CHECK(lv_disp_drv_register(&display));
    CHECK(lv_disp_get_hor_res(NULL) == EXLINK_DISPLAY_WIDTH);
    CHECK(lv_disp_get_ver_res(NULL) == EXLINK_DISPLAY_HEIGHT);
    CHECK(LV_FONT_DEFAULT == &lv_font_montserrat_20);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input); input.type = LV_INDEV_TYPE_KEYPAD; input.read_cb = key_read;
    indev_keypad = lv_indev_drv_register(&input);
    CHECK(indev_keypad);
    lv_theme_default_init(NULL, lv_color_hex(0), lv_color_hex(0xFF0000), true, NULL);
    if (strcmp(argv[2], "baseline")) {
        boot_geometry();
        export_page(argv[1], "boot");
    }
    const struct { const char *name; void (*create)(void); } pages[] = {
        {"menu", ui_Screen1_screen_init}, {"power", power_init},
        {"settings", settings_init}, {"scope", DSO_init},
        {"pinmap", pinmap_init}, {"pwm", pwm_init}, {"servo", servo_init},
        {"uart", uarthelper_init}, {"i2c", i2c_init}, {"spi", spi_init},
        {"voltmeter", voltmeter_init}, {"wireless", wirelessuart_init},
        {"frequency", FREcount_init}, {"readme", readme_init},
        {"level", level_init}, {"environment", environment_init},
    };
    for (unsigned i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        pages[i].create();
        advance(100);
        if (pages[i].create == ui_Screen1_screen_init) {
            CHECK(UI_MENU_ITEM_COUNT == 16 && lv_group_get_obj_count(group) == 16);
            CHECK(lv_obj_get_index(btn_level) == 11 && lv_obj_get_index(btn_environment) == 12);
            CHECK(lv_obj_get_index(btn12) == 13 && lv_obj_get_index(btn14) == 15);
            CHECK(lv_slider_get_min_value(slider) == 0 && lv_slider_get_max_value(slider) == 15);
        }
        CHECK(lv_obj_get_width(lv_scr_act()) == WIDTH && lv_obj_get_height(lv_scr_act()) == HEIGHT);
        export_page(argv[1], pages[i].name);
        if (pages[i].create == wirelessuart_init) {
            static const char *const modes[] = {"wireless-uart", "wireless-i2c", "wireless-listen", "wireless-spi"};
            for (unsigned mode = 0; mode < 4; ++mode) {
                lv_dropdown_set_selected(wireless_uart_list, mode);
                lv_event_send(wireless_uart_list, LV_EVENT_VALUE_CHANGED, NULL);
                advance(20);
                export_page(argv[1], modes[mode]);
            }
        }
    }
    ui_prepare_page();
    return 0;
}
