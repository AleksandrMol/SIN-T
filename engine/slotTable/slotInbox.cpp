#include "./slotInbox.h"

void SlotInbox::post(uint32_t slot, float value) {
  this->values[slot].store(value, std::memory_order_relaxed);
  this->flags[slot].store(true, std::memory_order_release);
  this->hasValues.store(true, std::memory_order_release);
  this->isRequested.store(true, std::memory_order_relaxed);
}

bool SlotInbox::takeRequest() {
  return this->isRequested.exchange(false, std::memory_order_relaxed);
}

void SlotInbox::drain(const std::function<void(uint32_t slot, float value)>& receive) {
  // Общий флаг снимается первым: значение, пришедшее во время прохода, поднимет его снова и не потеряется
  if (!this->hasValues.exchange(false, std::memory_order_acquire)) {
    return;
  }

  for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
    if (this->flags[slot].exchange(false, std::memory_order_acquire)) {
      receive(slot, this->values[slot].load(std::memory_order_relaxed));
    }
  }
}
