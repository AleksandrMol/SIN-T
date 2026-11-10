#include "./oscillatorProcessor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

#include "../../tools/tools.h"

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_WAVE = 0;
  constexpr uint32_t PARAM_OCTAVE = 1;
  constexpr uint32_t PARAM_SEMI = 2;
  constexpr uint32_t PARAM_FINE = 3;
  constexpr uint32_t PARAM_COARSE = 4;
  constexpr uint32_t PARAM_PHASE = 5;
  constexpr uint32_t PARAM_RAND = 6;
  constexpr uint32_t PARAM_UNISON = 7;
  constexpr uint32_t PARAM_DETUNE = 8;
  constexpr uint32_t PARAM_WIDTH = 9;
  constexpr uint32_t PARAM_PAN = 10;
  constexpr uint32_t PARAM_LEVEL = 11;

  constexpr float PAN_RANGE = 100.0f;
  constexpr float FULL_CYCLE_DEGREES = 360.0f;
  constexpr float PERCENT = 100.0f;
  constexpr float CENTS_PER_SEMITONE = 100.0f;
  constexpr float SEMITONES_PER_OCTAVE = 12.0f;
  constexpr float OCTAVE_RANGE = 4.0f;

  // Подголос появляется, уходит и переезжает на новое место за 10 мс (D-023)
  constexpr float FADE_SECONDS = 0.01f;

  // Выше этой доли частоты дискретизации шаг фазы перестаёт быть корректным
  constexpr float MAX_FREQUENCY_RATIO = 0.49f;

  std::unique_ptr<ModuleProcessor> createOscillatorProcessor() {
    return std::make_unique<OscillatorProcessor>();
  }

  /**
   * Усиление каналов по закону постоянной мощности, в центре оба ровно 1 (D-104).
   * Положение -1..1. √2·cos(π/4 + φ) = cos φ − sin φ: при φ = 0 это точная единица,
   * звук без панорамы не меняется
   */
  void toPanGains(float position, float& left, float& right) {
    const float angle = position * std::numbers::pi_v<float> / 4.0f;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);

    left = cosine - sine;
    right = cosine + sine;
  }

  /**
   * Место подголоса в унисоне: от -1 у самого низкого до 1 у самого высокого, один — посередине
   */
  float toSpread(uint32_t index, uint32_t count) {
    return count > 1 ? -1.0f + 2.0f * (float)index / (float)(count - 1) : 0.0f;
  }

  /**
   * 1/√x целиком в double: у sqrtf в glibc 2.43 новая версия символа, и плагин не загрузился бы
   * в среде flatpak-Reaper (D-107). (float)sqrt((double)x) не годится: GCC сам сводит его к sqrtf
   */
  float inverseSqrt(float value) {
    return (float)(1.0 / std::sqrt((double)value));
  }

  float moveToward(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
  }

  /**
   * Положение подголоса в стерео: Pan сдвигает всю картину, Width раздвигает подголоса
   */
  float toPosition(float pan, float width, float spread) {
    return std::clamp(pan / PAN_RANGE + width / PERCENT * spread, -1.0f, 1.0f);
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
      // Сдвиг на целые октавы, как OCT на референсе пользователя (D-108). Нет в файле — ноль, звук прежний
      .key = "octave",
      .name = "Octave",
      .minValue = -OCTAVE_RANGE,
      .maxValue = OCTAVE_RANGE,
      .defaultValue = 0.0f,
      .isStepped = true,
      .isModulatable = true,
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
      .decimals = 1,
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
      .decimals = 2,
    },
    {
      .key = "phase",
      .name = "Phase",
      .minValue = 0.0f,
      .maxValue = FULL_CYCLE_DEGREES,
      .defaultValue = 0.0f,
      .unit = "°",
      .isModulatable = true,
      .decimals = 0,
    },
    {
      // По умолчанию каждая нота со случайной фазы: решение пользователя, звук старых пресетов изменился (D-105)
      .key = "rand",
      .name = "Rand",
      .minValue = 0.0f,
      .maxValue = PERCENT,
      .defaultValue = PERCENT,
      .unit = "%",
      .isModulatable = true,
      .decimals = 0,
    },
    {
      .key = "unison",
      .name = "Unison",
      .minValue = 1.0f,
      .maxValue = (float)MAX_UNISON,
      .defaultValue = 1.0f,
      .isStepped = true,
      .isModulatable = true,
    },
    {
      // Насколько крайние подголоса уходят вверх и вниз от ноты, остальные между ними поровну
      .key = "detune",
      .name = "Detune",
      .minValue = 0.0f,
      .maxValue = 100.0f,
      .defaultValue = 20.0f,
      .unit = "ct",
      .isModulatable = true,
      .decimals = 1,
    },
    {
      .key = "width",
      .name = "Width",
      .minValue = 0.0f,
      .maxValue = PERCENT,
      .defaultValue = PERCENT,
      .unit = "%",
      .isModulatable = true,
      .decimals = 0,
    },
    {
      .key = "pan",
      .name = "Pan",
      .minValue = -PAN_RANGE,
      .maxValue = PAN_RANGE,
      .defaultValue = 0.0f,
      .isModulatable = true,
      .decimals = 0,
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
  for (UnisonVoice& voice : this->voices) {
    voice.oscillator.setSampleRate(sampleRate);
  }

  this->maxFrequency = sampleRate * MAX_FREQUENCY_RATIO;
  this->fadeStep = 1.0f / (FADE_SECONDS * sampleRate);
}

