#include "./slotOutbox.h"

void SlotOutbox::raise(uint32_t slot, uint8_t flag) {
  this->flags[slot].fetch_or(flag, std::memory_order_release);
  this->hasEvents.store(true, std::memory_order_release);
}

void SlotOutbox::beginGesture(uint32_t slot) {
  uint8_t events = this->flags[slot].load(std::memory_order_relaxed);
  uint8_t next;

  // Конец прошлого движения хост ещё не видел. Для него это одно движение: иначе порядок
  // «начало, значение, конец» закрыл бы новое движение раньше времени
  do {
    next = events & FLAG_END ? events & ~FLAG_END : events | FLAG_BEGIN;
  } while (!this->flags[slot].compare_exchange_weak(events, next, std::memory_order_release, std::memory_order_relaxed));

  this->hasEvents.store(true, std::memory_order_release);
}

void SlotOutbox::post(uint32_t slot, float value) {
  this->values[slot].store(value, std::memory_order_relaxed);
  this->raise(slot, FLAG_VALUE);
}

void SlotOutbox::endGesture(uint32_t slot) {
  this->raise(slot, FLAG_END);
}
