#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "../moduleDescriptor/moduleDescriptor.h"

/**
 * Реестр типов модулей. Через него модель узнаёт, какие модули существуют (D-033).
 * Используется только на главном потоке
 */
class ModuleRegistry {
  public:
    /**
     * Добавляет тип. Описание не копируется и должно жить дольше реестра.
     * Отклоняет некорректное описание и повторяющийся тип
     */
    std::expected<void, std::string> add(const ModuleDescriptor* descriptor);

    /**
     * Возвращает описание типа или nullptr, если такого типа нет
     */
    const ModuleDescriptor* find(std::string_view type) const;

    /**
     * Все типы в порядке добавления
     */
    const std::vector<const ModuleDescriptor*>& getAll() const;

  private:
    std::vector<const ModuleDescriptor*> descriptors;
};
