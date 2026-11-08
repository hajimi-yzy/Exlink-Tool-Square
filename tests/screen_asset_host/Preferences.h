#pragma once
#include <stddef.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
extern std::map<std::string, std::vector<unsigned char> > fakeNvs;
extern std::string failNvsKey;
class Preferences {
public:
    bool begin(const char *, bool) { return true; }
    void end() {}
    size_t getBytesLength(const char *key) { return fakeNvs[key].size(); }
    size_t getBytes(const char *key, void *output, size_t size) {
        if (fakeNvs[key].size() != size) return 0;
        memcpy(output, fakeNvs[key].data(), size); return size;
    }
    size_t putBytes(const char *key, const void *input, size_t size) {
        if (failNvsKey == key) return 0;
        const unsigned char *bytes = static_cast<const unsigned char *>(input);
        fakeNvs[key] = std::vector<unsigned char>(bytes, bytes + size); return size;
    }
};
