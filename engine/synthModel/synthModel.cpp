#include "./synthModel.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string_view>

#include "../builtinModules/builtinModules.h"

namespace {
  std::string moduleLabel(ModuleId id) {
    return "модуль " + std::to_string(id);
  }

  /**
   * Зона модуля для правил модуляции (D-004): генератор, к которому модуль относится.
   * Параметры самого генератора тоже считаются его параметрами. Пусто — глобальная зона
   */
  std::optional<ModuleId> getZone(const ModuleInstance& module) {
    if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
      return module.id;
    }

    return module.owner;
  }
}

SynthModel::SynthModel(const ModuleRegistry* registry) : registry(registry) {
  this->insertModule(MACROS_MODULE_ID, &MACROS_DESCRIPTOR, std::nullopt);
  this->outputId = this->createModule(&OUTPUT_DESCRIPTOR, std::nullopt);
}

CommandResult SynthModel::apply(const Command& command) {
  return std::visit([this](const auto& typedCommand) {
    return this->execute(typedCommand);
  }, command);
}

const std::map<ModuleId, ModuleInstance>& SynthModel::getModules() const {
  return this->modules;
}

const std::vector<Connection>& SynthModel::getConnections() const {
  return this->connections;
}

const std::vector<ModRoute>& SynthModel::getRoutes() const {
  return this->routes;
}

ModuleId SynthModel::getOutputId() const {
  return this->outputId;
}

ModuleId SynthModel::getNextId() const {
  return this->nextId;
}

const ModuleInstance* SynthModel::findModule(ModuleId id) const {
  const auto it = this->modules.find(id);

  return it == this->modules.end() ? nullptr : &it->second;
}

/**
 * Создаёт модуль с новым ID, без проверок
 */
ModuleId SynthModel::createModule(const ModuleDescriptor* descriptor, std::optional<ModuleId> owner) {
  const ModuleId id = this->nextId++;

  this->insertModule(id, descriptor, owner);

  return id;
}

/**
 * Добавляет модуль со значениями параметров по умолчанию, без проверок
 */
void SynthModel::insertModule(ModuleId id, const ModuleDescriptor* descriptor, std::optional<ModuleId> owner) {
  ModuleInstance module = { .id = id, .descriptor = descriptor, .owner = owner, .parameters = {} };

  for (const ParameterDescriptor& parameter : descriptor->parameters) {
    module.parameters[parameter.key] = parameter.defaultValue;
  }

  this->modules.emplace(id, std::move(module));
}

/**
 * Есть ли путь по аудиосоединениям от одного модуля к другому
 */
bool SynthModel::hasAudioPath(ModuleId from, ModuleId to) const {
  std::vector<ModuleId> pending = { from };
  std::set<ModuleId> visited;

  while (!pending.empty()) {
    const ModuleId current = pending.back();
    pending.pop_back();

    if (current == to) {
      return true;
    }

    if (!visited.insert(current).second) {
      continue;
    }

    for (const Connection& connection : this->connections) {
      if (connection.fromModule == current) {
        pending.push_back(connection.toModule);
      }
    }
  }

  return false;
}

CommandResult SynthModel::execute(const AddModule& command) {
  const ModuleDescriptor* descriptor = this->registry->find(command.type);

  if (!descriptor) {
    return std::unexpected("неизвестный тип модуля \"" + command.type + "\"");
  }

  if (descriptor->role == MODULE_ROLE::OUTPUT) {
    return std::unexpected("выход графа создаётся автоматически");
  }

  if (descriptor->role == MODULE_ROLE::MACROS) {
    return std::unexpected("макросы создаются автоматически");
  }

  if (command.owner) {
    const ModuleInstance* owner = this->findModule(*command.owner);

    if (!owner || owner->descriptor->role != MODULE_ROLE::GENERATOR) {
      return std::unexpected(moduleLabel(*command.owner) + " не является генератором");
    }

    if (!descriptor->isAllowedInPoly) {
      return std::unexpected("тип \"" + command.type + "\" недопустим в полифонической зоне");
    }
  }

  const ModuleId id = this->createModule(descriptor, command.owner);

  if (descriptor->role == MODULE_ROLE::GENERATOR) {
    this->createModule(&VOICE_OUTPUT_DESCRIPTOR, id);
  }

  return id;
}

