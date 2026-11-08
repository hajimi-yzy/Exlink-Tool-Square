#include "bridge_text_format.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main() {
  uint8_t bytes[256];
  for (unsigned i = 0; i < 256; ++i) bytes[i] = i;
  struct { char output[320]; unsigned char guard; } buffer{};
  buffer.guard = 0xA5;
  size_t consumed = 0;
  while (consumed < sizeof(bytes)) {
    auto chunk = format_bridge_text(buffer.output, sizeof(buffer.output), "RX <- ", bytes + consumed, sizeof(bytes) - consumed);
    assert(chunk.consumed > 0 && chunk.consumed <= 64);
    assert(chunk.length < sizeof(buffer.output));
    assert(strlen(buffer.output) == chunk.length && buffer.guard == 0xA5);
    for (size_t i = 0; i < chunk.length; ++i)
      assert(buffer.output[i] == '\n' || (buffer.output[i] >= 32 && buffer.output[i] <= 126));
    consumed += chunk.consumed;
  }
  uint8_t sample[] = {0x00, '\r', '\n', 0xff, 'A'};
  auto chunk = format_bridge_text(buffer.output, sizeof(buffer.output), "TX ", sample, sizeof(sample));
  assert(chunk.consumed == sizeof(sample));
  assert(strcmp(buffer.output, "TX \\x00\\r\\n\\xFFA\n") == 0);
  // Constrained outputs never split an escape sequence or overwrite guards.
  for (size_t capacity = 3; capacity < 20; ++capacity) {
    memset(buffer.output, '!', sizeof(buffer.output));
    chunk = format_bridge_text(buffer.output, capacity, "", sample, sizeof(sample));
    assert(chunk.length < capacity && buffer.output[chunk.length] == '\0');
    assert(buffer.output[capacity] == '!' && buffer.guard == 0xA5);
  }
  puts("Serial text formatting: all 256 byte values, chunk bounds, escape boundaries passed");
}
