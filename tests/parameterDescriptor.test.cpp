#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "../engine/parameterDescriptor/parameterDescriptor.h"

namespace {
  const ParameterDescriptor CUTOFF = {
    .key = "cutoff",
    .name = "Cutoff",
    .minValue = 20.0f,
    .maxValue = 20000.0f,
    .defaultValue = 1000.0f,
    .curve = PARAM_CURVE::LOG,
    .unit = "Hz",
    .isModulatable = true,
  };

  const ParameterDescriptor GAIN = {
    .key = "gain",
    .name = "Gain",
    .minValue = -24.0f,
    .maxValue = 6.0f,
    .defaultValue = 0.0f,
    .unit = "dB",
  };

  const ParameterDescriptor WAVE = {
    .key = "wave",
    .name = "Wave",
    .minValue = 1.0f,
    .maxValue = 4.0f,
    .defaultValue = 3.0f,
    .isStepped = true,
    .isModulatable = true,
  };

  /**
   * Копия корректного описания с одним испорченным полем
   */
  template <typename Change>
  ParameterDescriptor broken(ParameterDescriptor parameter, Change change) {
    change(parameter);
    return parameter;
  }
}

TEST_CASE("Линейная кривая переводит 0..1 в диапазон равномерно") {
  CHECK(toReal(GAIN, 0.0f) == -24.0f);
  CHECK(toReal(GAIN, 0.5f) == doctest::Approx(-9.0f));
  CHECK(toReal(GAIN, 1.0f) == 6.0f);

  CHECK(toNormalized(GAIN, -24.0f) == 0.0f);
  CHECK(toNormalized(GAIN, -9.0f) == doctest::Approx(0.5f));
  CHECK(toNormalized(GAIN, 6.0f) == 1.0f);
}

TEST_CASE("Логарифмическая кривая даёт равные шаги по октавам") {
  CHECK(toReal(CUTOFF, 0.0f) == 20.0f);
  CHECK(toReal(CUTOFF, 1.0f) == 20000.0f);

  // Середина логарифмического диапазона — среднее геометрическое границ
  CHECK(toReal(CUTOFF, 0.5f) == doctest::Approx(std::sqrt(20.0f * 20000.0f)));
  CHECK(toNormalized(CUTOFF, 20.0f) == 0.0f);
  CHECK(toNormalized(CUTOFF, 20000.0f) == doctest::Approx(1.0f));
}

TEST_CASE("toNormalized обратна toReal") {
  for (const ParameterDescriptor* parameter : { &CUTOFF, &GAIN }) {
    for (int i = 0; i <= 100; i++) {
      const float normalized = (float)i / 100.0f;

      CAPTURE(parameter->key);
      CAPTURE(normalized);
      CHECK(toNormalized(*parameter, toReal(*parameter, normalized)) == doctest::Approx(normalized).epsilon(0.0001));
    }
  }
}

TEST_CASE("Значения вне диапазона ограничиваются") {
  CHECK(toReal(GAIN, -1.0f) == -24.0f);
  CHECK(toReal(GAIN, 2.0f) == 6.0f);
  CHECK(toNormalized(CUTOFF, 1.0f) == 0.0f);
  CHECK(toNormalized(CUTOFF, 100000.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Ступенчатый параметр принимает только целые значения") {
  CHECK(toReal(WAVE, 0.0f) == 1.0f);
  CHECK(toReal(WAVE, 0.4f) == 2.0f);
  CHECK(toReal(WAVE, 0.6f) == 3.0f);
  CHECK(toReal(WAVE, 1.0f) == 4.0f);
}

TEST_CASE("validateParameter принимает корректные описания") {
  CHECK(validateParameter(CUTOFF).has_value());
  CHECK(validateParameter(GAIN).has_value());
  CHECK(validateParameter(WAVE).has_value());
}

TEST_CASE("validateParameter отклоняет некорректные описания") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float infinity = std::numeric_limits<float>::infinity();

  CHECK_FALSE(validateParameter(broken(GAIN, [](auto& p) { p.key = ""; })).has_value());
  CHECK_FALSE(validateParameter(broken(GAIN, [](auto& p) { p.name = ""; })).has_value());
  CHECK_FALSE(validateParameter(broken(GAIN, [](auto& p) { p.minValue = 6.0f; })).has_value());
  CHECK_FALSE(validateParameter(broken(GAIN, [&](auto& p) { p.minValue = nan; })).has_value());
  CHECK_FALSE(validateParameter(broken(GAIN, [&](auto& p) { p.maxValue = infinity; })).has_value());
  CHECK_FALSE(validateParameter(broken(GAIN, [](auto& p) { p.defaultValue = 7.0f; })).has_value());
  CHECK_FALSE(validateParameter(broken(GAIN, [&](auto& p) { p.defaultValue = nan; })).has_value());
  CHECK_FALSE(validateParameter(broken(CUTOFF, [](auto& p) { p.minValue = 0.0f; })).has_value());
  CHECK_FALSE(validateParameter(broken(WAVE, [](auto& p) { p.defaultValue = 2.5f; })).has_value());
}
