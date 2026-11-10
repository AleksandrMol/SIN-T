#include <doctest/doctest.h>

#include <vector>

#include "../engine/random/random.h"

namespace {
  constexpr int DRAW_COUNT = 10000;

  std::vector<float> draw(Random& random, int count) {
    std::vector<float> values;

    for (int index = 0; index < count; index++) {
      values.push_back(random.nextUnit());
    }

    return values;
  }
}

TEST_CASE("Случайные числа лежат в [0, 1) и заполняют весь отрезок") {
  Random random;

  random.setSeed(Random::makeSeed(7, 0));

  int lowerHalf = 0;
  float lowest = 1.0f;
  float highest = 0.0f;

  for (const float value : draw(random, DRAW_COUNT)) {
    REQUIRE(value >= 0.0f);
    REQUIRE(value < 1.0f);
    lowest = value < lowest ? value : lowest;
    highest = value > highest ? value : highest;
    lowerHalf += value < 0.5f ? 1 : 0;
  }

  CHECK(lowest < 0.01f);
  CHECK(highest > 0.99f);
  CHECK(lowerHalf > DRAW_COUNT * 45 / 100);
  CHECK(lowerHalf < DRAW_COUNT * 55 / 100);
}

TEST_CASE("Последовательность повторяется после rewind и зависит от модуля и голоса (D-105)") {
  Random first;
  Random second;

  first.setSeed(Random::makeSeed(3, 1));
  second.setSeed(Random::makeSeed(3, 1));

  const std::vector<float> reference = draw(first, 16);

  CHECK(draw(second, 16) == reference);

  draw(first, 5);
  first.rewind();
  CHECK(draw(first, 16) == reference);

  Random otherModule;
  Random otherVoice;

  otherModule.setSeed(Random::makeSeed(4, 1));
  otherVoice.setSeed(Random::makeSeed(3, 2));
  CHECK(draw(otherModule, 16) != reference);
  CHECK(draw(otherVoice, 16) != reference);
}
