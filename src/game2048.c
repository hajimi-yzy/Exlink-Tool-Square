#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <esp_system.h>

#include "ui.h"
#include "event.h"

/* 2048 rules and move/merge behavior implemented for this UI with reference to
 * MIT-licensed 2048 projects such as mevdschee/2048.c and gabrielecirulli/2048.
 */

#define GAME2048_GRID_SIZE 4
#define GAME2048_TARGET_EXP 11
#define GAME2048_SWIPE_THRESHOLD 36

#define GAME2048_BOARD_GAP 8
static lv_coord_t game2048_board_size;
static lv_coord_t game2048_tile_size;

typedef enum
{
    GAME2048_MOVE_UP = 0,
    GAME2048_MOVE_DOWN,
    GAME2048_MOVE_LEFT,
    GAME2048_MOVE_RIGHT
} game2048_move_t;

static uint8_t game2048_board[GAME2048_GRID_SIZE][GAME2048_GRID_SIZE];
static uint32_t game2048_score = 0;
static uint32_t game2048_best_score = 0;
static bool game2048_started = false;
static bool game2048_win_announced = false;

static lv_obj_t *game2048_tiles[GAME2048_GRID_SIZE][GAME2048_GRID_SIZE];
static lv_obj_t *game2048_tile_labels[GAME2048_GRID_SIZE][GAME2048_GRID_SIZE];
static lv_obj_t *game2048_score_value = NULL;
static lv_obj_t *game2048_best_value = NULL;
static lv_obj_t *game2048_status_label = NULL;

static int32_t game2048_touch_start_x = 0;
static int32_t game2048_touch_start_y = 0;

static void game2048_refresh_ui(void);
static void game2048_update_status_after_state(void);

static lv_color_t game2048_tile_color(uint8_t exponent)
{
    switch (exponent)
    {
    case 0:
        return lv_color_hex(0xCDC1B4);
    case 1:
        return lv_color_hex(0xEEE4DA);
    case 2:
        return lv_color_hex(0xEDE0C8);
    case 3:
        return lv_color_hex(0xF2B179);
    case 4:
        return lv_color_hex(0xF59563);
    case 5:
        return lv_color_hex(0xF67C5F);
    case 6:
        return lv_color_hex(0xF65E3B);
    case 7:
        return lv_color_hex(0xEDCF72);
    case 8:
        return lv_color_hex(0xEDCC61);
    case 9:
        return lv_color_hex(0xEDC850);
    case 10:
        return lv_color_hex(0xEDC53F);
    case 11:
        return lv_color_hex(0xEDC22E);
    default:
        return lv_color_hex(0x3C3A32);
    }
}

static lv_color_t game2048_text_color(uint8_t exponent)
{
    if (exponent <= 2)
    {
        return lv_color_hex(0x5F574F);
    }

    return lv_color_hex(0xF9F6F2);
}

static void game2048_set_status(const char *text)
{
    if (game2048_status_label && lv_obj_is_valid(game2048_status_label))
    {
        lv_label_set_text(game2048_status_label, text);
    }
}

static bool game2048_has_winning_tile(void)
{
    for (uint8_t row = 0; row < GAME2048_GRID_SIZE; ++row)
    {
        for (uint8_t col = 0; col < GAME2048_GRID_SIZE; ++col)
        {
            if (game2048_board[row][col] >= GAME2048_TARGET_EXP)
            {
                return true;
            }
        }
    }

    return false;
}

static bool game2048_has_moves(void)
{
    for (uint8_t row = 0; row < GAME2048_GRID_SIZE; ++row)
    {
        for (uint8_t col = 0; col < GAME2048_GRID_SIZE; ++col)
        {
            uint8_t value = game2048_board[row][col];
            if (value == 0)
            {
                return true;
            }

            if (row + 1 < GAME2048_GRID_SIZE && value == game2048_board[row + 1][col])
            {
                return true;
            }

            if (col + 1 < GAME2048_GRID_SIZE && value == game2048_board[row][col + 1])
            {
                return true;
            }
        }
    }

    return false;
}

