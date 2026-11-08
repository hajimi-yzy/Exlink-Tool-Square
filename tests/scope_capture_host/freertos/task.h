#pragma once
#include <stdint.h>
struct MockTask;
using TaskHandle_t = MockTask *;
using TickType_t = uint32_t;
enum { pdTRUE = 1, pdPASS = 1 };
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(milliseconds) (milliseconds)
int xTaskCreatePinnedToCore(void (*entry)(void *), const char *, unsigned, void *,
                          unsigned, TaskHandle_t *, unsigned);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(int clear, TickType_t wait);
void vTaskDelay(TickType_t ticks);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
