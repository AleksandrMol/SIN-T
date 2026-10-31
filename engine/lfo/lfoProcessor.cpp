#include "./lfoProcessor.h"

#include <array>
#include <cmath>
#include <memory>
#include <numbers>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_WAVE = 0;
  constexpr uint32_t PARAM_RATE = 1;
  constexpr uint32_t PARAM_SYNC = 2;
  constexpr uint32_t PARAM_DIVISION = 3;

  enum class LFO_WAVE {
    SINE,
    TRIANGLE,
    SAW,
    SQUARE,
  };

  constexpr float SECONDS_PER_MINUTE = 60.0f;

  // Длительность периода в четвертных нотах, от самых длинных к самым коротким.
  // Индекс хранится в пресетах: значения можно только дописывать в конец
  constexpr std::array<float, 18> QUARTERS_PER_CYCLE = {
    6.0f, // 1/1 с точкой
    4.0f, // 1/1
    3.0f, // 1/2 с точкой
    8.0f / 3.0f, // триоль 1/1
    2.0f, // 1/2
    1.5f, // 1/4 с точкой
    4.0f / 3.0f, // триоль 1/2
    1.0f, // 1/4
    0.75f, // 1/8 с точкой
    2.0f / 3.0f, // триоль 1/4
    0.5f, // 1/8
    0.375f, // 1/16 с точкой
    1.0f / 3.0f, // триоль 1/8
    0.25f, // 1/16
    0.1875f, // 1/32 с точкой
    1.0f / 6.0f, // триоль 1/16
    0.125f, // 1/32
    1.0f / 12.0f, // триоль 1/32
  };

  constexpr float DIVISION_QUARTER = 7.0f;

  float calculateWave(LFO_WAVE wave, double phase) {
    switch (wave) {
      case LFO_WAVE::SINE:
        return (float)std::sin(2.0 * std::numbers::pi * phase);

      case LFO_WAVE::TRIANGLE:
        // Как и синус, начинается с нуля и идёт вверх
        return (float)(phase < 0.25 ? 4.0 * phase : phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0);

      case LFO_WAVE::SAW:
        return (float)(2.0 * phase - 1.0);

      case LFO_WAVE::SQUARE:
        return phase < 0.5 ? 1.0f : -1.0f;
    }

    return 0.0f;
  }

  std::unique_ptr<ModuleProcessor> createLfoProcessor() {
    return std::make_unique<LfoProcessor>();
  }
}

const ModuleDescriptor LFO_DESCRIPTOR = {
  .type = "lfo",
  .name = "LFO",
  .modOutputs = { { "out", "Out", MOD_POLARITY::BIPOLAR } },
  .parameters = {
    {
      .key = "wave",
      .name = "Wave",
      .minValue = (float)LFO_WAVE::SINE,
      .maxValue = (float)LFO_WAVE::SQUARE,
      .defaultValue = (float)LFO_WAVE::SINE,
      .isStepped = true,
      .isModulatable = true,
    },
    {
      .key = "rate",
      .name = "Rate",
      .minValue = 0.01f,
      .maxValue = 50.0f,
      .defaultValue = 2.0f,
      .curve = PARAM_CURVE::LOG,
      .unit = "Hz",
      .isModulatable = true,
    },
    {
      .key = "sync",
      .name = "Sync",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.0f,
      .isStepped = true,
    },
    {
      .key = "division",
      .name = "Division",
      .minValue = 0.0f,
      .maxValue = (float)QUARTERS_PER_CYCLE.size() - 1.0f,
      .defaultValue = DIVISION_QUARTER,
      .isStepped = true,
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
  .createProcessor = createLfoProcessor,
};

void LfoProcessor::prepare(float sampleRate) {
  this->sampleRate = sampleRate;
}

void LfoProcessor::reset() {
  this->phase = 0.0;
}

void LfoProcessor::process(const ProcessChunk&) {}

void LfoProcessor::modulate(const ModulationChunk& chunk) {
  const bool isSynced = chunk.parameters[PARAM_SYNC].end > 0.5f;
  const float quarters = QUARTERS_PER_CYCLE[(size_t)chunk.parameters[PARAM_DIVISION].end];
  const float frequency = isSynced ? chunk.tempo / SECONDS_PER_MINUTE / quarters : chunk.parameters[PARAM_RATE].end;

  this->phase = std::fmod(this->phase + (double)frequency * chunk.frames / this->sampleRate, 1.0);

  chunk.outputs[0] = calculateWave((LFO_WAVE)chunk.parameters[PARAM_WAVE].end, this->phase);
}
