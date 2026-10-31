#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>

#include "./slotTable.h"

/**
 * Значения слотов от хоста по пути к модели. Хост присылает их в аудиопотоке, а модель живёт на главном
 * (D-023). Важно только последнее значение каждого слота, поэтому очереди и её переполнения нет:
 * аудиопоток пишет в ячейку и поднимает флаг, главный поток забирает, когда дойдёт (D-056)
 */
class SlotInbox {
  public:
    /**
     * Записывает значение. Вызывается из аудиопотока, а вне работы звука с главного
     */
    void post(uint32_t slot, float value);

    /**
     * Нужен ли вызов главного потока: что-то пришло после прошлого вопроса. Вызывается из аудиопотока
     */
    bool takeRequest();

    /**
     * Отдаёт всё, что пришло с прошлого раза. Вызывается на главном потоке
     */
    void drain(const std::function<void(uint32_t slot, float value)>& receive);

  private:
    std::array<std::atomic<float>, SLOT_COUNT> values{};
    std::array<std::atomic<bool>, SLOT_COUNT> flags{};
    std::atomic<bool> hasValues = false;
    std::atomic<bool> isRequested = false;
};
