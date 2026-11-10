#include <doctest/doctest.h>

#include <RmlUi/Core.h>

#include <cmath>
#include <map>
#include <string>

#include "../../gui/gridDecorator/gridDecorator.h"
#include "./editorFixture.h"

namespace {
  constexpr float EPSILON = 0.01f;

  /**
   * Сколько в остатке после целого числа периодов сетки: ноль — рисунок совпадает
   */
  float periodRest(float distance, float period) {
    const float rest = std::fmod(std::fabs(distance), period);

    return std::min(rest, period - rest);
  }

  /**
   * Вертикальные линии сетки: левый край светлой полоски и её прозрачность.
   * Светлая полоска узнаётся по цвету: у тени он чёрный
   */
  std::map<float, int> findVerticalLines(const Rml::Mesh& mesh, float height) {
    std::map<float, int> lines;

    for (size_t index = 0; index + 3 < mesh.vertices.size(); index += 4) {
      const Rml::Vertex& first = mesh.vertices[index];
      const Rml::Vertex& third = mesh.vertices[index + 2];
      const bool isVertical = first.position.y == 0.0f && third.position.y == height;

      if (isVertical && first.colour.blue > 0) {
        lines[first.position.x] = first.colour.alpha;
      }
    }

    return lines;
  }
}

TEST_CASE("Слой сетки сдвигается не дальше одного периода, а её линии идут вместе со схемой") {
  for (const float pan : { 0.0f, 10.0f, -10.0f, GRID_PERIOD_DP, -GRID_PERIOD_DP, 1000.5f, -1000.5f, 37.25f }) {
    const Rml::Vector2f offset = getGridOffset(Rml::Vector2f(pan, -pan));

    CAPTURE(pan);
    CHECK(offset.x <= 0.0f);
    CHECK(offset.x > -GRID_PERIOD_DP);
    CHECK(offset.y <= 0.0f);
    CHECK(offset.y > -GRID_PERIOD_DP);
    CHECK(periodRest(pan - offset.x, GRID_PERIOD_DP) < EPSILON);
    CHECK(periodRest(-pan - offset.y, GRID_PERIOD_DP) < EPSILON);
  }
}

TEST_CASE("Линии сетки идут с шагом клетки, каждая GRID_MAJOR_EVERY-я ярче") {
  const Rml::Vector2f size(GRID_PERIOD_DP * 3.0f, 100.0f);
  const std::map<float, int> lines = findVerticalLines(buildGrid(size, 1.0f), size.y);

  REQUIRE(lines.size() == (size_t)(GRID_MAJOR_EVERY * 3));

  int index = 0;

  for (const auto& [x, alpha] : lines) {
    CAPTURE(index);
    CHECK(x == doctest::Approx(GRID_CELL_DP * (float)index));

    if (index % GRID_MAJOR_EVERY == 0) {
      CHECK(alpha > lines.at(GRID_CELL_DP));
    } else {
      CHECK(alpha == lines.at(GRID_CELL_DP));
    }

    index++;
  }
}

TEST_CASE("При дробном масштабе линии стоят на целых пикселях и не расплываются") {
  const float scale = 1.25f;
  const Rml::Vector2f size(GRID_PERIOD_DP * scale * 2.0f, 50.0f);
  const Rml::Mesh mesh = buildGrid(size, scale);

  REQUIRE_FALSE(mesh.vertices.empty());

  for (const Rml::Vertex& vertex : mesh.vertices) {
    CHECK(vertex.position.x == std::round(vertex.position.x));
    CHECK(vertex.position.y == std::round(vertex.position.y));
  }

  const std::map<float, int> lines = findVerticalLines(mesh, size.y);

  CHECK(lines.size() == (size_t)(GRID_MAJOR_EVERY * 2));
  CHECK(lines.rbegin()->first == doctest::Approx(GRID_CELL_DP * scale * (float)(GRID_MAJOR_EVERY * 2 - 1)).epsilon(0.02));
}

TEST_CASE_FIXTURE(EditorFixture, "Сетка холста закрывает весь холст и сдвигается вместе со схемой") {
  Rml::Element* canvas = this->get("#canvas");
  Rml::Element* grid = this->get("#grid");
  Rml::Element* world = this->get("#world");
  const Rml::DecoratorsPtr decorators = this->decorators(grid);

  REQUIRE(decorators);
  REQUIRE(decorators->list.size() == 1);
  CHECK(decorators->list[0].type == "grid");

  const auto checkGrid = [&] {
    const Rml::Vector2f canvasOffset = canvas->GetAbsoluteOffset(Rml::BoxArea::Padding);
    const Rml::Vector2f gridOffset = grid->GetAbsoluteOffset(Rml::BoxArea::Border) - canvasOffset;
    const Rml::Vector2f gridSize = grid->GetBox().GetSize(Rml::BoxArea::Padding);
    const Rml::Vector2f canvasSize = canvas->GetBox().GetSize(Rml::BoxArea::Padding);
    const Rml::Vector2f worldOffset = world->GetAbsoluteOffset(Rml::BoxArea::Border) - canvasOffset;

    // Отступ слоя в стилях равен периоду рисунка из кода
    CHECK(gridSize.x == doctest::Approx(canvasSize.x + GRID_PERIOD_DP));
    CHECK(gridSize.y == doctest::Approx(canvasSize.y + GRID_PERIOD_DP));
    CHECK(gridOffset.x <= 0.0f);
    CHECK(gridOffset.y <= 0.0f);
    CHECK(gridOffset.x + gridSize.x >= canvasSize.x);
    CHECK(gridOffset.y + gridSize.y >= canvasSize.y);
    CHECK(periodRest(worldOffset.x - gridOffset.x, GRID_PERIOD_DP) < EPSILON);
    CHECK(periodRest(worldOffset.y - gridOffset.y, GRID_PERIOD_DP) < EPSILON);
  };

  checkGrid();

  const Rml::Vector2f free = canvas->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(900.0f, 400.0f);

  this->dragTo(free, free + Rml::Vector2f(-40.0f, 30.0f));
  checkGrid();
  this->dragTo(free, free + Rml::Vector2f(-100.0f, 75.0f));
  checkGrid();
}