void OscillatorProcessor::seed(uint64_t seed) {
  this->random.setSeed(seed);
}

void OscillatorProcessor::rewind() {
  this->random.rewind();
}

void OscillatorProcessor::reset() {
  for (UnisonVoice& voice : this->voices) {
    voice.oscillator.stopWave();
    voice.isActive = false;
  }

  this->isStarting = true;
}

float OscillatorProcessor::toFrequency(float pitch) const {
  return std::min(midiToFreq(pitch), this->maxFrequency);
}

/**
 * Подголос начинает звучать со своей случайной фазы (D-105)
 */
void OscillatorProcessor::startVoice(UnisonVoice& voice, float rand) {
  voice.oscillator.setPhase(rand / PERCENT * this->random.nextUnit());
  voice.isActive = true;
}

void OscillatorProcessor::process(const ProcessChunk& chunk) {
  const ParameterRamp& coarse = chunk.parameters[PARAM_COARSE];
  const ParameterRamp& fine = chunk.parameters[PARAM_FINE];
  const ParameterRamp& detune = chunk.parameters[PARAM_DETUNE];
  const ParameterRamp& width = chunk.parameters[PARAM_WIDTH];
  const ParameterRamp& pan = chunk.parameters[PARAM_PAN];
  const ParameterRamp& level = chunk.parameters[PARAM_LEVEL];
  const ParameterRamp& phase = chunk.parameters[PARAM_PHASE];
  const float rand = chunk.parameters[PARAM_RAND].start;
  AudioBuffer& output = *chunk.outputs[0];

  // Ступенчатые параметры меняются на границе куска, без интерполяции (D-025)
  const auto wave = (WAVE_TYPE)chunk.parameters[PARAM_WAVE].end;
  const auto count = (uint32_t)chunk.parameters[PARAM_UNISON].end;
  const float pitch = chunk.voice->pitch + chunk.parameters[PARAM_OCTAVE].end * SEMITONES_PER_OCTAVE + chunk.parameters[PARAM_SEMI].end;

  // Значения на концах участка сетки. Состояние подголоса меняется только в конце участка,
  // поэтому все куски одного участка видят одни и те же концы, как бы хост ни резал блок
  const float segmentStep = this->fadeStep * (float)chunk.rampFrames;
  const bool isSegmentEnd = chunk.rampOffset + chunk.frames == chunk.rampFrames;
  std::array<ParameterRamp, MAX_UNISON> gains{};
  std::array<ParameterRamp, MAX_UNISON> spreads{};
  ParameterRamp totalGain{};

  for (uint32_t index = 0; index < MAX_UNISON; index++) {
    UnisonVoice& voice = this->voices[index];
    const bool isWanted = index < count;

    // Новый подголос встаёт сразу на своё место и плавно набирает громкость. Нота начинается сразу со всеми
    // подголосами. Числа берутся по порядку подголосов и при Rand 0: при одном подголоске последовательность та же, что до унисона
    if (isWanted && !voice.isActive) {
      voice.gain = this->isStarting ? 1.0f : 0.0f;
      voice.spread = toSpread(index, count);
      this->startVoice(voice, rand);
    }

    if (!voice.isActive) {
      continue;
    }

    gains[index] = { voice.gain, moveToward(voice.gain, isWanted ? 1.0f : 0.0f, segmentStep) };
    spreads[index] = { voice.spread, moveToward(voice.spread, toSpread(index, count), 2.0f * segmentStep) };
    totalGain.start += gains[index].start;
    totalGain.end += gains[index].end;
  }

  this->isStarting = false;

  // Сумма подголосов делится на √N: громкость унисона почти не зависит от их числа.
  // Первый подголос нужен всегда, поэтому сумма не меньше 1
  const ParameterRamp normalize = { inverseSqrt(totalGain.start), inverseSqrt(totalGain.end) };

  // Общее для всех подголосов считается один раз на кусок, а не для каждого подголоска
  std::array<float, MAX_CHUNK_FRAMES> volumes;
  std::array<float, MAX_CHUNK_FRAMES> phaseOffsets;

  for (uint32_t frame = 0; frame < chunk.frames; frame++) {
    volumes[frame] = chunk.at(level, frame) * chunk.at(normalize, frame);
    phaseOffsets[frame] = chunk.at(phase, frame) / FULL_CYCLE_DEGREES;
  }

  std::fill_n(output.left.begin(), chunk.frames, 0.0f);
  std::fill_n(output.right.begin(), chunk.frames, 0.0f);

  for (uint32_t index = 0; index < MAX_UNISON; index++) {
    UnisonVoice& voice = this->voices[index];

    if (!voice.isActive) {
      continue;
    }

    const bool isWanted = index < count;
    const ParameterRamp& gain = gains[index];
    const ParameterRamp& spread = spreads[index];

    // Экспонента, синус и косинус считаются только на концах участка, между ними значения идут по прямой
    const ParameterRamp frequency = {
      this->toFrequency(pitch + coarse.start + fine.start / CENTS_PER_SEMITONE + detune.start / CENTS_PER_SEMITONE * spread.start),
      this->toFrequency(pitch + coarse.end + fine.end / CENTS_PER_SEMITONE + detune.end / CENTS_PER_SEMITONE * spread.end),
    };

    ParameterRamp leftGain{};
    ParameterRamp rightGain{};

    toPanGains(toPosition(pan.start, width.start, spread.start), leftGain.start, rightGain.start);
    toPanGains(toPosition(pan.end, width.end, spread.end), leftGain.end, rightGain.end);
    voice.oscillator.setWave(wave);

    // Без модуляции высоты частота на участке постоянна: шаг фазы считается один раз
    const bool isSteadyFrequency = frequency.start == frequency.end;

    if (isSteadyFrequency) {
      voice.oscillator.setFreq(frequency.start);
    }

    for (uint32_t frame = 0; frame < chunk.frames; frame++) {
      if (!isSteadyFrequency) {
        voice.oscillator.setFreq(chunk.at(frequency, frame));
      }

      voice.oscillator.setVolume(volumes[frame] * chunk.at(gain, frame));
      voice.oscillator.setPhaseOffset(phaseOffsets[frame]);
      voice.oscillator.doSample();

      output.left[frame] += voice.oscillator.currentSample * chunk.at(leftGain, frame);
      output.right[frame] += voice.oscillator.currentSample * chunk.at(rightGain, frame);
    }

    if (isSegmentEnd) {
      voice.gain = gain.end;
      voice.spread = spread.end;
      voice.isActive = isWanted || voice.gain > 0.0f;
    }
  }
}
