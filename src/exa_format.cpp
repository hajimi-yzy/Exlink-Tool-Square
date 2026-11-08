#include "exa_format.h"
#include <limits.h>

namespace exlink {
namespace exa {
namespace {
const size_t kHeader = 32;
const size_t kEntry = 12;
const uint16_t kEdge = 480;
const uint16_t kMaxFrames = 250;
const uint32_t kMaxDuration = 25000;

uint16_t word(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}
uint32_t dword(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}
bool header(const uint8_t *data, size_t size, uint16_t *count, uint32_t *duration) {
    if (!data || size < kHeader || size > UINT32_MAX) return false;
    if (data[0] != 'E' || data[1] != 'X' || data[2] != 'A' || data[3] != '1' ||
        word(data + 4) != 1 || word(data + 6) != kHeader ||
        word(data + 8) != kEdge || word(data + 10) != kEdge ||
        word(data + 14) != 1 || dword(data + 16) != size ||
        dword(data + 24) != kHeader || dword(data + 28) != 0) return false;
    *count = word(data + 12);
    *duration = dword(data + 20);
    return *count > 0 && *count <= kMaxFrames && *duration > 0 &&
           *duration <= kMaxDuration &&
           size >= kHeader + static_cast<size_t>(*count) * kEntry;
}
bool matchingView(const View &view) {
    uint16_t count = 0;
    uint32_t duration = 0;
    return header(view.data, view.size, &count, &duration) &&
           view.frameCount == count && view.durationMs == duration;
}
bool rows(const uint8_t *data, size_t length, uint16_t *row,
          RowCallback callback, void *context) {
    size_t cursor = 0;
    for (uint16_t y = 0; y < kEdge; ++y) {
        uint16_t x = 0;
        while (x < kEdge) {
            if (length - cursor < 4) return false;
            const uint16_t count = word(data + cursor);
            const uint16_t color = word(data + cursor + 2);
            cursor += 4;
            if (!count || count > kEdge - x) return false;
            if (row) {
                for (uint16_t i = 0; i < count; ++i) row[x + i] = color;
            }
            x = static_cast<uint16_t>(x + count);
        }
        if (callback && !callback(y, row, kEdge, context)) return false;
    }
    return cursor == length;
}
} // namespace

bool validate(const uint8_t *data, size_t size, View *output) {
    if (output) *output = View{nullptr, 0, 0, 0};
    uint16_t count = 0;
    uint32_t duration = 0;
    if (!header(data, size, &count, &duration)) return false;
    size_t next = kHeader + static_cast<size_t>(count) * kEntry;
    uint32_t sum = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *entry = data + kHeader + static_cast<size_t>(i) * kEntry;
        const uint32_t offset = dword(entry);
        const uint32_t length = dword(entry + 4);
        const uint32_t frameDuration = dword(entry + 8);
        if (offset != next || length > size - next || !frameDuration ||
            frameDuration > kMaxDuration || frameDuration > kMaxDuration - sum)
            return false;
        if (!rows(data + next, length, nullptr, nullptr, nullptr)) return false;
        next += length;
        sum += frameDuration;
    }
    if (next != size || sum != duration) return false;
    if (output) *output = View{data, size, count, duration};
    return true;
}

uint16_t frameForElapsed(const View &view, uint32_t elapsedMs) {
    if (!matchingView(view)) return 0;
    uint32_t remainder = elapsedMs % view.durationMs;
    for (uint16_t i = 0; i < view.frameCount; ++i) {
        const uint32_t duration = dword(view.data + kHeader +
                                       static_cast<size_t>(i) * kEntry + 8);
        if (!duration || duration > kMaxDuration) return 0;
        if (remainder < duration) return i;
        remainder -= duration;
    }
    return 0;
}

bool decodeFrameRows(const View &view, uint16_t index, uint16_t *row,
                     size_t capacity, RowCallback callback, void *context) {
    if (!row || capacity < kEdge || !callback || !matchingView(view) ||
        index >= view.frameCount) return false;
    const uint8_t *entry = view.data + kHeader + static_cast<size_t>(index) * kEntry;
    const uint32_t offset = dword(entry);
    const uint32_t length = dword(entry + 4);
    if (offset < kHeader + static_cast<size_t>(view.frameCount) * kEntry ||
        offset > view.size || length > view.size - offset) return false;
    return rows(view.data + offset, length, row, callback, context);
}

} // namespace exa
} // namespace exlink
