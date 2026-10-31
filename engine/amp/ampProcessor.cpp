#include "./ampProcessor.h"

#include <memory>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_GAIN = 0;
  constexpr uint32_t PARAM_VELOCITY = 1;

  std::unique_ptr<ModuleProcessor> createAmpProcessor() {
    return std::make_unique<AmpProcessor>();
  }
}

const ModuleDescriptor AMP_DESCRIPTOR = {
  .type = "amp",
  .name = "Amp",
  .inputs = { { "in", "In" } },
  .outputs = { { "out", "Out" } },
  .parameters = {
    {
      .key = "gain",
      .name = "Gain",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 1.0f,
      .isModulatable = true,
    },
    {
      .key = "velocity",
      .name = "Velocity sensitivity",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 1.0f,
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
  .createProcessor = createAmpProcessor,
};

void AmpProcessor::prepare(float) {}

void AmpProcessor::reset() {}

void AmpProcessor::process(const ProcessChunk& chunk) {
  const ParameterRamp& gain = chunk.parameters[PARAM_GAIN];
  const ParameterRamp& sensitivity = chunk.parameters[PARAM_VELOCITY];
  const AudioBuffer& input = *chunk.inputs[0];
  AudioBuffer& output = *chunk.outputs[0];

  for (uint32_t frame = 0; frame < chunk.frames; frame++) {
    const float amount = chunk.at(sensitivity, frame);
    const float value = chunk.at(gain, frame) * (1.0f - amount + amount * chunk.voice->velocity);

    output.left[frame] = input.left[frame] * value;
    output.right[frame] = input.right[frame] * value;
  }
}