static bool game2048_spawn_random_tile(void)
{
    uint8_t empty_count = 0;

    for (uint8_t row = 0; row < GAME2048_GRID_SIZE; ++row)
    {
        for (uint8_t col = 0; col < GAME2048_GRID_SIZE; ++col)
        {
            if (game2048_board[row][col] == 0)
            {
                ++empty_count;
            }
        }
    }

    if (empty_count == 0)
    {
        return false;
    }

    uint32_t selected_empty = esp_random() % empty_count;

    for (uint8_t row = 0; row < GAME2048_GRID_SIZE; ++row)
    {
        for (uint8_t col = 0; col < GAME2048_GRID_SIZE; ++col)
        {
            if (game2048_board[row][col] != 0)
            {
                continue;
            }

            if (selected_empty == 0)
            {
                game2048_board[row][col] = (esp_random() % 10 == 0) ? 2 : 1;
                return true;
            }

            --selected_empty;
        }
    }

    return false;
}

static bool game2048_collapse_line(uint8_t line[GAME2048_GRID_SIZE], uint32_t *score_gain)
{
    uint8_t original[GAME2048_GRID_SIZE];
    uint8_t compact[GAME2048_GRID_SIZE] = {0};
    uint8_t output[GAME2048_GRID_SIZE] = {0};
    uint8_t compact_len = 0;
    uint8_t output_len = 0;

    memcpy(original, line, sizeof(original));

    for (uint8_t index = 0; index < GAME2048_GRID_SIZE; ++index)
    {
        if (line[index] != 0)
        {
            compact[compact_len++] = line[index];
        }
    }

    for (uint8_t index = 0; index < compact_len; ++index)
    {
        if (index + 1 < compact_len && compact[index] == compact[index + 1])
        {
            output[output_len] = compact[index] + 1;
            if (score_gain != NULL)
            {
                *score_gain += (1UL << output[output_len]);
            }
            ++index;
        }
        else
        {
            output[output_len] = compact[index];
        }

        ++output_len;
    }

    memcpy(line, output, sizeof(output));
    return memcmp(original, line, sizeof(original)) != 0;
}

static bool game2048_apply_move(game2048_move_t move)
{
    bool changed = false;
    uint32_t score_gain = 0;
    uint8_t line[GAME2048_GRID_SIZE];

    for (uint8_t major = 0; major < GAME2048_GRID_SIZE; ++major)
    {
        for (uint8_t minor = 0; minor < GAME2048_GRID_SIZE; ++minor)
        {
            switch (move)
            {
            case GAME2048_MOVE_LEFT:
                line[minor] = game2048_board[major][minor];
                break;
            case GAME2048_MOVE_RIGHT:
                line[minor] = game2048_board[major][GAME2048_GRID_SIZE - 1 - minor];
                break;
            case GAME2048_MOVE_UP:
                line[minor] = game2048_board[minor][major];
                break;
            case GAME2048_MOVE_DOWN:
                line[minor] = game2048_board[GAME2048_GRID_SIZE - 1 - minor][major];
                break;
            }
        }

        if (game2048_collapse_line(line, &score_gain))
        {
            changed = true;
        }

        for (uint8_t minor = 0; minor < GAME2048_GRID_SIZE; ++minor)
        {
            switch (move)
            {
            case GAME2048_MOVE_LEFT:
                game2048_board[major][minor] = line[minor];
                break;
            case GAME2048_MOVE_RIGHT:
                game2048_board[major][GAME2048_GRID_SIZE - 1 - minor] = line[minor];
                break;
            case GAME2048_MOVE_UP:
                game2048_board[minor][major] = line[minor];
                break;
            case GAME2048_MOVE_DOWN:
                game2048_board[GAME2048_GRID_SIZE - 1 - minor][major] = line[minor];
                break;
            }
        }
    }

    if (!changed)
    {
        return false;
    }

    game2048_score += score_gain;
    if (game2048_score > game2048_best_score)
    {
        game2048_best_score = game2048_score;
    }

    game2048_spawn_random_tile();
    return true;
}

static void game2048_try_move(game2048_move_t move)
{
    if (!game2048_started)
    {
        return;
    }

    if (game2048_apply_move(move))
    {
        game2048_update_status_after_state();
        game2048_refresh_ui();
    }
}

