/* Real LVGL + extracted production boot page, callbacks and A8 resources.
 * Hardware and the main-menu constructor are not part of this host test. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "display_geometry.h"

/* Fail without invoking Windows' interactive C-runtime abort dialog. */
#undef assert
#define assert(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Assertion failed: %s, %s:%d\n", #condition, __FILE__, __LINE__); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

enum { PANEL = EXLINK_DISPLAY_WIDTH, CAPTION_X = 12, CAPTION_Y = 150, CAPTION_W = 154, CAPTION_H = 34 };
_Static_assert(EXLINK_DISPLAY_WIDTH == EXLINK_DISPLAY_HEIGHT, "Boot preview requires square native panel");
static lv_color_t framebuffer[PANEL * PANEL], dirty_frame[PANEL * PANEL];
static lv_color_t final_frame[PANEL * PANEL], draw_pixels[PANEL * 32];
lv_obj_t *ui_Image1, *ui_Image2;
static unsigned menu_entries, flushed;

static void ui_prepare_page(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static void ui_Screen1_screen_init(void)
{
    ++menu_entries;
    lv_obj_clean(lv_scr_act());
}

#include "production_boot.inc"

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    assert(area->x1 >= 0 && area->y1 >= 0 && area->x2 < PANEL && area->y2 < PANEL);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x)
            framebuffer[y * PANEL + x] = *pixels++;
    ++flushed;
    lv_disp_flush_ready(driver);
}

static void advance(unsigned milliseconds)
{
    for (unsigned elapsed = 0; elapsed < milliseconds;) {
        const unsigned step = LV_MIN(5, milliseconds - elapsed);
        lv_tick_inc(step);
        lv_timer_handler();
        elapsed += step;
    }
    /* Sample exact boundaries as well as the normal display timer cadence. */
    lv_anim_refr_now();
}

