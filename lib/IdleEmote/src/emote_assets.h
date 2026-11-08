/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Embedded expression data is derived from Espressif's esp_emote_assets
 * component. Its idf_component.yml declares Apache-2.0; the installed
 * component does not contain a separate LICENSE file.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace idle_emote {

enum class Emote : uint8_t {
    Neutral = 0,
    Happy,
    Shocked,
    Winking,
    Sleepy,
    Count,
};

struct AnimationInfo {
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t fps;
};

// Called once for every decoded row. rgb565 is valid only until the callback
// returns. Return false to stop decoding.
using RowCallback = bool (*)(void *user, uint16_t y,
                             const uint16_t *rgb565, uint16_t width);

// Returns metadata for one of the five embedded official expressions.
bool getInfo(Emote emote, AnimationInfo *out);

// Maps elapsed animation time to a looping frame index.
uint16_t frameForElapsed(Emote emote, uint32_t elapsed_ms);

// Decodes one frame without heap allocation. The caller supplies a row buffer
// of at least getInfo().width uint16_t values (250 bytes for these assets).
// Set byte_swap when the display path expects byte-swapped RGB565.
bool decodeFrameRows(Emote emote, uint16_t frame_index,
                     uint16_t *row_buffer, size_t row_buffer_pixels,
                     RowCallback callback, void *user,
                     bool byte_swap = false);

}  // namespace idle_emote
