#include "./oscillatorProcessor.h"

#include <memory>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_WAVE = 0;
  constexpr uint32_t PARAM_FREQUENCY = 1;
  constexpr uint32_t PARAM_LEVEL = 2;

  std::unique_ptr<ModuleProcessor> createOscillatorProcessor() {
    return std::make_unique<OscillatorProcessor>();
  }
}

const ModuleDescriptor OSCILLATOR_DESCRIPTOR = {
  .type = "oscillator",
  .name = "Oscillator",
  .outputs = { { "out", "Out" } },
  .parameters = {
    {
      .key = "wave",
      .name = "Wave",
      .minValue = (float)WAVE_TYPE::SIN,
      .maxValue = (float)WAVE_TYPE::TNG,
      .defaultValue = (float)WAVE_TYPE::SAW,
      .isStepped = true,
      .isModulatable = true,
    },
    {
      .key = "frequency",
      .name = "Frequency",
      .minValue = 20.0f,
      .maxValue = 20000.0f,
      .defaultValue = 440.0f,
      .curve = PARAM_CURVE::LOG,
      .unit = "Hz",
      .isModulatable = true,
    },
    {
      .key = "level",
      .name = "Level",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.1f,
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
  .createProcessor = createOscillatorProcessor,
};

void OscillatorProcessor::prepare(float sampleRate) {
  this->oscillator.setSampleRate(sampleRate);
}

void OscillatorProcessor::process(const ProcessChunk& chunk) {
  const ParameterRamp& frequency = chunk.parameters[PARAM_FREQUENCY];
  const ParameterRamp& level = chunk.parameters[PARAM_LEVEL];
  AudioBuffer& output = *chunk.outputs[0];

  // Ступенчатый параметр меняется на границе куска, без интерполяции (D-025)
  this->oscillator.setWave((WAVE_TYPE)chunk.parameters[PARAM_WAVE].end);

  for (uint32_t frame = 0; frame < chunk.frames; frame++) {
    this->oscillator.setFreq(frequency.at(frame, chunk.frames));
    this->oscillator.setVolume(level.at(frame, chunk.frames));
    this->oscillator.doSample();

    output.left[frame] = this->oscillator.currentSample;
    output.right[frame] = this->oscillator.currentSample;
  }
}
