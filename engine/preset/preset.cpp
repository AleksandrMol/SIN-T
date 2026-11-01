#include "./preset.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "../builtinModules/builtinModules.h"
#include "../presetMigration/presetMigration.h"

namespace {
  using json = nlohmann::json;

  /**
   * Самая короткая запись числа: 1200 и 0.1 вместо 1200.0 и 0.10000000149011612.
   * При чтении в float значение восстанавливается точно
   */
  json toJsonNumber(float value) {
    if (std::trunc(value) == value && std::fabs(value) < 1e9f) {
      return (int64_t)value;
    }

    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), value);

    double shortest = 0.0;
    std::from_chars(text, written.ptr, shortest);

    return shortest;
  }

  json positionToJson(const ModulePosition& position) {
    return { { "x", toJsonNumber(position.x) }, { "y", toJsonNumber(position.y) } };
  }

  json moduleToJson(const ModuleInstance& module) {
    if (module.isStub()) {
      json stub = json::parse(module.stubData);

      // Положение — единственное, что меняется у заглушки: блок можно передвинуть
      if (module.position) {
        stub["position"] = positionToJson(*module.position);
      }

      return stub;
    }

    json result = json::object();
    result["id"] = module.id;
    result["type"] = module.descriptor->type;
    result["typeVersion"] = module.descriptor->typeVersion;

    if (module.owner) {
      result["owner"] = *module.owner;
    }

    if (!module.enabled) {
      result["enabled"] = false;
    }

    if (module.position) {
      result["position"] = positionToJson(*module.position);
    }

    if (!module.parameters.empty()) {
      json parameters = json::object();

      for (const auto& [key, value] : module.parameters) {
        parameters[key] = toJsonNumber(value);
      }

      result["parameters"] = std::move(parameters);
    }

    return result;
  }

  /**
   * Несдвинутые макросы в файл не пишутся: версия формата остаётся прежней, старые файлы читаются как есть
   */
  bool hasDefaultParameters(const ModuleInstance& module) {
    return std::ranges::all_of(module.descriptor->parameters, [&module](const ParameterDescriptor& parameter) {
      return module.parameters.at(parameter.key) == parameter.defaultValue;
    });
  }

  ModuleId readId(const json& value) {
    if (!value.is_number_unsigned() || value.get<uint64_t>() >= std::numeric_limits<ModuleId>::max()) {
      throw PresetError("неверный ID модуля: " + value.dump());
    }

    return value.get<ModuleId>();
  }

  const json& readArray(const json& file, const char* key) {
    const json& value = file.at(key);

    if (!value.is_array()) {
      throw PresetError(std::string(key) + " должен быть массивом");
    }

    return value;
  }

  Connection readConnection(const json& entry) {
    return {
      .fromModule = readId(entry.at("fromModule")),
      .fromPort = entry.at("fromPort").get<std::string>(),
      .toModule = readId(entry.at("toModule")),
      .toPort = entry.at("toPort").get<std::string>(),
    };
  }

  ModRoute readRoute(const json& entry) {
    const json& source = entry.at("source");
    const json& target = entry.at("target");

    return {
      .source = { readId(source.at("module")), source.at("output").get<std::string>() },
      .target = { readId(target.at("module")), target.at("parameter").get<std::string>() },
      .depth = entry.at("depth").get<float>(),
    };
  }

  /**
   * Добавляет элемент с таким ключом, если его ещё нет
   */
  template <typename T>
  void addUnique(std::vector<T>& items, const std::string& key, T item) {
    const bool exists = std::any_of(items.begin(), items.end(), [&key](const T& existing) {
      return existing.key == key;
    });

    if (!exists) {
      items.push_back(std::move(item));
    }
  }

  /**
   * Восстанавливает порты и параметры заглушки по соединениям и маршрутам файла (D-021)
   */
  std::shared_ptr<const ModuleDescriptor> buildStubDescriptor(ModuleId id, const std::string& type, const json& file) {
    auto descriptor = std::make_shared<ModuleDescriptor>();
    descriptor->type = type;
    descriptor->name = type;
    descriptor->isAllowedInPoly = true;

    for (const json& entry : readArray(file, "connections")) {
      const Connection connection = readConnection(entry);

      if (connection.toModule == id) {
        addUnique(descriptor->inputs, connection.toPort, { connection.toPort, connection.toPort });
      }

      if (connection.fromModule == id) {
        addUnique(descriptor->outputs, connection.fromPort, { connection.fromPort, connection.fromPort });
      }
    }

    for (const json& entry : readArray(file, "routes")) {
      const ModRoute route = readRoute(entry);

      if (route.source.module == id) {
        addUnique(descriptor->modOutputs, route.source.output, { route.source.output, route.source.output, MOD_POLARITY::UNIPOLAR });
      }

      if (route.target.module == id) {
        addUnique(descriptor->parameters, route.target.parameter, {
          .key = route.target.parameter,
          .name = route.target.parameter,
          .minValue = 0.0f,
          .maxValue = 1.0f,
          .defaultValue = 0.0f,
          .isModulatable = true,
        });
      }
    }

    return descriptor;
  }

  /**
   * Проверяет то, что команды гарантируют сами, а загрузка восстанавливает напрямую:
   * принадлежность модулей подграфам и по одному выходу голоса в каждом генераторе (D-004, D-034)
   */
  void checkStructure(const SynthModel& model) {
    std::map<ModuleId, int> voiceOutputs;

    for (const auto& [id, module] : model.getModules()) {
      if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
        voiceOutputs.try_emplace(id, 0);
      }
    }

    for (const auto& [id, module] : model.getModules()) {
      const bool isVoiceOutput = module.descriptor->type == VOICE_OUTPUT_DESCRIPTOR.type;

      if (!module.owner) {
        if (isVoiceOutput) {
          throw PresetError("выход голоса вне генератора");
        }

        continue;
      }

      if (!voiceOutputs.contains(*module.owner)) {
        throw PresetError("владелец модуля " + std::to_string(id) + " не является генератором");
      }

      if (!module.descriptor->isAllowedInPoly) {
        throw PresetError("тип \"" + module.descriptor->type + "\" недопустим в полифонической зоне");
      }

      if (isVoiceOutput) {
        voiceOutputs[*module.owner]++;
      }
    }

    for (const auto& [generator, count] : voiceOutputs) {
      if (count != 1) {
        throw PresetError("у генератора " + std::to_string(generator) + " должен быть один выход голоса");
      }
    }
  }

  void throwIfFailed(const CommandResult& result) {
    if (!result) {
      throw PresetError(result.error());
    }
  }
}

