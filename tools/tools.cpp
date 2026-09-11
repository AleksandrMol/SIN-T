#include "tools.h"
#include <cmath>
#include <cstdint>

float midiToFreq (uint32_t note) {
  return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f);
};
