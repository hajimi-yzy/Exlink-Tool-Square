#pragma once
#include "FreeRTOS.h"

struct HostQueue {
  size_t capacity, item_size;
  std::deque<std::vector<uint8_t>> values;
};
using QueueHandle_t = HostQueue *;
inline QueueHandle_t xQueueCreate(unsigned capacity, size_t item_size) {
  return new HostQueue{capacity, item_size, {}};
}
inline void vQueueDelete(QueueHandle_t queue) { delete queue; }
inline BaseType_t xQueueSend(QueueHandle_t queue, const void *data, TickType_t) {
  if (queue->values.size() >= queue->capacity) return pdFALSE;
  const auto *bytes = static_cast<const uint8_t *>(data);
  queue->values.emplace_back(bytes, bytes + queue->item_size);
  return pdTRUE;
}
inline BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t) {
  if (queue->values.empty()) return pdFALSE;
  std::memcpy(data, queue->values.front().data(), queue->item_size);
  queue->values.pop_front();
  return pdTRUE;
}
inline void xQueueReset(QueueHandle_t queue) { queue->values.clear(); }
inline unsigned uxQueueMessagesWaiting(QueueHandle_t queue) {
  return static_cast<unsigned>(queue->values.size());
}
