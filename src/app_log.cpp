#include "app_log.h"

#include <Arduino.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#include "HWCDC.h"
#endif

namespace
{
constexpr size_t APP_LOG_BUFFER_SIZE = 512;
constexpr size_t APP_LOG_OVERRIDE_DEPTH = 8;
constexpr size_t APP_LOG_CAPTURE_SIZE = 3072;
char g_capture[APP_LOG_CAPTURE_SIZE];
size_t g_capture_head = 0, g_capture_count = 0;
portMUX_TYPE g_capture_mux = portMUX_INITIALIZER_UNLOCKED;

void capture_append(const char *text, size_t length)
{
  // Callers pass at most 64 bytes per lock: no formatting/IO/allocation while
  // locked. A full ring drops oldest bytes, never grows or blocks the producer.
  portENTER_CRITICAL(&g_capture_mux);
  for (size_t i = 0; i < length; ++i) {
    g_capture[g_capture_head] = text[i];
    g_capture_head = (g_capture_head + 1) % APP_LOG_CAPTURE_SIZE;
    if (g_capture_count < APP_LOG_CAPTURE_SIZE) ++g_capture_count;
  }
  portEXIT_CRITICAL(&g_capture_mux);
}

void capture_sanitized(const char *text, size_t length)
{
  if (!text) return;
  char batch[64];
  size_t used = 0;
  const char hex[] = "0123456789ABCDEF";
  const size_t limit = length < 4096 ? length : 4096;
  for (size_t i = 0; i < limit; ++i) {
    uint8_t byte = static_cast<uint8_t>(text[i]);
    bool printable = (byte >= 32 && byte <= 126) || byte == '\n' || byte == '\r' || byte == '\t';
    size_t required = printable ? 1 : 4;
    if (used + required > sizeof(batch)) { capture_append(batch, used); used = 0; }
    if (printable) batch[used++] = char(byte);
    else {
      batch[used++] = '\\'; batch[used++] = 'x';
      batch[used++] = hex[byte >> 4]; batch[used++] = hex[byte & 15];
    }
  }
  if (used) capture_append(batch, used);
  if (length > limit) capture_append("[truncated]\n", 12);
}

app_log_prepare_usb_fn_t g_prepare_usb_fn = nullptr;
bool g_log_enabled = true;
app_log_channel_t g_log_channel = APP_LOG_CHANNEL_UART0;
app_log_level_t g_log_level = APP_LOG_LEVEL_INFO;
app_log_state_t g_log_override_stack[APP_LOG_OVERRIDE_DEPTH] = {};
size_t g_log_override_depth = 0;
bool g_esp_sink_installed = false;
vprintf_like_t g_previous_vprintf = nullptr;

const char *app_log_level_prefix(app_log_level_t level)
{
  switch (level)
  {
  case APP_LOG_LEVEL_ERROR:
    return "E";
  case APP_LOG_LEVEL_WARN:
    return "W";
  case APP_LOG_LEVEL_INFO:
    return "I";
  case APP_LOG_LEVEL_DEBUG:
    return "D";
  default:
    return "?";
  }
}

app_log_state_t app_log_get_resolved_state()
{
  if (g_log_override_depth > 0)
  {
    return g_log_override_stack[g_log_override_depth - 1U];
  }

  app_log_state_t state = {};
  state.enabled = g_log_enabled;
  state.channel = g_log_channel;
  state.level = g_log_level;
  return state;
}

void app_log_apply_debug_route()
{
  app_log_state_t effective = app_log_get_resolved_state();
#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  if (effective.channel == APP_LOG_CHANNEL_USB && g_prepare_usb_fn != nullptr)
  {
    g_prepare_usb_fn();
  }

  if (!effective.enabled)
  {
    Serial.setDebugOutput(false);
    USBSerial.setDebugOutput(false);
    return;
  }

  if (effective.channel == APP_LOG_CHANNEL_USB)
  {
    Serial.setDebugOutput(false);
    USBSerial.setDebugOutput(true);
    return;
  }

  if (effective.channel == APP_LOG_CHANNEL_UART0)
  {
    USBSerial.setDebugOutput(false);
    Serial.setDebugOutput(true);
    return;
  }

  Serial.setDebugOutput(false);
  USBSerial.setDebugOutput(false);
#else
  Serial.setDebugOutput(effective.enabled && effective.channel == APP_LOG_CHANNEL_UART0);
#endif
}

int app_log_write_text(const char *text, size_t length)
{
  capture_sanitized(text, length);
  app_log_state_t effective = app_log_get_resolved_state();
  if (!effective.enabled || text == nullptr || length == 0)
  {
    return (int)length;
  }

  if (effective.channel == APP_LOG_CHANNEL_UART0)
  {
    Serial.write((const uint8_t *)text, length);
    return (int)length;
  }

#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
  if (effective.channel == APP_LOG_CHANNEL_USB)
  {
    if (g_prepare_usb_fn != nullptr)
    {
      g_prepare_usb_fn();
    }
    USBSerial.write((const uint8_t *)text, length);
    return (int)length;
  }
#endif

  return (int)length;
}

int app_log_filtered_vprintf(const char *fmt, va_list args)
{
  va_list copy;
  va_copy(copy, args);

  char rendered[APP_LOG_BUFFER_SIZE];
  int rendered_len = vsnprintf(rendered, sizeof(rendered), fmt, copy);
  va_end(copy);

  if (rendered_len > 0 &&
      strstr(rendered, "BT_BTM") != nullptr &&
      strstr(rendered, "BTM_GetSecurityFlags false") != nullptr)
  {
    return rendered_len;
  }

  if (rendered_len <= 0)
  {
    return rendered_len;
  }

  size_t output_length = (size_t)rendered_len;
  if (output_length >= sizeof(rendered))
  {
    output_length = sizeof(rendered) - 1U;
  }

  app_log_write_text(rendered, output_length);
  return rendered_len;
}
} // namespace

