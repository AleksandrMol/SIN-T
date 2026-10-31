#include "./filterProcessor.h"

#include <memory>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_CUTOFF = 0;

  std::unique_ptr<ModuleProcessor> createFilterProcessor() {
    return std::make_unique<FilterProcessor>();
  }
}

const ModuleDescriptor FILTER_DESCRIPTOR = {
  .type = "filter",
  .name = "Filter",
  .inputs = { { "in", "In" } },
  .outputs = { { "out", "Out" } },
  .parameters = {
    {
      .key = "cutoff",
      .name = "Cutoff",
      .minValue = 20.0f,
      .maxValue = 20000.0f,
      .defaultValue = 1000.0f,
      .curve = PARAM_CURVE::LOG,
      .unit = "Hz",
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
  .createProcessor = createFilterProcessor,
};

void FilterProcessor::prepare(float sampleRate) {
  this->left.setSampleRate(sampleRate);
  this->right.setSampleRate(sampleRate);
}

void FilterProcessor::process(const ProcessChunk& chunk) {
  const ParameterRamp& cutoff = chunk.parameters[PARAM_CUTOFF];
  const AudioBuffer& input = *chunk.inputs[0];
  AudioBuffer& output = *chunk.outputs[0];

  // Экспонента считается только на концах куска, между ними коэффициент интерполируется
  const ParameterRamp alpha = { this->left.getAlpha(cutoff.start), this->left.getAlpha(cutoff.end) };

  for (uint32_t frame = 0; frame < chunk.frames; frame++) {
    const float value = alpha.at(frame, chunk.frames);

    output.left[frame] = this->left.calculateOut(input.left[frame], value);
    output.right[frame] = this->right.calculateOut(input.right[frame], value);
  }
}
