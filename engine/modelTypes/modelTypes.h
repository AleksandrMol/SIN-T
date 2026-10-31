#pragma once

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "../moduleDescriptor/moduleDescriptor.h"

// Постоянный ID модуля. Уникален во всей модели и не используется повторно
using ModuleId = uint32_t;

// Макросы есть в любой модели. Обычные ID начинаются с 1, поэтому этот не пересекается со старыми файлами (D-055)
constexpr ModuleId MACROS_MODULE_ID = 0;

/**
 * Положение блока на холсте, в точках холста (D-065). На звук не влияет
 */
struct ModulePosition {
  float x;
  float y;

  bool operator==(const ModulePosition&) const = default;
};

/**
 * Экземпляр модуля в модели: только данные, без DSP
 */
struct ModuleInstance {
  ModuleId id;
  const ModuleDescriptor* descriptor;
  std::optional<ModuleId> owner; // Генератор, в подграфе которого лежит модуль. Пусто — главный граф
  std::map<std::string, float> parameters; // Базовые значения в реальных единицах, как в пресете (D-025)
  bool enabled = true; // Отключённый модуль пропускает сумму входов на выходы (D-040)
  std::optional<ModulePosition> position{}; // Пусто — блок ставит авторасстановка (D-065)

  // Заглушка модуля неизвестного типа (D-021). Описание восстановлено из соединений и маршрутов
  // пресета, исходные данные модуля хранятся как есть и записываются обратно при сохранении
  std::shared_ptr<const ModuleDescriptor> stubDescriptor{};
  std::string stubData{};

  bool isStub() const {
    return this->stubDescriptor != nullptr;
  }

  bool operator==(const ModuleInstance&) const = default;
};

/**
 * Аудиосоединение. Соединяет модули только одного графа
 */
struct Connection {
  ModuleId fromModule;
  std::string fromPort;
  ModuleId toModule;
  std::string toPort;

  auto operator<=>(const Connection&) const = default;
};

struct ModSource {
  ModuleId module;
  std::string output;

  auto operator<=>(const ModSource&) const = default;
};

struct ModTarget {
  ModuleId module;
  std::string parameter;

  auto operator<=>(const ModTarget&) const = default;
};

/**
 * Маршрут модуляции. Глубина -1..1, вклад в параметр — глубина × значение источника (D-025)
 */
struct ModRoute {
  ModSource source;
  ModTarget target;
  float depth;

  bool operator==(const ModRoute&) const = default;
};

// Маршрут определяется парой источник и цель: повторный такой же модель не допускает
using RouteKey = std::pair<ModSource, ModTarget>;

// Слот хоста без параметра
constexpr uint32_t NO_PARAMETER = UINT32_MAX;

/**
 * Куда ведёт слот хоста (D-011). Индекс параметра — в порядке описания модуля
 */
struct SlotTarget {
  ModuleId module = 0;
  uint32_t parameter = NO_PARAMETER;

  bool operator==(const SlotTarget&) const = default;
};