static void export_frame(const char *directory, unsigned index)
{
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/frame-%03u.ppm", directory, index) < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fprintf(file, "P6\n%d %d\n255\n", PANEL, PANEL) > 0);
    for (unsigned i = 0; i < PANEL * PANEL; ++i) {
        const uint32_t rgb = lv_color_to32(framebuffer[i]);
        const unsigned char pixel[] = {(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255};
        assert(fwrite(pixel, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}

static lv_obj_t *caption(void)
{
    return lv_obj_get_child(lv_scr_act(), 2);
}

static lv_obj_t *cover(void)
{
    return lv_obj_get_child(caption(), 1);
}

static void check_no_lines(lv_obj_t *parent)
{
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        assert(!lv_obj_check_type(child, &lv_line_class));
        check_no_lines(child);
    }
}

static void check_wordmark_pixels(void)
{
    const int left = lv_obj_get_x(ui_Image2), top = lv_obj_get_y(ui_Image2);
    unsigned red = 0, white = 0;
    for (int y = 0; y < 33; ++y) {
        if (top + y < 0 || top + y >= PANEL) continue;
        for (int x = 0; x < 210; ++x) {
            if (left + x < 0 || left + x >= PANEL) continue;
            const unsigned offset = (y * 210 + x) * 3;
            lv_color_t original;
            original.full = Exlink_png.data[offset] | (Exlink_png.data[offset + 1] << 8);
            const lv_color_t expected = lv_color_mix(original, lv_color_black(), Exlink_png.data[offset + 2]);
            const lv_color_t actual = framebuffer[(top + y) * PANEL + left + x];
            assert(actual.full == expected.full);
            if (actual.full == lv_color_hex(0xFF0000).full) ++red;
            if (actual.full == lv_color_white().full) ++white;
        }
    }
    if (top == 105) assert(red && white);
}

static void check_frame(void)
{
    lv_refr_now(NULL);
    memcpy(dirty_frame, framebuffer, sizeof(framebuffer));
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    if (memcmp(dirty_frame, framebuffer, sizeof(framebuffer))) {
        fprintf(stderr, "Dirty redraw differs from full redraw at tick %u\n", (unsigned)lv_tick_get());
        abort();
    }
    assert(lv_obj_get_child_cnt(lv_scr_act()) == 4);
    assert(lv_obj_get_child(lv_scr_act(), 0) == ui_Image1);
    assert(lv_obj_get_child(lv_scr_act(), 1) == ui_Image2);
    assert(lv_obj_get_y(ui_Image1) == 12 && lv_obj_get_width(ui_Image1) == 200);
    assert(lv_obj_get_height(ui_Image1) == 87);
    assert(lv_obj_get_x(ui_Image2) == 12 && lv_obj_get_width(ui_Image2) == 210);
    assert(lv_obj_get_height(ui_Image2) == 33);
    assert(lv_obj_get_x(caption()) == CAPTION_X && lv_obj_get_y(caption()) == CAPTION_Y);
    assert(lv_obj_get_width(caption()) == CAPTION_W && lv_obj_get_height(caption()) == CAPTION_H);
    assert(lv_obj_get_child_cnt(caption()) == 2);
    assert(lv_img_get_src(ui_Image1) == &boot_portrait_white);
    assert(lv_img_get_src(ui_Image2) == &Exlink_png);
    assert(lv_img_get_src(lv_obj_get_child(caption(), 0)) == &boot_caption_white);
    check_no_lines(lv_scr_act());
    for (unsigned i = 0; i < PANEL * PANEL; ++i) {
        const int x = i % PANEL, y = i / PANEL;
        const int wordmark_x = lv_obj_get_x(ui_Image2), wordmark_y = lv_obj_get_y(ui_Image2);
        if (x >= wordmark_x && x < wordmark_x + 210 && y >= wordmark_y && y < wordmark_y + 33) continue;
        const uint32_t rgb = lv_color_to32(framebuffer[i]);
        const int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
        /* RGB565 grey may differ by one 5-bit level across channels. */
        assert(abs(r - g) <= 8 && abs(r - b) <= 8);
    }
    check_wordmark_pixels();
}

static void check_final_positions(void)
{
    assert(lv_obj_get_x(ui_Image1) == 17 && lv_obj_get_y(ui_Image1) == 12);
    assert(lv_obj_get_y(ui_Image2) == 105);
    assert(lv_obj_get_y(ui_Image1) + lv_obj_get_height(ui_Image1) + 6 == lv_obj_get_y(ui_Image2));
    assert(lv_obj_get_y(ui_Image2) + lv_obj_get_height(ui_Image2) + 12 == CAPTION_Y);
}

static void check_caption_pixels(int revealed, int covered)
{
    assert(revealed >= 0 && covered >= 0 && revealed + covered <= CAPTION_W);
    for (int y = 0; y < CAPTION_H; ++y) {
        for (int x = 0; x < CAPTION_W; ++x) {
            const int expected = x < revealed ? boot_caption_white.data[y * CAPTION_W + x] :
                                 x < revealed + covered ? 255 : 0;
            const lv_color_t actual = framebuffer[(CAPTION_Y + y) * PANEL + CAPTION_X + x];
            const lv_color_t oracle = lv_color_mix(lv_color_white(), lv_color_black(), expected);
            if (actual.full != oracle.full) {
                fprintf(stderr, "Caption pixel mismatch: tick=%u xy=(%d,%d) reveal=%d cover=%d alpha=%d expected565=0x%04x actual565=0x%04x\n",
                        (unsigned)lv_tick_get(), x, y, revealed, covered, expected, oracle.full, actual.full);
            }
            assert(actual.full == oracle.full);
        }
    }
}

static void check_animation_contract(unsigned baseline)
{
    assert(lv_anim_count_running() == baseline + 4);
    lv_anim_t *portrait = lv_anim_get(ui_Image1, anim_cb1);
    lv_anim_t *wordmark = lv_anim_get(ui_Image2, anim_cb2);
    lv_anim_t *reveal = lv_anim_get(caption(), boot_caption_progress);
    assert(portrait && portrait->time == 450 && portrait->act_time == 0);
    assert(wordmark && wordmark->time == 450 && wordmark->act_time == -200);
    assert(reveal && reveal->time == 1300 && reveal->act_time == -850);
    assert(reveal->start_value == 0 && reveal->end_value == 1300 && !reveal->early_apply);
    assert(reveal->path_cb == lv_anim_path_linear);
    assert(lv_obj_has_flag(cover(), LV_OBJ_FLAG_HIDDEN));
    assert(boot_portrait_white.header.cf == LV_IMG_CF_ALPHA_8BIT);
    assert(Exlink_png.header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
    assert(boot_caption_white.header.cf == LV_IMG_CF_ALPHA_8BIT);
    assert(boot_portrait_white.data_size == 200 * 87);
    assert(Exlink_png.data_size == 210 * 33 * 3);
    assert(boot_caption_white.data_size == CAPTION_W * CAPTION_H);
}

static void check_precise_caption_phases(unsigned baseline)
{
    create_boot_animation();
    advance(700);
    check_frame();
    check_final_positions();
    check_caption_pixels(0, 0);
    lv_anim_del(caption(), boot_caption_progress);
    const int phases[] = {0, 250, 500, 599, 600, 950, 1299, 1300};
    int previous_width = 0;
    for (unsigned i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        const int value = phases[i];
        boot_caption_progress(caption(), value);
        check_frame();
        if (value < 500) {
            const int width = lv_obj_get_width(cover());
            assert(lv_obj_get_x(cover()) == 0 && lv_obj_get_y(cover()) == 0);
            assert(lv_obj_get_height(cover()) == CAPTION_H);
            assert(width >= CAPTION_H && width < CAPTION_W && width > previous_width);
            if (!value) assert(width == CAPTION_H);
            check_caption_pixels(0, width);
            previous_width = width;
        } else if (value <= 600) {
            assert(lv_obj_get_x(cover()) == 0 && lv_obj_get_width(cover()) == CAPTION_W);
            check_caption_pixels(0, CAPTION_W);
        } else if (value < 1300) {
            const int revealed = lv_obj_get_x(cover());
            assert(abs(revealed - CAPTION_W * (value - 600) / 700) <= 1);
            assert(lv_obj_get_width(cover()) == CAPTION_W - revealed);
            check_caption_pixels(revealed, CAPTION_W - revealed);
        } else {
            assert(lv_obj_has_flag(cover(), LV_OBJ_FLAG_HIDDEN));
            check_caption_pixels(CAPTION_W, 0);
            assert(memcmp(final_frame, framebuffer, sizeof(final_frame)) == 0);
        }
    }
    ui_prepare_page();
    assert(lv_anim_count_running() == baseline);
}

static void check_early_exit(unsigned elapsed, unsigned baseline)
{
    const unsigned entries = menu_entries;
    create_boot_animation();
    advance(elapsed);
    check_frame();
    assert(!lv_obj_has_flag(cover(), LV_OBJ_FLAG_HIDDEN));
    ui_prepare_page();
    assert(lv_obj_get_child_cnt(lv_scr_act()) == 0);
    assert(lv_anim_count_running() == baseline);
    advance(4000);
    assert(lv_anim_count_running() == baseline && menu_entries == entries);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, draw_pixels, NULL, PANEL * 32);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = display.ver_res = PANEL;
    display.draw_buf = &buffer;
    display.flush_cb = flush;
    assert(lv_disp_drv_register(&display));
    assert(lv_disp_get_hor_res(NULL) == EXLINK_DISPLAY_WIDTH);
    assert(lv_disp_get_ver_res(NULL) == EXLINK_DISPLAY_HEIGHT);
    lv_theme_default_init(NULL, lv_color_hex(0x000000), lv_color_hex(0xFF0000),
                          LV_THEME_DEFAULT_DARK, NULL);
    advance(20);
    const unsigned baseline = lv_anim_count_running();
    create_boot_animation();
    check_animation_contract(baseline);
    for (unsigned index = 0; index < 90; ++index) {
        const unsigned time = index * 40;
        if (index) advance(40);
        check_frame();
        assert(!menu_entries);
        if (time < 850) {
            assert(lv_obj_has_flag(cover(), LV_OBJ_FLAG_HIDDEN));
            if (time >= 680) check_caption_pixels(0, 0);
        } else if (time < 1350) {
            assert(!lv_obj_has_flag(cover(), LV_OBJ_FLAG_HIDDEN));
            assert(lv_obj_get_x(cover()) == 0);
            const int width = lv_obj_get_width(cover());
            assert(width >= CAPTION_H && width <= CAPTION_W);
            check_caption_pixels(0, width);
        } else if (time < 1450) {
            assert(lv_obj_get_x(cover()) == 0 && lv_obj_get_width(cover()) == CAPTION_W);
            check_caption_pixels(0, CAPTION_W);
        } else if (time < 2150) {
            const int revealed = lv_obj_get_x(cover());
            assert(abs(revealed - CAPTION_W * (int)(time - 1450) / 700) <= 2);
            assert(lv_obj_get_width(cover()) == CAPTION_W - revealed);
            check_caption_pixels(revealed, CAPTION_W - revealed);
        } else {
            assert(lv_obj_has_flag(cover(), LV_OBJ_FLAG_HIDDEN));
            check_caption_pixels(CAPTION_W, 0);
            if (index == 54) memcpy(final_frame, framebuffer, sizeof(final_frame));
            else assert(memcmp(final_frame, framebuffer, sizeof(final_frame)) == 0);
        }
        if (time >= 680) check_final_positions();
        export_frame(argv[1], index);
    }
    assert(flushed && !menu_entries);
    advance(35); /* 3595ms: final artwork remains until the 3600ms menu transition. */
    check_frame();
    assert(!menu_entries && memcmp(final_frame, framebuffer, sizeof(final_frame)) == 0);
    advance(5);
    assert(menu_entries == 1 && ui_Image1 == NULL && ui_Image2 == NULL);
    assert(lv_obj_get_child_cnt(lv_scr_act()) == 0);
    assert(lv_anim_count_running() == baseline);
    check_precise_caption_phases(baseline);
    assert(menu_entries == 1);
    check_early_exit(1100, baseline); /* Growth phase. */
    check_early_exit(1800, baseline); /* Wipe phase. */
    for (unsigned repeat = 0; repeat < 10; ++repeat) {
        create_boot_animation();
        check_animation_contract(baseline);
        advance(3600);
        assert(menu_entries == repeat + 2);
        assert(ui_Image1 == NULL && ui_Image2 == NULL);
        assert(lv_obj_get_child_cnt(lv_scr_act()) == 0);
        assert(lv_anim_count_running() == baseline);
    }
    puts("PASS: 90 real LVGL frames, dirty redraw, original red/white wordmark pixels, white caption growth/hold/wipe/final pixels");
    puts("PASS: precise caption boundaries, 3600ms menu exit, growth/wipe early cleanup, 10 repeats without animations left");
    puts("Final objects: portrait=(17,12) 200x87; wordmark=(12,105) 210x33; caption=(12,150) 154x34");
    return 0;
}
