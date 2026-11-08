#pragma once
#include <stddef.h>
#include <stdint.h>

namespace exlink {
namespace exa {

// Non-owning view. The validated bytes must remain alive and immutable.
struct View {
    const uint8_t *data;
    size_t size;
    uint16_t frameCount;
    uint32_t durationMs;
};

typedef bool (*RowCallback)(uint16_t rowIndex, const uint16_t *pixels,
                            size_t width, void *context);

// Validates every frame without allocation. Clears output on failure.
bool validate(const uint8_t *data, size_t size, View *output);
// Animation loops at durationMs; returns zero for an invalid/empty view.
uint16_t frameForElapsed(const View &view, uint32_t elapsedMs);
// Requires a validated immutable View. Pixels are RGB565 host-endian words.
// row must contain at least 480 words; callback false cancels and returns false.
bool decodeFrameRows(const View &view, uint16_t index, uint16_t *row,
                     size_t capacity, RowCallback callback, void *context);

} // namespace exa
} // namespace exlink
