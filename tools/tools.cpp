#include <cmath>
#include <cstdint>

#include "tools.h"

float midiToFreq(int32_t note) {
  const float result = 440.0f * std::pow(2.0f, (float)(note - 69) / 12.0f);

  return result;
}
