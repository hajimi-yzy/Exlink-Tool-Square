#pragma once
#include "FreeRTOS.h"

enum eNotifyAction { eSetBits };
inline BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *,
    unsigned, void *context, unsigned, TaskHandle_t *task, int) {
  host::runtime.entry = entry; host::runtime.context = context;
  *task = reinterpret_cast<void *>(1);
  return pdPASS;
}
inline BaseType_t xTaskNotify(TaskHandle_t, uint32_t bits, eNotifyAction) {
  host::runtime.notifications |= bits; return pdTRUE;
}
inline BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t bits,
    eNotifyAction action, BaseType_t *woken) {
  *woken = pdFALSE; return xTaskNotify(task, bits, action);
}
inline BaseType_t xTaskNotifyWait(uint32_t, uint32_t clear,
    uint32_t *notification, TickType_t wait) {
  host::advance(wait);
  if (host::runtime.tick_hook) host::runtime.tick_hook();
  if (host::runtime.now >= host::runtime.deadline) throw host::StopWorker{};
  *notification = host::runtime.notifications;
  host::runtime.notifications &= ~clear;
  return pdTRUE;
}
inline void vTaskDelay(TickType_t wait) { host::advance(wait); }
