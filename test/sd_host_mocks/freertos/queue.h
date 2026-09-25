#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
#include <condition_variable>
#include <chrono>
#include <cstring>
struct SdTestQueue {
  unsigned capacity, itemSize;
  std::mutex lock;
  std::condition_variable changed;
  std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = SdTestQueue*;
inline QueueHandle_t xQueueCreate(unsigned capacity, unsigned itemSize) {
  auto* queue = new SdTestQueue;
  queue->capacity = capacity; queue->itemSize = itemSize;
  return queue;
}
inline void vQueueDelete(QueueHandle_t queue) { delete queue; }
inline int xQueueSend(QueueHandle_t queue, const void* item, uint32_t timeout) {
  std::unique_lock<std::mutex> lock(queue->lock);
  if (!queue->changed.wait_for(lock, std::chrono::milliseconds(timeout), [&] {
      return queue->items.size() < queue->capacity;
    })) return 0;
  const auto* bytes = static_cast<const uint8_t*>(item);
  queue->items.emplace_back(bytes, bytes + queue->itemSize);
  queue->changed.notify_all();
  return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void* item, uint32_t timeout) {
  std::unique_lock<std::mutex> lock(queue->lock);
  if (!queue->changed.wait_for(lock, std::chrono::milliseconds(timeout), [&] {
      return !queue->items.empty();
    })) return 0;
  memcpy(item, queue->items.front().data(), queue->itemSize);
  queue->items.pop_front(); queue->changed.notify_all();
  return pdTRUE;
}
inline unsigned uxQueueMessagesWaiting(QueueHandle_t queue) {
  std::lock_guard<std::mutex> lock(queue->lock);
  return queue->items.size();
}
