#include <doctest/doctest.h>

#include <cmath>

#include "../engine/filter/filter.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
}

TEST_CASE("Фильтр пропускает постоянный сигнал") {
  Filter filter;
  filter.setSampleRate(SAMPLE_RATE);

  float out = 0.0f;
  for (int i = 0; i < (int)SAMPLE_RATE; i++) {
    out = filter.calculateOut(1.0f);
  }

  CHECK(out == doctest::Approx(1.0f));
}

TEST_CASE("Фильтр подавляет высокие частоты") {
  Filter filter;
  filter.setSampleRate(SAMPLE_RATE);

  // Чередование +1 и -1 — самая высокая частота, которую можно записать
  float peak = 0.0f;
  for (int i = 0; i < (int)SAMPLE_RATE; i++) {
    const float out = filter.calculateOut(i % 2 == 0 ? 1.0f : -1.0f);

    if (i > (int)SAMPLE_RATE / 2) {
      peak = std::fmax(peak, std::fabs(out));
    }
  }

  CHECK(peak < 0.05f);
}
