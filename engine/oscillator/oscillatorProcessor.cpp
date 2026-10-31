#include "./oscillatorProcessor.h"

#include <algorithm>
#include <memory>

#include "../../tools/tools.h"

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_WAVE = 0;
  constexpr uint32_t PARAM_COARSE = 1;
  constexpr uint32_t PARAM_FINE = 2;
  constexpr uint32_t PARAM_LEVEL = 3;

  // Выше этой доли частоты дискретизации шаг фазы перестаёт быть корректным
  constexpr float MAX_FREQUENCY_RATIO = 0.49f;

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
      .key = "coarse",
      .name = "Coarse",
      .minValue = -48.0f,
      .maxValue = 48.0f,
      .defaultValue = 0.0f,
      .unit = "st",
      .isStepped = true,
      .isModulatable = true,
    },
    {
      .key = "fine",
      .name = "Fine",
      .minValue = -100.0f,
      .maxValue = 100.0f,
      .defaultValue = 0.0f,
      .unit = "ct",
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
  this->maxFrequency = sampleRate * MAX_FREQUENCY_RATIO;
}

void OscillatorProcessor::reset() {
  this->oscillator.stopWave();
}

float OscillatorProcessor::toFrequency(float pitch) const {
  return std::min(midiToFreq(pitch), this->maxFrequency);
}

void OscillatorProcessor::process(const ProcessChunk& chunk) {
  const ParameterRamp& fine = chunk.parameters[PARAM_FINE];
  const ParameterRamp& level = chunk.parameters[PARAM_LEVEL];
  AudioBuffer& output = *chunk.outputs[0];

  // Ступенчатые параметры меняются на границе куска, без интерполяции (D-025)
  this->oscillator.setWave((WAVE_TYPE)chunk.parameters[PARAM_WAVE].end);

  const float pitch = chunk.voice->pitch + chunk.parameters[PARAM_COARSE].end;

  // Экспонента считается только на концах куска, между ними частота интерполируется
  const ParameterRamp frequency = {
    this->toFrequency(pitch + fine.start / 100.0f),
    this->toFrequency(pitch + fine.end / 100.0f),
  };

  for (uint32_t frame = 0; frame < chunk.frames; frame++) {
    this->oscillator.setFreq(chunk.at(frequency, frame));
    this->oscillator.setVolume(chunk.at(level, frame));
    this->oscillator.doSample();

    output.left[frame] = this->oscillator.currentSample;
    output.right[frame] = this->oscillator.currentSample;
  }
}