static void game2048_start_new_game(void)
{
    memset(game2048_board, 0, sizeof(game2048_board));
    game2048_score = 0;
    game2048_win_announced = false;
    game2048_started = true;

    game2048_spawn_random_tile();
    game2048_spawn_random_tile();
    game2048_update_status_after_state();
    game2048_refresh_ui();
}

static void game2048_update_status_after_state(void)
{
    if (game2048_has_winning_tile())
    {
        game2048_win_announced = true;
    }
}

static void game2048_refresh_ui(void)
{
    char text[32];

    if (game2048_score_value && lv_obj_is_valid(game2048_score_value))
    {
        snprintf(text, sizeof(text), "%lu", (unsigned long)game2048_score);
        lv_label_set_text(game2048_score_value, text);
    }

    if (game2048_best_value && lv_obj_is_valid(game2048_best_value))
    {
        snprintf(text, sizeof(text), "%lu", (unsigned long)game2048_best_score);
        lv_label_set_text(game2048_best_value, text);
    }

    for (uint8_t row = 0; row < GAME2048_GRID_SIZE; ++row)
    {
        for (uint8_t col = 0; col < GAME2048_GRID_SIZE; ++col)
        {
            uint8_t exponent = game2048_board[row][col];
            lv_obj_t *tile = game2048_tiles[row][col];
            lv_obj_t *label = game2048_tile_labels[row][col];

            if (!tile || !label || !lv_obj_is_valid(tile) || !lv_obj_is_valid(label))
            {
                continue;
            }

            lv_obj_set_style_bg_color(tile, game2048_tile_color(exponent), 0);
            lv_obj_set_style_text_color(label, game2048_text_color(exponent), 0);
            lv_obj_set_style_text_font(label, exponent >= 10 ? &lv_font_montserrat_14 : &lv_font_montserrat_20, 0);

            if (exponent == 0)
            {
                lv_label_set_text(label, "");
            }
            else
            {
                snprintf(text, sizeof(text), "%lu", (unsigned long)(1UL << exponent));
                lv_label_set_text(label, text);
            }

            lv_obj_center(label);
        }
    }
}

static lv_obj_t *game2048_create_value_card(lv_coord_t x, lv_coord_t y, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *card = lv_obj_create(lv_scr_act());
    bool has_title = (title != NULL && title[0] != '\0');
    lv_obj_set_size(card, 108, 56);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (has_title)
    {
        lv_obj_t *title_label = lv_label_create(card);
        lv_label_set_text(title_label, title);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title_label, lv_color_hex(0xF9F6F2), 0);
        lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, -2);
    }

    *value_label = lv_label_create(card);
    lv_label_set_text(*value_label, "0");
    lv_obj_set_style_text_font(*value_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(*value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(*value_label, has_title ? LV_ALIGN_BOTTOM_MID : LV_ALIGN_CENTER, 0, has_title ? 2 : 0);

    return card;
}

static lv_obj_t *game2048_create_button(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_btn_create(lv_scr_act());
    lv_obj_set_size(button, w, h);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xD1495B), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x9C2635), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);

    return button;
}

static lv_obj_t *game2048_create_side_button(lv_coord_t x, lv_coord_t y, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *button = game2048_create_button(x, y, 72, 56, text, cb);
    lv_obj_set_style_radius(button, 12, 0);
    return button;
}

static void game2048_move_up_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game2048_try_move(GAME2048_MOVE_UP);
}

static void game2048_move_down_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game2048_try_move(GAME2048_MOVE_DOWN);
}

static void game2048_move_left_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game2048_try_move(GAME2048_MOVE_LEFT);
}

static void game2048_move_right_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game2048_try_move(GAME2048_MOVE_RIGHT);
}

static void game2048_new_game_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    game2048_start_new_game();
}

static void game2048_exit_event_cb(lv_event_t *e)
{
    longpress_event_handler_back(e);
}

