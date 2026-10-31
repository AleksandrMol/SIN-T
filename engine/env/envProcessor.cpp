#include "./envProcessor.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_ATTACK = 0;
  constexpr uint32_t PARAM_DECAY = 1;
  constexpr uint32_t PARAM_SUSTAIN = 2;
  constexpr uint32_t PARAM_RELEASE = 3;
  constexpr uint32_t PARAM_ATTACK_CURVE = 4;
  constexpr uint32_t PARAM_DECAY_CURVE = 5;
  constexpr uint32_t PARAM_RELEASE_CURVE = 6;

  constexpr float MIN_TIME = 0.001f;
  constexpr float MAX_TIME = 10.0f;

  // Кривая 1 даёт показатель степени 1/4: стадия проходит 84% пути за первую половину времени
  constexpr float CURVE_BASE = 4.0f;

  std::unique_ptr<ModuleProcessor> createEnvProcessor() {
    return std::make_unique<EnvProcessor>();
  }

  /**
   * Форма стадии: положение 0..1 переводится в пройденный путь 0..1. Концы на месте, поэтому стадии
   * стыкуются без скачка. Кривая больше нуля проходит путь быстрее в начале, меньше нуля — в конце
   */
  float shapeStage(float position, float curve) {
    return std::pow(std::clamp(position, 0.0f, 1.0f), std::pow(CURVE_BASE, -curve));
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
    {
      .key = "attackCurve",
      .name = "Attack curve",
      .minValue = -1.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.0f,
      .shortName = "A curve",
    },
    {
      .key = "decayCurve",
      .name = "Decay curve",
      .minValue = -1.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.0f,
      .shortName = "D curve",
    },
    {
      .key = "releaseCurve",
      .name = "Release curve",
      .minValue = -1.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.0f,
      .shortName = "R curve",
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
  this->output = 0.0f;
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

/**
 * Уровень на выходе. Сама огибающая идёт линейно, а кривая меняет только то, что видно снаружи:
 * поэтому время стадии не зависит от кривой, а кривая 0 в точности даёт прежний линейный уровень (D-049)
 */
float EnvProcessor::shapeLevel(const ModulationChunk& chunk, float sustain) const {
  switch (this->stage) {
    case STAGE::ATTACK: {
      const float curve = chunk.parameters[PARAM_ATTACK_CURVE].end;

      return curve != 0.0f ? shapeStage(this->level, curve) : this->level;
    }

    case STAGE::DECAY: {
      const float curve = chunk.parameters[PARAM_DECAY_CURVE].end;
      const float range = 1.0f - sustain;

      return curve != 0.0f && range > 0.0f ? 1.0f - range * shapeStage((1.0f - this->level) / range, curve) : this->level;
    }

    case STAGE::RELEASE: {
      const float curve = chunk.parameters[PARAM_RELEASE_CURVE].end;

      return curve != 0.0f && this->releaseLevel > 0.0f
        ? this->releaseLevel * (1.0f - shapeStage(1.0f - this->level / this->releaseLevel, curve))
        : this->level;
    }

    default:
      return this->level;
  }
}

void EnvProcessor::modulate(const ModulationChunk& chunk) {
  const float sustain = chunk.parameters[PARAM_SUSTAIN].end;

  if (!chunk.voice->gate && this->stage != STAGE::IDLE && this->stage != STAGE::RELEASE) {
    // Спад начинается с того, что слышно, иначе на изогнутой стадии был бы скачок
    this->stage = STAGE::RELEASE;
    this->level = this->output;
    this->releaseLevel = this->output;
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

  this->output = this->shapeLevel(chunk, sustain);
  chunk.outputs[0] = this->output;
}
