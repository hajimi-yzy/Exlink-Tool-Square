#ifndef APP_LOG_H
#define APP_LOG_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
  APP_LOG_CHANNEL_NONE = 0,
  APP_LOG_CHANNEL_UART0 = 1,
  APP_LOG_CHANNEL_USB = 2,
} app_log_channel_t;

typedef enum
{
  APP_LOG_LEVEL_ERROR = 0,
  APP_LOG_LEVEL_WARN = 1,
  APP_LOG_LEVEL_INFO = 2,
  APP_LOG_LEVEL_DEBUG = 3,
} app_log_level_t;

typedef struct
{
  bool enabled;
  app_log_channel_t channel;
  app_log_level_t level;
} app_log_state_t;

typedef void (*app_log_prepare_usb_fn_t)(void);

void app_log_init(app_log_prepare_usb_fn_t prepare_usb_fn);
void app_log_install_esp_sink(void);

void app_log_set_enabled(bool enabled);
bool app_log_is_enabled(void);
void app_log_suspend(void);
void app_log_resume(void);
void app_log_resume_all(void);
bool app_log_is_suspended(void);

void app_log_set_channel(app_log_channel_t channel);
app_log_channel_t app_log_get_channel(void);

void app_log_set_level(app_log_level_t level);
app_log_level_t app_log_get_level(void);
bool app_log_should_log(app_log_level_t level);
app_log_state_t app_log_get_effective_state(void);

void app_log_push_override(app_log_state_t state);
void app_log_pop_override(void);
void app_log_clear_overrides(void);

int app_log_log(app_log_level_t level, const char *tag, const char *fmt, ...);
int app_log_vlog(app_log_level_t level, const char *tag, const char *fmt, va_list args);
int app_log_printf(const char *fmt, ...);
int app_log_vprintf(const char *fmt, va_list args);
// Capture bridge bytes for the phone independently of suspended USB/UART
// diagnostics. Non-ASCII/control bytes are escaped as ASCII \\xNN.
void app_log_capture_bridge(const char *prefix, const char *text, size_t length);

#define APP_LOGE(tag, fmt, ...) app_log_log(APP_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define APP_LOGW(tag, fmt, ...) app_log_log(APP_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define APP_LOGI(tag, fmt, ...) app_log_log(APP_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define APP_LOGD(tag, fmt, ...) app_log_log(APP_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
