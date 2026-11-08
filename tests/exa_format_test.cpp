#include "exa_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using exlink::exa::View;
using exlink::exa::validate;
using exlink::exa::frameForElapsed;
using exlink::exa::decodeFrameRows;

static void put16(std::vector<uint8_t> &data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}
static void put32(std::vector<uint8_t> &data, size_t offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
static std::vector<uint8_t> asset(uint16_t frames = 2) {
    const size_t start = 32 + static_cast<size_t>(frames) * 12;
    std::vector<uint8_t> data(start + static_cast<size_t>(frames) * 480 * 4, 0);
    memcpy(data.data(), "EXA1", 4);
    put16(data, 4, 1); put16(data, 6, 32);
    put16(data, 8, 480); put16(data, 10, 480);
    put16(data, 12, frames); put16(data, 14, 1);
    put32(data, 16, static_cast<uint32_t>(data.size()));
    put32(data, 20, frames * 100); put32(data, 24, 32);
    for (uint16_t i = 0; i < frames; ++i) {
        const size_t offset = start + static_cast<size_t>(i) * 1920;
        put32(data, 32 + i * 12, static_cast<uint32_t>(offset));
        put32(data, 36 + i * 12, 1920); put32(data, 40 + i * 12, 100);
        for (uint16_t y = 0; y < 480; ++y) {
            put16(data, offset + y * 4, 480);
            put16(data, offset + y * 4 + 2, static_cast<uint16_t>(0x1234 + i));
        }
    }
    return data;
}
static void reject(const std::vector<uint8_t> &data) {
    View view = {data.data(), data.size(), 99, 99};
    assert(!validate(data.data(), data.size(), &view));
    assert(!view.data && !view.size && !view.frameCount && !view.durationMs);
}
struct CallbackState { uint16_t color; unsigned rows; bool cancel; };
static bool capture(uint16_t y, const uint16_t *pixels, size_t width, void *context) {
    CallbackState *state = static_cast<CallbackState *>(context);
    assert(y == state->rows && width == 480);
    for (size_t x = 0; x < width; ++x) assert(pixels[x] == state->color);
    ++state->rows;
    return !state->cancel;
}
int main() {
    std::vector<uint8_t> valid = asset();
    View view = {};
    assert(validate(valid.data(), valid.size(), &view));
    assert(validate(valid.data(), valid.size(), nullptr));
    assert(view.frameCount == 2 && view.durationMs == 200);
    assert(frameForElapsed(view, 0) == 0 && frameForElapsed(view, 99) == 0);
    assert(frameForElapsed(view, 100) == 1 && frameForElapsed(view, 199) == 1);
    assert(frameForElapsed(view, 200) == 0);
    assert(frameForElapsed(view, UINT32_MAX) == (UINT32_MAX % 200 >= 100 ? 1 : 0));
    uint16_t row[480];
    CallbackState state = {0x1235, 0, false};
    assert(decodeFrameRows(view, 1, row, 480, capture, &state));
    assert(state.rows == 480);
    assert(!decodeFrameRows(view, 2, row, 480, capture, &state));
    assert(!decodeFrameRows(view, 0, row, 479, capture, &state));
    assert(!decodeFrameRows(view, 0, nullptr, 480, capture, &state));
    assert(!decodeFrameRows(view, 0, row, 480, nullptr, &state));
    state = CallbackState{0x1234, 0, true};
    assert(!decodeFrameRows(view, 0, row, 480, capture, &state) && state.rows == 1);
    assert(!validate(nullptr, valid.size(), &view));
    assert(!validate(valid.data(), 31, &view));
    // Mutate every required header field.
    const size_t fields[] = {0,4,6,8,10,12,14,16,20,24,28};
    for (size_t field : fields) {
        std::vector<uint8_t> bad = valid; bad[field] ^= 1; reject(bad);
    }
    std::vector<uint8_t> bad = valid;
    put16(bad, 12, 0); reject(bad);
    bad = valid; put16(bad, 12, 251); reject(bad);
    bad = valid; put32(bad, 20, 0); reject(bad);
    bad = valid; put32(bad, 20, 25001); reject(bad);
    bad = valid; put32(bad, 40, 0); reject(bad);
    bad = valid; put32(bad, 40, UINT32_MAX); reject(bad);
    bad = valid; put32(bad, 40, 25000); put32(bad, 52, 1); reject(bad);
    bad = valid; put32(bad, 32, 55); reject(bad); // Into table.
    bad = valid; put32(bad, 32, 57); reject(bad); // Gap.
    bad = valid; put32(bad, 44, 56); reject(bad); // Overlap.
    bad = valid; put32(bad, 36, UINT32_MAX); reject(bad); // Length overflow.
    bad = valid; put32(bad, 44, UINT32_MAX); reject(bad); // Offset overflow.
    bad = valid; put32(bad, 36, 1919); reject(bad); // Incomplete last run.
    bad = valid; put16(bad, 56, 0); reject(bad);
    bad = valid; put16(bad, 56, 481); reject(bad);
    bad = valid; put16(bad, 56, 479); reject(bad); // Next row run crosses edge.
    bad = valid; bad.pop_back(); put32(bad,16,static_cast<uint32_t>(bad.size())); reject(bad);
    bad = valid; bad.push_back(0); put32(bad,16,static_cast<uint32_t>(bad.size())); reject(bad);
    // 250 frames and 25000 ms are valid inclusive boundaries.
    std::vector<uint8_t> maximum = asset(250);
    assert(validate(maximum.data(), maximum.size(), &view));
    // Split first row into two legal consecutive runs: no runCount row header.
    std::vector<uint8_t> split = asset(1);
    split.insert(split.begin() + 48, 4, 0);
    put32(split,16,static_cast<uint32_t>(split.size())); put32(split,36,1924);
    put16(split,44,100); put16(split,48,380); put16(split,50,0x1234);
    assert(validate(split.data(), split.size(), &view));
    state = CallbackState{0x1234, 0, false};
    assert(decodeFrameRows(view,0,row,480,capture,&state));
    puts("EXA1 parser/decode tests: OK");
}
