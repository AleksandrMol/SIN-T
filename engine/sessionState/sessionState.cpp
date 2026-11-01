#include "./sessionState.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "../preset/preset.h"
#include "../presetMigration/presetMigration.h"

namespace {
  using json = nlohmann::json;

  void restoreSlots(const json& session, SlotTable& slots) {
    for (const json& entry : session.at("slots")) {
      const bool isRestored = slots.restore(entry.at("slot").get<uint32_t>(), {
        .module = entry.at("module").get<ModuleId>(),
        .type = entry.at("type").get<std::string>(),
        .parameter = entry.at("parameter").get<std::string>(),
      });

      if (!isRestored) {
        throw std::invalid_argument("неверный номер слота: " + entry.at("slot").dump());
      }
    }
  }
}

std::string saveSession(const SynthModel& model, const SlotTable& slots, const std::string& presetName) {
  json file = json::parse(saveModel(model));
  json entries = json::array();

  for (const auto& [slot, binding] : slots.getPoolBindings()) {
    entries.push_back({
      { "slot", slot },
      { "module", binding.module },
      { "type", binding.type },
      { "parameter", binding.parameter },
    });
  }

  file["session"] = {
    { "presetName", presetName },
    { "slots", std::move(entries) },
  };

  return file.dump(2);
}

std::expected<SessionState, std::string> loadSession(const ModuleRegistry* registry, std::string_view text) {
  auto model = loadModel(registry, text);

  if (!model) {
    return std::unexpected(model.error());
  }

  try {
    // Поле session тоже подчиняется версии файла, поэтому читается после миграций
    json file = json::parse(text.begin(), text.end());

    migrateFile(file);

    SessionState state = { .model = std::move(*model), .slots = {}, .presetName = {} };

    if (file.contains("session")) {
      const json& session = file.at("session");

      state.presetName = session.at("presetName").get<std::string>();
      restoreSlots(session, state.slots);
    }

    state.slots.sync(state.model);

    return state;
  } catch (const json::exception& error) {
    return std::unexpected(std::string("ошибка в состоянии: ") + error.what());
  } catch (const PresetError& error) {
    return std::unexpected(std::string("ошибка в состоянии: ") + error.what());
  } catch (const std::invalid_argument& error) {
    return std::unexpected(std::string("ошибка в состоянии: ") + error.what());
  }
}
