#include "./builtinModules.h"

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
      .maxValue = 64.0f,
      .defaultValue = 16.0f,
      .unit = "voices",
      .isStepped = true,
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

std::expected<void, std::string> addBuiltinModules(ModuleRegistry& registry) {
  for (const ModuleDescriptor* descriptor : { &OUTPUT_DESCRIPTOR, &VOICE_OUTPUT_DESCRIPTOR, &GENERATOR_DESCRIPTOR }) {
    if (const auto result = registry.add(descriptor); !result) {
      return result;
    }
  }

  return {};
}
