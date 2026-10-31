#pragma once

#include <expected>
#include <optional>
#include <string>
#include <variant>

#include "../modelTypes/modelTypes.h"

/**
 * Команды — единственный способ изменить модель (D-030). Это только данные, как action в Redux:
 * их можно записать, передать между потоками и повторить, на этом будет построен undo/redo
 */
struct AddModule {
  std::string type;
  std::optional<ModuleId> owner = std::nullopt; // Генератор, в подграф которого добавить модуль
};

struct RemoveModule {
  ModuleId id;
};

struct Connect {
  Connection connection;
};

struct Disconnect {
  Connection connection;
};

struct SetParameter {
  ModuleId module;
  std::string parameter;
  float value; // В реальных единицах. Значение вне диапазона ограничивается
};

struct SetModuleEnabled {
  ModuleId module;
  bool enabled;
};

struct AddRoute {
  ModRoute route;
};

struct RemoveRoute {
  ModSource source;
  ModTarget target;
};

struct SetRouteDepth {
  ModSource source;
  ModTarget target;
  float depth;
};

using Command = std::variant<
  AddModule,
  RemoveModule,
  Connect,
  Disconnect,
  SetParameter,
  SetModuleEnabled,
  AddRoute,
  RemoveRoute,
  SetRouteDepth
>;

// Успех: ID созданного модуля, если команда его создала. Ошибка: текст причины
using CommandResult = std::expected<std::optional<ModuleId>, std::string>;
