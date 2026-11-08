#pragma once
#include <stddef.h>
#include <stdint.h>

struct BridgeTextChunk { size_t consumed, length; };

// Arbitrary serial bytes are not UTF-8. Produce bounded ASCII for LVGL; keep
// the original bytes untouched on the actual bridge transport.
inline BridgeTextChunk format_bridge_text(char *output, size_t capacity,
    const char *prefix, const uint8_t *data, size_t length) {
  BridgeTextChunk result{0, 0};
  if (!output || capacity < 3 || !data) return result;
  const char hex[] = "0123456789ABCDEF";
  if (prefix) {
    for (size_t i = 0; prefix[i] && i < 32 && result.length + 2 < capacity; ++i)
      output[result.length++] = prefix[i];
  }
  while (result.consumed < length && result.consumed < 64) {
    uint8_t byte = data[result.consumed];
    size_t required = byte >= 32 && byte <= 126 ? 1 : (byte == '\r' || byte == '\n' ? 2 : 4);
    if (result.length + required + 2 > capacity) break;
    if (required == 1) output[result.length++] = char(byte);
    else {
      output[result.length++] = '\\';
      if (required == 2) output[result.length++] = byte == '\r' ? 'r' : 'n';
      else {
        output[result.length++] = 'x';
        output[result.length++] = hex[byte >> 4];
        output[result.length++] = hex[byte & 15];
      }
    }
    ++result.consumed;
  }
  output[result.length++] = '\n';
  output[result.length] = '\0';
  return result;
}
