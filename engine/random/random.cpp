#include "./random.h"

namespace {
  constexpr uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ull;

  /**
   * Перемешивание SplitMix64: соседние входы дают несвязанные выходы
   */
  uint64_t mix(uint64_t value) {
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;

    return value ^ (value >> 31);
  }
}

uint64_t Random::makeSeed(uint32_t moduleId, uint32_t voice) {
  return mix(((uint64_t)moduleId << 32) | voice);
}

void Random::setSeed(uint64_t seed) {
  this->seed = seed;
  this->state = seed;
}

void Random::rewind() {
  this->state = this->seed;
}

float Random::nextUnit() {
  this->state += GOLDEN_GAMMA;

  // Старшие 24 бита: столько точно помещается в мантиссу float, результат строго меньше 1
  return (float)(mix(this->state) >> 40) / (float)(1u << 24);
}
