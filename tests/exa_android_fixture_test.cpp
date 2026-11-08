// Consume fixtures emitted by the actual Kotlin AssetFormat tests with the
// production firmware decoder. Run Android unit tests before this test.
#include "exa_format.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>

struct Expected {
    uint16_t color;
    bool checker;
    unsigned rows;
};

static bool checkRow(uint16_t y, const uint16_t *pixels, size_t width, void *context) {
    Expected &expected = *static_cast<Expected *>(context);
    assert(width == 480 && y == expected.rows++);
    for (size_t x = 0; x < width; ++x) {
        uint16_t color = expected.color;
        if (expected.checker && (x || y)) color = (x + y) % 2 ? 0x07E0 : 0xF800;
        assert(pixels[x] == color);
    }
    return true;
}

static std::vector<uint8_t> load(const std::string &path) {
    FILE *file = fopen(path.c_str(), "rb");
    assert(file && "Run Android AssetFormatTest to create fixtures first");
    assert(fseek(file, 0, SEEK_END) == 0);
    const long length = ftell(file);
    assert(length > 0 && length <= 0x380000);
    rewind(file);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    assert(fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    assert(fclose(file) == 0);
    return bytes;
}

static void check(const std::string &path, uint32_t duration,
                  const std::vector<uint32_t> &durations,
                  const std::vector<uint16_t> &colors, bool checker = false) {
    const std::vector<uint8_t> bytes = load(path);
    exlink::exa::View view = {};
    assert(exlink::exa::validate(bytes.data(), bytes.size(), &view));
    assert(view.durationMs == duration && view.frameCount == colors.size());
    uint16_t row[480];
    uint32_t time = 0;
    for (uint16_t frame = 0; frame < view.frameCount; ++frame) {
        assert(exlink::exa::frameForElapsed(view, time) == frame);
        assert(exlink::exa::frameForElapsed(view, time + durations[frame] - 1) == frame);
        Expected expected = {colors[frame], checker, 0};
        assert(exlink::exa::decodeFrameRows(view, frame, row, 480, checkRow, &expected));
        assert(expected.rows == 480);
        time += durations[frame];
    }
    assert(time == duration && exlink::exa::frameForElapsed(view, time) == 0);
}

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : "android/app/build/exa-fixture";
    check(root + "/rgb.exa", 501, {100, 250, 151}, {0xF800, 0x07E0, 0x001F});
    check(root + "/repeated.exa", 25000, {25000}, {0x11AA});
    check(root + "/adapted.exa", 800, {400, 400}, {0xF800, 0xF890}, true);
    puts("Kotlin encoder -> firmware parser/pixels/timing: OK");
}
