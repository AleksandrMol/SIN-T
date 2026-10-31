#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "../moduleDescriptor/moduleDescriptor.h"

// Постоянный ID модуля. Уникален во всей модели и не используется повторно
using ModuleId = uint32_t;

/**
 * Экземпляр модуля в модели: только данные, без DSP
 */
struct ModuleInstance {
  ModuleId id;
  const ModuleDescriptor* descriptor;
  std::optional<ModuleId> owner; // Генератор, в подграфе которого лежит модуль. Пусто — главный граф
  std::map<std::string, float> parameters; // Базовые значения в реальных единицах, как в пресете (D-025)
  bool enabled = true; // Отключённый модуль пропускает сумму входов на выходы (D-040)

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

  bool operator==(const Connection&) const = default;
};

struct ModSource {
  ModuleId module;
  std::string output;

  bool operator==(const ModSource&) const = default;
};

struct ModTarget {
  ModuleId module;
  std::string parameter;

  bool operator==(const ModTarget&) const = default;
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
