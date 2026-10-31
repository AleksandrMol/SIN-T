#include "./envProcessor.h"

#include <cmath>
#include <memory>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_ATTACK = 0;
  constexpr uint32_t PARAM_DECAY = 1;
  constexpr uint32_t PARAM_SUSTAIN = 2;
  constexpr uint32_t PARAM_RELEASE = 3;

  constexpr float MIN_TIME = 0.001f;
  constexpr float MAX_TIME = 10.0f;

  std::unique_ptr<ModuleProcessor> createEnvProcessor() {
    return std::make_unique<EnvProcessor>();
  }
}

const ModuleDescriptor ENV_DESCRIPTOR = {
  .type = "env",
  .name = "ENV",
  .modOutputs = { { "out", "Out", MOD_POLARITY::UNIPOLAR } },
  .parameters = {
    {
      .key = "attack",
      .name = "Attack",
      .minValue = MIN_TIME,
      .maxValue = MAX_TIME,
      .defaultValue = 0.005f,
      .curve = PARAM_CURVE::LOG,
      .unit = "s",
      .isModulatable = true,
    },
    {
      .key = "decay",
      .name = "Decay",
      .minValue = MIN_TIME,
      .maxValue = MAX_TIME,
      .defaultValue = 0.3f,
      .curve = PARAM_CURVE::LOG,
      .unit = "s",
      .isModulatable = true,
    },
    {
      .key = "sustain",
      .name = "Sustain",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.7f,
      .isModulatable = true,
    },
    {
      .key = "release",
      .name = "Release",
      .minValue = MIN_TIME,
      .maxValue = MAX_TIME,
      .defaultValue = 0.2f,
      .curve = PARAM_CURVE::LOG,
      .unit = "s",
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
  .isVoiceModulator = true,
  .createProcessor = createEnvProcessor,
};

void EnvProcessor::prepare(float sampleRate) {
  this->sampleRate = sampleRate;
}

void EnvProcessor::reset() {
  this->stage = STAGE::ATTACK;
  this->level = 0.0f;
}

void EnvProcessor::process(const ProcessChunk&) {}

bool EnvProcessor::isFinished() const {
  return this->stage == STAGE::IDLE;
}

/**
 * Двигает уровень к цели со скоростью «distance за seconds» и тратит на это кадры.
 * Возвращает true, если цель достигнута: остаток кадров достанется следующей стадии
 */
bool EnvProcessor::moveTo(float target, float seconds, float distance, float& frames) {
  const float rate = distance / (seconds * this->sampleRate);
  const float remaining = std::fabs(target - this->level);

  if (remaining > rate * frames) {
    this->level += std::copysign(rate * frames, target - this->level);
    frames = 0.0f;

    return false;
  }

  this->level = target;
  frames -= remaining / rate;

  return true;
}

void EnvProcessor::modulate(const ModulationChunk& chunk) {
  const float sustain = chunk.parameters[PARAM_SUSTAIN].end;

  if (!chunk.voice->gate && this->stage != STAGE::IDLE && this->stage != STAGE::RELEASE) {
    this->stage = STAGE::RELEASE;
    this->releaseLevel = this->level;
  }

  float frames = (float)chunk.frames;

  while (frames > 0.0f && this->stage != STAGE::IDLE) {
    switch (this->stage) {
      case STAGE::ATTACK:
        if (this->moveTo(1.0f, chunk.parameters[PARAM_ATTACK].end, 1.0f, frames)) {
          this->stage = STAGE::DECAY;
        }
        break;

      case STAGE::DECAY:
        // Sustain мог подняться выше уровня из-за модуляции: тогда спадать некуда
        if (this->level <= sustain || this->moveTo(sustain, chunk.parameters[PARAM_DECAY].end, 1.0f - sustain, frames)) {
          this->stage = STAGE::SUSTAIN;
        }
        break;

      case STAGE::SUSTAIN:
        this->level = sustain;
        frames = 0.0f;
        break;

      case STAGE::RELEASE:
        if (this->releaseLevel <= 0.0f || this->moveTo(0.0f, chunk.parameters[PARAM_RELEASE].end, this->releaseLevel, frames)) {
          this->stage = STAGE::IDLE;
        }
        break;

      case STAGE::IDLE:
        break;
    }
  }

  chunk.outputs[0] = this->level;
}
