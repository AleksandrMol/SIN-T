#include "./oscillatorProcessor.h"

#include <algorithm>
#include <memory>

#include "../../tools/tools.h"

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_WAVE = 0;
  constexpr uint32_t PARAM_SEMI = 1;
  constexpr uint32_t PARAM_FINE = 2;
  constexpr uint32_t PARAM_COARSE = 3;
  constexpr uint32_t PARAM_LEVEL = 4;

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
      .choices = { "Sine", "Square", "Saw", "Triangle" },
    },
    {
      .key = "semi",
      .name = "Semi",
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
      // До D-103 был ступенчатым и выполнял роль semi. Значения из старых файлов дают ту же высоту
      .key = "coarse",
      .name = "Coarse",
      .minValue = -48.0f,
      .maxValue = 48.0f,
      .defaultValue = 0.0f,
      .unit = "st",
      .isModulatable = true,
    },
    {
      .key = "level",
      .name = "Level",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.5f,
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
  const ParameterRamp& coarse = chunk.parameters[PARAM_COARSE];
  const ParameterRamp& fine = chunk.parameters[PARAM_FINE];
  const ParameterRamp& level = chunk.parameters[PARAM_LEVEL];
  AudioBuffer& output = *chunk.outputs[0];

  // Ступенчатые параметры меняются на границе куска, без интерполяции (D-025)
  this->oscillator.setWave((WAVE_TYPE)chunk.parameters[PARAM_WAVE].end);

  const float pitch = chunk.voice->pitch + chunk.parameters[PARAM_SEMI].end;

  // Экспонента считается только на концах куска, между ними частота интерполируется
  const ParameterRamp frequency = {
    this->toFrequency(pitch + coarse.start + fine.start / 100.0f),
    this->toFrequency(pitch + coarse.end + fine.end / 100.0f),
  };

  for (uint32_t frame = 0; frame < chunk.frames; frame++) {
    this->oscillator.setFreq(chunk.at(frequency, frame));
    this->oscillator.setVolume(chunk.at(level, frame));
    this->oscillator.doSample();

    output.left[frame] = this->oscillator.currentSample;
    output.right[frame] = this->oscillator.currentSample;
  }
}