std::string saveModel(const SynthModel& model) {
  json file = json::object();
  file["version"] = CURRENT_PRESET_VERSION;
  file["nextModuleId"] = model.getNextId();

  json modules = json::array();
  for (const auto& [id, module] : model.getModules()) {
    if (module.descriptor->role != MODULE_ROLE::MACROS || !hasDefaultParameters(module)) {
      modules.push_back(moduleToJson(module));
    }
  }
  file["modules"] = std::move(modules);

  json connections = json::array();
  for (const Connection& connection : model.getConnections()) {
    connections.push_back({
      { "fromModule", connection.fromModule },
      { "fromPort", connection.fromPort },
      { "toModule", connection.toModule },
      { "toPort", connection.toPort },
    });
  }
  file["connections"] = std::move(connections);

  json routes = json::array();
  for (const ModRoute& route : model.getRoutes()) {
    routes.push_back({
      { "source", { { "module", route.source.module }, { "output", route.source.output } } },
      { "target", { { "module", route.target.module }, { "parameter", route.target.parameter } } },
      { "depth", toJsonNumber(route.depth) },
    });
  }
  file["routes"] = std::move(routes);

  return file.dump(2);
}

std::expected<SynthModel, std::string> loadModel(const ModuleRegistry* registry, std::string_view text) {
  try {
    json file = json::parse(text.begin(), text.end());

    migrateFile(file);
    migrateModules(file, registry);

    SynthModel model(registry);
    ModuleId maxId = model.outputId;
    bool hasOutput = false;
    bool hasMacros = false;

    // Настройки задаются после всех модулей: команде нужен уже созданный модуль
    std::vector<std::pair<ModuleId, const json*>> settingEntries;

    for (const json& entry : readArray(file, "modules")) {
      const ModuleId id = readId(entry.at("id"));
      const std::string type = entry.at("type").get<std::string>();
      const uint32_t typeVersion = entry.at("typeVersion").get<uint32_t>();
      const std::optional<ModuleId> owner = entry.contains("owner")
        ? std::optional<ModuleId>(readId(entry.at("owner")))
        : std::nullopt;

      maxId = std::max(maxId, id);

      if (type == OUTPUT_DESCRIPTOR.type && !owner) {
        if (id != model.outputId || hasOutput) {
          throw PresetError("неверный выход графа");
        }

        hasOutput = true;
        settingEntries.emplace_back(id, &entry);
        continue;
      }

      if (type == MACROS_DESCRIPTOR.type) {
        if (id != MACROS_MODULE_ID || owner || hasMacros) {
          throw PresetError("неверный модуль макросов");
        }

        hasMacros = true;
        settingEntries.emplace_back(id, &entry);
        continue;
      }

      if (model.modules.contains(id)) {
        throw PresetError("ID " + std::to_string(id) + " повторяется");
      }

      const ModuleDescriptor* descriptor = registry->find(type);

      if (descriptor && typeVersion <= descriptor->typeVersion) {
        model.insertModule(id, descriptor, owner);
        settingEntries.emplace_back(id, &entry);
      } else {
        auto stubDescriptor = buildStubDescriptor(id, type, file);

        model.insertModule(id, stubDescriptor.get(), owner);

        ModuleInstance& stub = model.modules.at(id);
        stub.stubDescriptor = std::move(stubDescriptor);
        stub.stubData = entry.dump();
      }
    }

    if (!hasOutput) {
      throw PresetError("нет выхода графа");
    }

    checkStructure(model);

    // Положение есть и у заглушек, поэтому читается отдельно от настроек (D-065)
    for (const json& entry : readArray(file, "modules")) {
      if (entry.contains("position")) {
        const json& position = entry.at("position");

        throwIfFailed(model.apply(SetModulePosition{
          readId(entry.at("id")),
          { position.at("x").get<float>(), position.at("y").get<float>() },
        }));
      }
    }

    for (const auto& [id, entry] : settingEntries) {
      if (entry->contains("enabled")) {
        throwIfFailed(model.apply(SetModuleEnabled{ id, entry->at("enabled").get<bool>() }));
      }

      if (!entry->contains("parameters")) {
        continue;
      }

      const json& values = entry->at("parameters");

      if (!values.is_object()) {
        throw PresetError("parameters должен быть объектом");
      }

      // Параметр, которого нет в файле, остаётся со значением по умолчанию.
      // Ключи, неизвестные текущей версии модуля, пропускаются
      for (const ParameterDescriptor& parameter : model.modules.at(id).descriptor->parameters) {
        if (values.contains(parameter.key)) {
          throwIfFailed(model.apply(SetParameter{ id, parameter.key, values.at(parameter.key).get<float>() }));
        }
      }
    }

    for (const json& entry : readArray(file, "connections")) {
      throwIfFailed(model.apply(Connect{ readConnection(entry) }));
    }

    for (const json& entry : readArray(file, "routes")) {
      throwIfFailed(model.apply(AddRoute{ readRoute(entry) }));
    }

    model.nextId = std::max(readId(file.at("nextModuleId")), maxId + 1);

    return model;
  } catch (const PresetError& error) {
    return std::unexpected(std::string("ошибка в пресете: ") + error.what());
  } catch (const json::exception& error) {
    return std::unexpected(std::string("ошибка в пресете: ") + error.what());
  }
}
