#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "../tools/goldenScenarios/goldenScenarios.h"

TEST_CASE("Звук совпадает с эталонами (D-072)") {
  for (const GoldenScenario& scenario : getGoldenScenarios()) {
    CAPTURE(scenario.name);

    const std::optional<WavFile> golden = readWav(getGoldenPath(scenario, SINT_SOURCE_DIR).string());
    REQUIRE_MESSAGE(golden.has_value(), "эталона нет или файл повреждён");
    CHECK(isAudible(golden->audio));

    const auto rendered = renderGolden(scenario, SINT_SOURCE_DIR);
    REQUIRE_MESSAGE(rendered.has_value(), rendered.error());

    const GoldenDifference difference = compareGolden(*golden, *rendered);

    CHECK_MESSAGE(
      difference.amount <= GOLDEN_TOLERANCE,
      "разница ", difference.amount, " на кадре ", difference.frame,
      ". Если звук изменён намеренно: cmake --build build --target update_golden"
    );
  }
}

TEST_CASE("compareGolden находит наибольшую разницу в обоих каналах") {
  const WavFile expected = { { { 0.1f, 0.2f, 0.3f }, { 0.4f, 0.5f, 0.6f } }, 48000 };

  CHECK(compareGolden(expected, expected).amount == 0.0f);

  WavFile changed = expected;
  changed.audio.left[1] += 0.01f;
  changed.audio.right[2] -= 0.05f;

  const GoldenDifference difference = compareGolden(expected, changed);

  CHECK(difference.amount == doctest::Approx(0.05f));
  CHECK(difference.frame == 2);
}

TEST_CASE("compareGolden не принимает разный формат и нечисловой сэмпл") {
  const float noMatch = std::numeric_limits<float>::infinity();
  const WavFile expected = { { { 0.1f, 0.2f }, { 0.3f, 0.4f } }, 48000 };
  WavFile changed = expected;

  SUBCASE("Частота дискретизации") {
    changed.sampleRate = 44100;
  }

  SUBCASE("Длина левого канала") {
    changed.audio.left.pop_back();
  }

  SUBCASE("Длина правого канала") {
    changed.audio.right.push_back(0.0f);
  }

  SUBCASE("NaN в начале левого канала") {
    changed.audio.left[0] = std::nanf("");
  }

  SUBCASE("NaN в конце правого канала") {
    changed.audio.right[1] = std::nanf("");
  }

  CHECK(compareGolden(expected, changed).amount == noMatch);
}

TEST_CASE("isAudible отвергает тишину и нечисловые сэмплы") {
  CHECK(isAudible({ { 0.0f, 0.5f }, { 0.0f, 0.0f } }));
  CHECK(isAudible({ { 0.0f, 0.0f }, { 0.0f, -0.5f } }));
  CHECK_FALSE(isAudible({ { 0.0f, 0.001f }, { 0.0f, 0.0f } }));
  CHECK_FALSE(isAudible({ { 0.5f, std::nanf("") }, { 0.0f, 0.0f } }));
  CHECK_FALSE(isAudible({ { 0.5f, 0.0f }, { 0.0f, std::numeric_limits<float>::infinity() } }));
}

TEST_CASE("Для всех сценариев эталонов есть свои имена и ноты") {
  const auto& scenarios = getGoldenScenarios();

  for (size_t index = 0; index < scenarios.size(); index++) {
    CHECK_FALSE(scenarios[index].notes.empty());
    CHECK(scenarios[index].frameCount > 0);

    for (size_t other = index + 1; other < scenarios.size(); other++) {
      CHECK(scenarios[index].name != scenarios[other].name);
    }
  }
}
