#include "./defaultPreset.h"

#include <initializer_list>
#include <optional>

namespace {
  /**
   * Выход голоса генератора: он создаётся вместе с генератором
   */
  ModuleId findVoiceOutput(const SynthModel& model, ModuleId generator) {
    for (const auto& [id, module] : model.getModules()) {
      if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
        return id;
      }
    }

    return 0;
  }
}

std::expected<SynthModel, std::string> createDefaultModel(const ModuleRegistry* registry) {
  SynthModel model(registry);

  const auto add = [&model](const char* type, std::optional<ModuleId> owner) -> std::expected<ModuleId, std::string> {
    const CommandResult result = model.apply(AddModule{ type, owner });

    if (!result) {
      return std::unexpected(result.error());
    }

    return **result;
  };

  const auto generator = add("generator", std::nullopt);

  if (!generator) {
    return std::unexpected(generator.error());
  }

  const auto oscillator = add("oscillator", *generator);
  const auto filter = add("filter", *generator);
  const auto amp = add("amp", *generator);

  if (!oscillator || !filter || !amp) {
    return std::unexpected("в реестре нет модулей пресета по умолчанию");
  }

  for (const Connection& connection : std::initializer_list<Connection>{
    { *oscillator, "out", *filter, "in" },
    { *filter, "out", *amp, "in" },
    { *amp, "out", findVoiceOutput(model, *generator), "in" },
    { *generator, "out", model.getOutputId(), "in" },
  }) {
    if (const CommandResult result = model.apply(Connect{ connection }); !result) {
      return std::unexpected(result.error());
    }
  }

  return model;
}