extern "C" void app_log_init(app_log_prepare_usb_fn_t prepare_usb_fn)
{
  g_prepare_usb_fn = prepare_usb_fn;
  app_log_apply_debug_route();
}

extern "C" void app_log_install_esp_sink(void)
{
  if (g_esp_sink_installed)
  {
    return;
  }

  g_previous_vprintf = esp_log_set_vprintf(app_log_filtered_vprintf);
  (void)g_previous_vprintf;
  g_esp_sink_installed = true;
}

extern "C" void app_log_set_enabled(bool enabled)
{
  g_log_enabled = enabled;
  app_log_apply_debug_route();
}

extern "C" bool app_log_is_enabled(void)
{
  return g_log_enabled;
}

extern "C" void app_log_suspend(void)
{
  app_log_state_t state = app_log_get_resolved_state();
  state.enabled = false;
  app_log_push_override(state);
}

extern "C" void app_log_resume(void)
{
  app_log_pop_override();
}

extern "C" void app_log_resume_all(void)
{
  app_log_clear_overrides();
}

extern "C" bool app_log_is_suspended(void)
{
  app_log_state_t effective = app_log_get_resolved_state();
  return !effective.enabled;
}

extern "C" void app_log_set_channel(app_log_channel_t channel)
{
  g_log_channel = channel;
  app_log_apply_debug_route();
}

extern "C" app_log_channel_t app_log_get_channel(void)
{
  return g_log_channel;
}

extern "C" void app_log_set_level(app_log_level_t level)
{
  g_log_level = level;
}

extern "C" app_log_level_t app_log_get_level(void)
{
  return g_log_level;
}

extern "C" bool app_log_should_log(app_log_level_t level)
{
  app_log_state_t effective = app_log_get_resolved_state();
  return effective.enabled && level <= effective.level;
}

extern "C" app_log_state_t app_log_get_effective_state(void)
{
  return app_log_get_resolved_state();
}

