#include "./largePatch.h"

#include <array>
#include <vector>

namespace {
  constexpr std::array<const char*, 5> CHAIN = { "oscillator", "filter", "amp", "env", "lfo" };
}

LargePatch buildLargePatch(MyPluginInstance* instance, size_t blocks, size_t routes) {
  LargePatch patch;
  const SynthModel& model = *instance->model;

  for (const auto& [id, module] : model.getModules()) {
    if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
      patch.generator = id;
      break;
    }
  }

  std::vector<ModuleId> modulators;
  std::vector<ModuleId> targets;
  std::optional<ModuleId> previous;

  for (size_t index = 0; index < blocks; index++) {
    const std::string type = CHAIN[index % CHAIN.size()];
    const CommandResult result = my_plug_apply_command(instance, AddModule{ type, patch.generator });

    if (!result) {
      break;
    }

    const ModuleId id = **result;
    const ModuleInstance& module = *model.findModule(id);

    patch.lastBlock = id;

    if (!module.descriptor->modOutputs.empty()) {
      modulators.push_back(id);
    }

    if (!module.descriptor->parameters.empty()) {
      targets.push_back(id);
    }

    if (previous && !module.descriptor->inputs.empty() && !model.findModule(*previous)->descriptor->outputs.empty()) {
      const std::string from = model.findModule(*previous)->descriptor->outputs.front().key;

      if (my_plug_apply_command(instance, Connect{ { *previous, from, id, module.descriptor->inputs.front().key } })) {
        patch.connectionCount++;
      }
    }

    if (!module.descriptor->outputs.empty()) {
      previous = id;
    }
  }

  // Повторы пар модель отклонит, поэтому попыток с запасом
  for (size_t attempt = 0; attempt < routes * 20 && patch.routeCount < routes && !modulators.empty(); attempt++) {
    const ModuleId source = modulators[attempt % modulators.size()];
    const ModuleInstance& target = *model.findModule(targets[(attempt * 7) % targets.size()]);
    const auto& parameters = target.descriptor->parameters;
    const ModRoute route = {
      { source, model.findModule(source)->descriptor->modOutputs.front().key },
      { target.id, parameters[attempt % parameters.size()].key },
      0.25f,
    };

    if (my_plug_apply_command(instance, AddRoute{ route })) {
      patch.routeCount++;
    }
  }

  // Считаются все блоки холста, вместе с теми, что уже были в пресете
  for (const auto& [id, module] : model.getModules()) {
    if (module.owner == patch.generator) {
      patch.blockCount++;
    }
  }

  patch.valueModule = targets.front();
  patch.valueParameter = model.findModule(patch.valueModule)->descriptor->parameters.back().key;

  return patch;
}
