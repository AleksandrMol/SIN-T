#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "./slotTable.h"

enum class OUTBOX_EVENT {
  GESTURE_BEGIN,
  VALUE,
  GESTURE_END,
};

/**
 * Значения слотов по пути от редактора к хосту (D-067). Редактор живёт на главном потоке, а хост принимает
 * события только в process и flush, то есть обычно в аудиопотоке. Устроено как SlotInbox в обратную
 * сторону: важно только последнее значение слота, поэтому очереди и её переполнения нет
 */
class SlotOutbox {
  public:
    /**
     * Начало и конец движения ручки, значение между ними. Вызываются на главном потоке
     */
    void beginGesture(uint32_t slot);
    void post(uint32_t slot, float value);
    void endGesture(uint32_t slot);

    /**
     * Отдаёт всё накопленное, внутри слота в порядке «начало, значение, конец». Без выделения памяти.
     * Вызывается из process или flush, поэтому параллельно с собой не работает
     */
    template <typename Receive>
    void drain(Receive&& receive) {
      if (!this->hasEvents.exchange(false, std::memory_order_acquire)) {
        return;
      }

      for (uint32_t slot = 0; slot < SLOT_COUNT; slot++) {
        const uint8_t events = this->flags[slot].exchange(0, std::memory_order_acquire);

        if (events & FLAG_BEGIN) {
          receive(slot, OUTBOX_EVENT::GESTURE_BEGIN, 0.0f);
        }

        if (events & FLAG_VALUE) {
          receive(slot, OUTBOX_EVENT::VALUE, this->values[slot].load(std::memory_order_relaxed));
        }

        if (events & FLAG_END) {
          receive(slot, OUTBOX_EVENT::GESTURE_END, 0.0f);
        }
      }
    }

  private:
    static constexpr uint8_t FLAG_BEGIN = 1;
    static constexpr uint8_t FLAG_VALUE = 2;
    static constexpr uint8_t FLAG_END = 4;

    std::array<std::atomic<float>, SLOT_COUNT> values{};
    std::array<std::atomic<uint8_t>, SLOT_COUNT> flags{};
    std::atomic<bool> hasEvents = false;

    void raise(uint32_t slot, uint8_t flag);
};