CommandResult SynthModel::execute(const RemoveModule& command) {
  const ModuleInstance* module = this->findModule(command.id);

  if (!module) {
    return std::unexpected(moduleLabel(command.id) + " не существует");
  }

  if (module->descriptor->role == MODULE_ROLE::OUTPUT) {
    return std::unexpected("выход графа нельзя удалить");
  }

  if (module->descriptor->role == MODULE_ROLE::MACROS) {
    return std::unexpected("макросы нельзя удалить");
  }

  // Вместе с генератором удаляется весь его подграф
  std::set<ModuleId> removed = { command.id };

  for (const auto& [id, candidate] : this->modules) {
    if (candidate.owner == command.id) {
      removed.insert(id);
    }
  }

  std::erase_if(this->connections, [&removed](const Connection& connection) {
    return removed.contains(connection.fromModule) || removed.contains(connection.toModule);
  });

  std::erase_if(this->routes, [&removed](const ModRoute& route) {
    return removed.contains(route.source.module) || removed.contains(route.target.module);
  });

  for (const ModuleId id : removed) {
    this->modules.erase(id);
  }

  return std::nullopt;
}

std::expected<void, std::string> SynthModel::checkConnection(const Connection& connection) const {
  const ModuleInstance* from = this->findModule(connection.fromModule);
  const ModuleInstance* to = this->findModule(connection.toModule);

  if (!from) {
    return std::unexpected(moduleLabel(connection.fromModule) + " не существует");
  }

  if (!to) {
    return std::unexpected(moduleLabel(connection.toModule) + " не существует");
  }

  if (from->owner != to->owner) {
    return std::unexpected("соединение возможно только внутри одного графа");
  }

  if (!findByKey(from->descriptor->outputs, connection.fromPort)) {
    return std::unexpected(moduleLabel(from->id) + ": нет выхода \"" + connection.fromPort + "\"");
  }

  if (!findByKey(to->descriptor->inputs, connection.toPort)) {
    return std::unexpected(moduleLabel(to->id) + ": нет входа \"" + connection.toPort + "\"");
  }

  if (std::ranges::find(this->connections, connection) != this->connections.end()) {
    return std::unexpected("такое соединение уже есть");
  }

  if (this->hasAudioPath(connection.toModule, connection.fromModule)) {
    return std::unexpected("соединение создаёт цикл");
  }

  return {};
}

CommandResult SynthModel::execute(const Connect& command) {
  if (const auto result = this->checkConnection(command.connection); !result) {
    return std::unexpected(result.error());
  }

  this->connections.push_back(command.connection);

  return std::nullopt;
}

CommandResult SynthModel::execute(const Disconnect& command) {
  const auto it = std::ranges::find(this->connections, command.connection);

  if (it == this->connections.end()) {
    return std::unexpected("такого соединения нет");
  }

  this->connections.erase(it);

  return std::nullopt;
}

CommandResult SynthModel::execute(const SetParameter& command) {
  const auto module = this->modules.find(command.module);

  if (module == this->modules.end()) {
    return std::unexpected(moduleLabel(command.module) + " не существует");
  }

  if (module->second.isStub()) {
    return std::unexpected(moduleLabel(command.module) + ": параметры неизвестного модуля не меняются");
  }

  const ParameterDescriptor* parameter = findByKey(module->second.descriptor->parameters, command.parameter);

  if (!parameter) {
    return std::unexpected(moduleLabel(command.module) + ": нет параметра \"" + command.parameter + "\"");
  }

  if (!std::isfinite(command.value)) {
    return std::unexpected("значение параметра не является числом");
  }

  module->second.parameters[command.parameter] = limitReal(*parameter, command.value);

  return std::nullopt;
}

CommandResult SynthModel::execute(const SetModuleEnabled& command) {
  const auto module = this->modules.find(command.module);

  if (module == this->modules.end()) {
    return std::unexpected(moduleLabel(command.module) + " не существует");
  }

  // Заглушка и так пропускает сигнал, а её исходные данные меняться не должны (D-021)
  if (module->second.isStub()) {
    return std::unexpected(moduleLabel(command.module) + ": неизвестный модуль нельзя отключить");
  }

  if (module->second.descriptor->role == MODULE_ROLE::OUTPUT && !command.enabled) {
    return std::unexpected("выход графа нельзя отключить");
  }

  if (module->second.descriptor->role == MODULE_ROLE::MACROS && !command.enabled) {
    return std::unexpected("макросы нельзя отключить");
  }

  module->second.enabled = command.enabled;

  return std::nullopt;
}

CommandResult SynthModel::execute(const SetModulePosition& command) {
  const auto module = this->modules.find(command.module);

  if (module == this->modules.end()) {
    return std::unexpected(moduleLabel(command.module) + " не существует");
  }

  // Макросы живут в своей панели, а не на холсте
  if (module->second.descriptor->role == MODULE_ROLE::MACROS) {
    return std::unexpected("у макросов нет положения на холсте");
  }

  if (!std::isfinite(command.position.x) || !std::isfinite(command.position.y)) {
    return std::unexpected("положение модуля не является числом");
  }

  module->second.position = command.position;

  return std::nullopt;
}

