#include <doctest/doctest.h>

#include "../tools/tools.h"

TEST_CASE("midiToFreq переводит клавиши в частоту равномерного строя") {
  CHECK(midiToFreq(69) == doctest::Approx(440.0f));
  CHECK(midiToFreq(57) == doctest::Approx(220.0f));
  CHECK(midiToFreq(81) == doctest::Approx(880.0f));
  CHECK(midiToFreq(60) == doctest::Approx(261.6256f).epsilon(0.0001));
}
