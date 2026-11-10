#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>

#include "../../gui/waveScreen/waveScreen.h"

namespace {
  constexpr int STEPS = 64;

  bool isJump(const WaveLine& line) {
    return line.size() == 2 && line[0].x == line[1].x;
  }

  int countJumps(const std::vector<WaveLine>& lines) {
    int count = 0;

    for (const WaveLine& line : lines) {
      count += isJump(line) ? 1 : 0;
    }

    return count;
  }
}

TEST_CASE("Экран проходит ровно период волны, точки лежат на её форме (D-108)") {
  for (const WAVE_TYPE type : { WAVE_TYPE::SIN, WAVE_TYPE::SQR, WAVE_TYPE::SAW, WAVE_TYPE::TNG }) {
    for (const float offset : { 0.0f, 0.1f, 0.25f, 0.5f, 0.8f, 0.999f }) {
      CAPTURE((int)type);
      CAPTURE(offset);

      const std::vector<WaveLine> lines = traceWave(type, offset, STEPS);

      REQUIRE_FALSE(lines.empty());
      CHECK(lines.front().front().x == 0.0f);
      CHECK(lines.back().back().x == 1.0f);

      float lastX = 0.0f;

      for (const WaveLine& line : lines) {
        // Линии идут слева направо и стыкуются без щелей
        CHECK(line.front().x == doctest::Approx(lastX));
        lastX = line.back().x;

        for (size_t index = 0; index < line.size(); index++) {
          const Rml::Vector2f point = line[index];

          CHECK(point.y >= -1.0f);
          CHECK(point.y <= 1.0f);

          if (index > 0) {
            CHECK(point.x >= line[index - 1].x);
          }

          if (!isJump(line) && index > 0 && index + 1 < line.size()) {
            const float phase = point.x + offset - std::floor(point.x + offset);

            CHECK(point.y == doctest::Approx(shapeWave(type, phase)).epsilon(1e-3));
          }
        }
      }
    }
  }
}

TEST_CASE("Скачки пилы и меандра — вертикальные отрезки, изломы треугольника не рвут линию (D-108)") {
  SUBCASE("пила без сдвига — одна линия снизу вверх") {
    const std::vector<WaveLine> lines = traceWave(WAVE_TYPE::SAW, 0.0f, STEPS);

    REQUIRE(lines.size() == 1);
    CHECK(lines[0].front().y == doctest::Approx(-1.0f).epsilon(1e-3));
    CHECK(lines[0].back().y == doctest::Approx(1.0f).epsilon(1e-3));
  }

  SUBCASE("пила со сдвигом на четверть — скачок там, где фаза кончается") {
    const std::vector<WaveLine> lines = traceWave(WAVE_TYPE::SAW, 0.25f, STEPS);

    REQUIRE(lines.size() == 3);
    REQUIRE(isJump(lines[1]));
    CHECK(lines[1][0].x == doctest::Approx(0.75f));
    CHECK(lines[1][0].y == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(lines[1][1].y == doctest::Approx(-1.0f).epsilon(1e-3));
    CHECK(lines[0].front().y == doctest::Approx(-0.5f).epsilon(1e-3));
  }

  SUBCASE("меандр — скачок посередине периода") {
    const std::vector<WaveLine> lines = traceWave(WAVE_TYPE::SQR, 0.0f, STEPS);

    CHECK(countJumps(lines) == 1);
    CHECK(lines.front().front().y == 1.0f);
    CHECK(lines.back().back().y == -1.0f);
  }

  SUBCASE("меандр со сдвигом — два скачка внутри экрана") {
    CHECK(countJumps(traceWave(WAVE_TYPE::SQR, 0.3f, STEPS)) == 2);
  }

  SUBCASE("треугольник — одна линия, вершина не срезана") {
    const std::vector<WaveLine> lines = traceWave(WAVE_TYPE::TNG, 0.3f, STEPS);

    REQUIRE(lines.size() == 1);

    float peak = -1.0f;

    for (const Rml::Vector2f point : lines[0]) {
      peak = std::fmax(peak, point.y);
    }

    CHECK(peak == doctest::Approx(1.0f).epsilon(1e-3));
  }

  SUBCASE("синус — одна линия без скачков") {
    CHECK(traceWave(WAVE_TYPE::SIN, 0.4f, STEPS).size() == 1);
  }
}

TEST_CASE("Сдвиг фазы двигает волну влево: в начале экрана то, что было на сдвиге (D-105, D-108)") {
  const std::vector<WaveLine> plain = traceWave(WAVE_TYPE::SIN, 0.0f, STEPS);
  const std::vector<WaveLine> shifted = traceWave(WAVE_TYPE::SIN, 0.25f, STEPS);

  CHECK(plain.front().front().y == doctest::Approx(0.0f).epsilon(1e-3));
  CHECK(shifted.front().front().y == doctest::Approx(1.0f).epsilon(1e-3));
}
