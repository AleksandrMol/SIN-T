#include "./builtinModules.h"

#include "../voicePool/voicePool.h"

namespace {
  std::vector<ModOutputDescriptor> makeMacroOutputs() {
    std::vector<ModOutputDescriptor> outputs;

    for (uint32_t index = 1; index <= MACRO_COUNT; index++) {
      const std::string number = std::to_string(index);

      outputs.push_back({ "macro" + number, "Macro " + number, MOD_POLARITY::UNIPOLAR });
    }

    return outputs;
  }

  std::vector<ParameterDescriptor> makeMacroParameters() {
    std::vector<ParameterDescriptor> parameters;

    for (uint32_t index = 1; index <= MACRO_COUNT; index++) {
      const std::string number = std::to_string(index);

      parameters.push_back({
        .key = "macro" + number,
        .name = "Macro " + number,
        .minValue = 0.0f,
        .maxValue = 1.0f,
        .defaultValue = 0.0f,
      });
    }

    return parameters;
  }
}

const ModuleDescriptor OUTPUT_DESCRIPTOR = {
  .type = "output",
  .name = "Output",
  .inputs = { { "in", "In" } },
  .role = MODULE_ROLE::OUTPUT,
};

const ModuleDescriptor VOICE_OUTPUT_DESCRIPTOR = {
  .type = "voiceOutput",
  .name = "Voice output",
  .inputs = { { "in", "In" } },
  .isAllowedInPoly = true,
  .role = MODULE_ROLE::OUTPUT,
};

// Запрет в полифонической зоне и есть запрет вложенных генераторов (D-004)
const ModuleDescriptor GENERATOR_DESCRIPTOR = {
  .type = "generator",
  .name = "Generator",
  .outputs = { { "out", "Out" } },
  .modOutputs = { { "velocity", "Velocity", MOD_POLARITY::UNIPOLAR } },
  .parameters = {
    {
      .key = "polyphony",
      .name = "Polyphony",
      .minValue = 1.0f,
      .maxValue = (float)MAX_POLYPHONY,
      .defaultValue = 16.0f,
      .unit = "voices",
      .isStepped = true,
      .isStructural = true,
    },
    {
      .key = "pitchBendRange",
      .name = "Pitch bend range",
      .minValue = 0.0f,
      .maxValue = 48.0f,
      .defaultValue = 2.0f,
      .unit = "st",
      .isStepped = true,
    },
  },
  .isAllowedInPoly = false,
  .role = MODULE_ROLE::GENERATOR,
};

// Выход каждого макроса равен его параметру: значение задаёт хост, остальное делает матрица (D-055)
const ModuleDescriptor MACROS_DESCRIPTOR = {
  .type = "macros",
  .name = "Macros",
  .modOutputs = makeMacroOutputs(),
  .parameters = makeMacroParameters(),
  .role = MODULE_ROLE::MACROS,
};

std::expected<void, std::string> addBuiltinModules(ModuleRegistry& registry) {
  for (const ModuleDescriptor* descriptor : {
    &OUTPUT_DESCRIPTOR,
    &VOICE_OUTPUT_DESCRIPTOR,
    &GENERATOR_DESCRIPTOR,
    &MACROS_DESCRIPTOR,
  }) {
    if (const auto result = registry.add(descriptor); !result) {
      return result;
    }
  }

  return {};
}
