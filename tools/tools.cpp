#include <cmath>
#include <cstdint>

#include "tools.h"

float midiToFreq(float note) {
  const float result = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);

  return result;
}