/**
 * Проверяет концы маршрута, правила модуляции между зонами (D-004) и то, что такого маршрута ещё нет
 */
std::expected<void, std::string> SynthModel::checkRoute(const ModSource& source, const ModTarget& target) const {
  const ModuleInstance* sourceModule = this->findModule(source.module);
  const ModuleInstance* targetModule = this->findModule(target.module);

  if (!sourceModule) {
    return std::unexpected(moduleLabel(source.module) + " не существует");
  }

  if (!targetModule) {
    return std::unexpected(moduleLabel(target.module) + " не существует");
  }

  if (!findByKey(sourceModule->descriptor->modOutputs, source.output)) {
    return std::unexpected(moduleLabel(source.module) + ": нет выхода модуляции \"" + source.output + "\"");
  }

  const ParameterDescriptor* parameter = findByKey(targetModule->descriptor->parameters, target.parameter);

  if (!parameter) {
    return std::unexpected(moduleLabel(target.module) + ": нет параметра \"" + target.parameter + "\"");
  }

  if (!parameter->isModulatable) {
    return std::unexpected("параметр \"" + target.parameter + "\" не модулируется");
  }

  const std::optional<ModuleId> sourceZone = getZone(*sourceModule);
  const std::optional<ModuleId> targetZone = getZone(*targetModule);

  if (sourceZone && targetZone && sourceZone != targetZone) {
    return std::unexpected("голосовой модулятор не может управлять параметром другого генератора");
  }

  const bool exists = std::ranges::any_of(this->routes, [&](const ModRoute& route) {
    return route.source == source && route.target == target;
  });

  if (exists) {
    return std::unexpected("такой маршрут уже есть");
  }

  return {};
}

std::vector<ModRoute>::iterator SynthModel::findRoute(const ModSource& source, const ModTarget& target) {
  return std::ranges::find_if(this->routes, [&](const ModRoute& route) {
    return route.source == source && route.target == target;
  });
}

CommandResult SynthModel::execute(const AddRoute& command) {
  const ModRoute& route = command.route;

  if (const auto result = this->checkRoute(route.source, route.target); !result) {
    return std::unexpected(result.error());
  }

  if (!std::isfinite(route.depth)) {
    return std::unexpected("глубина маршрута не является числом");
  }

  this->routes.push_back(route);
  this->routes.back().depth = std::clamp(route.depth, -1.0f, 1.0f);

  return std::nullopt;
}

CommandResult SynthModel::execute(const RemoveRoute& command) {
  const auto it = this->findRoute(command.source, command.target);

  if (it == this->routes.end()) {
    return std::unexpected("такого маршрута нет");
  }

  this->routes.erase(it);

  return std::nullopt;
}

CommandResult SynthModel::execute(const SetRouteDepth& command) {
  const auto it = this->findRoute(command.source, command.target);

  if (it == this->routes.end()) {
    return std::unexpected("такого маршрута нет");
  }

  if (!std::isfinite(command.depth)) {
    return std::unexpected("глубина маршрута не является числом");
  }

  it->depth = std::clamp(command.depth, -1.0f, 1.0f);

  return std::nullopt;
}

uint32_t findModuleNumber(const SynthModel& model, const ModuleInstance& module) {
  uint32_t number = 1;

  for (const auto& [id, other] : model.getModules()) {
    if (id < module.id && other.owner == module.owner && other.descriptor->type == module.descriptor->type) {
      number++;
    }
  }

  return number;
}

std::string makeModuleLabel(const SynthModel& model, const ModuleInstance& module) {
  return module.descriptor->name + " " + std::to_string(findModuleNumber(model, module));
}

ModelTransition mergeModels(const SynthModel& previous, const SynthModel& next) {
  ModelTransition transition = { next, next };
  SynthModel& scheme = transition.scheme;

  for (const auto& [id, module] : previous.modules) {
    const auto [position, isAdded] = scheme.modules.try_emplace(id, module);

    if (!isAdded) {
      position->second.enabled = position->second.enabled || module.enabled;
    }
  }

  // Провода прошлой модели циклов не образуют, поэтому новые проверяются уже относительно них
  scheme.connections = previous.connections;

  for (const Connection& connection : next.connections) {
    if (std::ranges::contains(scheme.connections, connection)) {
      continue;
    }

    if (scheme.hasAudioPath(connection.toModule, connection.fromModule)) {
      std::erase(transition.settled.connections, connection);
    } else {
      scheme.connections.push_back(connection);
    }
  }

  for (const ModRoute& route : previous.routes) {
    const bool isKept = std::ranges::any_of(next.routes, [&route](const ModRoute& other) {
      return other.source == route.source && other.target == route.target;
    });

    if (!isKept) {
      scheme.routes.push_back(route);
    }
  }

  return transition;
}