extern "C" void app_log_push_override(app_log_state_t state)
{
  if (g_log_override_depth < APP_LOG_OVERRIDE_DEPTH)
  {
    g_log_override_stack[g_log_override_depth++] = state;
  }
  else
  {
    g_log_override_stack[APP_LOG_OVERRIDE_DEPTH - 1U] = state;
  }
  app_log_apply_debug_route();
}

extern "C" void app_log_pop_override(void)
{
  if (g_log_override_depth > 0)
  {
    --g_log_override_depth;
  }
  app_log_apply_debug_route();
}

extern "C" void app_log_clear_overrides(void)
{
  g_log_override_depth = 0;
  app_log_apply_debug_route();
}

extern "C" int app_log_vlog(app_log_level_t level, const char *tag, const char *fmt, va_list args)
{
  // Suspend affects physical debug output, not the bounded phone diagnostic
  // stream; still honour the configured severity threshold.
  if (level > app_log_get_resolved_state().level)
  {
    return 0;
  }

  // Use one buffer: the former message + final buffers consumed over 1 KiB
  // before printf/USB stack usage, which is unsafe on small system tasks.
  char final_buffer[APP_LOG_BUFFER_SIZE + 64];
  int prefix_len = tag && tag[0] ?
      snprintf(final_buffer, 64, "[%s][%.48s] ", app_log_level_prefix(level), tag) :
      snprintf(final_buffer, 64, "[%s] ", app_log_level_prefix(level));
  if (prefix_len <= 0 || prefix_len >= 64) return 0;
  va_list copy;
  va_copy(copy, args);
  int message_len = vsnprintf(final_buffer + prefix_len, APP_LOG_BUFFER_SIZE, fmt, copy);
  va_end(copy);
  if (message_len <= 0)
  {
    return message_len;
  }

  int final_len = prefix_len + message_len;
  size_t output_length = prefix_len +
      (message_len < int(APP_LOG_BUFFER_SIZE) ? size_t(message_len) : APP_LOG_BUFFER_SIZE - 1);

  app_log_write_text(final_buffer, output_length);
  return final_len;
}

extern "C" int app_log_log(app_log_level_t level, const char *tag, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int result = app_log_vlog(level, tag, fmt, args);
  va_end(args);
  return result;
}

extern "C" int app_log_vprintf(const char *fmt, va_list args)
{
  va_list copy;
  va_copy(copy, args);
  int result = app_log_vlog(APP_LOG_LEVEL_INFO, nullptr, fmt, copy);
  va_end(copy);
  return result;
}

extern "C" int app_log_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int result = app_log_vprintf(fmt, args);
  va_end(args);
  return result;
}

extern "C" void app_log_capture_bridge(const char *prefix, const char *text, size_t length)
{
  if (!text || !length) return;
  capture_append("[bridge] ", 9);
  if (prefix) capture_sanitized(prefix, strnlen(prefix, 32));
  capture_sanitized(text, length);
  capture_append("\n", 1);
}

namespace exlink {
size_t updateReadLog(char *buffer, size_t capacity)
{
  if (!buffer || !capacity) return 0;
  const size_t maximum = capacity < 480 ? capacity : 480;
  size_t copied = 0;
  // Drain in short locks so BLE reads cannot stall producers for 480 copies.
  while (copied < maximum) {
    portENTER_CRITICAL(&g_capture_mux);
    size_t count = g_capture_count;
    if (count > 64) count = 64;
    if (count > maximum - copied) count = maximum - copied;
    size_t tail = (g_capture_head + APP_LOG_CAPTURE_SIZE - g_capture_count) % APP_LOG_CAPTURE_SIZE;
    for (size_t i = 0; i < count; ++i) buffer[copied + i] = g_capture[(tail + i) % APP_LOG_CAPTURE_SIZE];
    g_capture_count -= count;
    portEXIT_CRITICAL(&g_capture_mux);
    copied += count;
    if (!count) break;
  }
  return copied;
}
} // namespace exlink