static void game2048_swipe_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();

    if (!indev)
    {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED)
    {
        game2048_touch_start_x = point.x;
        game2048_touch_start_y = point.y;
        return;
    }

    if (code != LV_EVENT_RELEASED)
    {
        return;
    }

    int32_t delta_x = point.x - game2048_touch_start_x;
    int32_t delta_y = point.y - game2048_touch_start_y;
    int32_t abs_x = LV_ABS(delta_x);
    int32_t abs_y = LV_ABS(delta_y);

    if (abs_x < GAME2048_SWIPE_THRESHOLD && abs_y < GAME2048_SWIPE_THRESHOLD)
    {
        return;
    }

    if (abs_x >= abs_y)
    {
        game2048_try_move(delta_x > 0 ? GAME2048_MOVE_RIGHT : GAME2048_MOVE_LEFT);
    }
    else
    {
        game2048_try_move(delta_y > 0 ? GAME2048_MOVE_DOWN : GAME2048_MOVE_UP);
    }
}

void game2048_init(void)
{
    ui_prepare_page();
    const lv_coord_t width = lv_disp_get_hor_res(NULL);
    const lv_coord_t height = lv_disp_get_ver_res(NULL);
    const lv_coord_t available = LV_MIN(width - 64, height - 96);
    game2048_tile_size = (available - GAME2048_BOARD_GAP * 5) / 4;
    game2048_board_size = game2048_tile_size * 4 + GAME2048_BOARD_GAP * 5;
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x111111), 0);
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(lv_scr_act(), longpress_event_handler_back, LV_EVENT_LONG_PRESSED, NULL);
    lv_indev_set_group(indev_keypad, NULL);

    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xF2B179), 0);
    lv_obj_set_pos(title, 24, 20);

    game2048_create_value_card(width / 2 - 116, 8, "SCORE", &game2048_score_value);
    game2048_create_side_button(width - 208, 8, LV_SYMBOL_REFRESH, game2048_new_game_event_cb);
    game2048_create_side_button(width - 120, 8, LV_SYMBOL_LEFT, game2048_exit_event_cb);

    lv_obj_t *board = lv_obj_create(lv_scr_act());
    lv_obj_set_size(board, game2048_board_size, game2048_board_size);
    lv_obj_set_pos(board, (width - game2048_board_size) / 2, 80);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_style_radius(board, 12, 0);
    lv_obj_set_style_pad_all(board, 0, 0);
    lv_obj_clear_flag(board, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t row = 0; row < GAME2048_GRID_SIZE; ++row)
    {
        for (uint8_t col = 0; col < GAME2048_GRID_SIZE; ++col)
        {
            lv_obj_t *tile = lv_obj_create(board);
            lv_obj_set_size(tile, game2048_tile_size, game2048_tile_size);
            lv_obj_set_pos(tile,
                           GAME2048_BOARD_GAP + col * (game2048_tile_size + GAME2048_BOARD_GAP),
                           GAME2048_BOARD_GAP + row * (game2048_tile_size + GAME2048_BOARD_GAP));
            lv_obj_set_style_bg_color(tile, game2048_tile_color(0), 0);
            lv_obj_set_style_border_width(tile, 0, 0);
            lv_obj_set_style_radius(tile, 8, 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);

            game2048_tiles[row][col] = tile;
            game2048_tile_labels[row][col] = lv_label_create(tile);
            lv_obj_set_style_pad_all(tile, 0, 0);
            lv_obj_set_style_text_font(game2048_tile_labels[row][col], &lv_font_montserrat_28, 0);
            lv_obj_center(game2048_tile_labels[row][col]);
        }
    }

    lv_obj_t *touch_layer = lv_obj_create(board);
    lv_obj_set_size(touch_layer, game2048_board_size, game2048_board_size);
    lv_obj_set_pos(touch_layer, 0, 0);
    lv_obj_set_style_bg_opa(touch_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_layer, 0, 0);
    lv_obj_set_style_pad_all(touch_layer, 0, 0);
    lv_obj_set_style_radius(touch_layer, 12, 0);
    lv_obj_add_flag(touch_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(touch_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch_layer, game2048_swipe_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(touch_layer, game2048_swipe_event_cb, LV_EVENT_RELEASED, NULL);

    game2048_best_value = NULL;
    game2048_status_label = NULL;

    if (!game2048_started)
    {
        game2048_start_new_game();
    }
    else
    {
        game2048_update_status_after_state();
        game2048_refresh_ui();
    }
}
