#include <doctest/doctest.h>

#include <RmlUi/Core.h>

#include <array>
#include <cmath>

#include "../../gui/meshBuilder/meshBuilder.h"

namespace {
  constexpr int STEPS = 96;
  constexpr int ROWS = 3; // Внутренний край, внешний край, конец мягкого края

  const Rml::Vector2f CENTER(20.0f, 30.0f);

  const Rml::Vertex& vertexAt(const Rml::Mesh& mesh, int step, int row) {
    return mesh.vertices[(size_t)(step * ROWS + row)];
  }

  float distance(Rml::Vector2f a, Rml::Vector2f b) {
    return (a - b).Magnitude();
  }
}

TEST_CASE("Кольцо замыкается без щели и лежит на своих радиусах") {
  const Rml::Colourb color(100, 110, 120);
  MeshBuilder builder;

  builder.addRing({ .center = CENTER, .innerRadius = 5.0f, .outerRadius = 10.0f, .feather = 3.0f, .steps = STEPS }, { &color, 1 }, { &color, 1 });

  const Rml::Mesh mesh = builder.takeMesh();

  REQUIRE(mesh.vertices.size() == (size_t)(STEPS + 1) * ROWS);
  CHECK(mesh.indices.size() == (size_t)STEPS * (ROWS - 1) * 6);

  // Направление крутится умножением: без точного возврата на первую вершину накопилась бы щель
  for (int row = 0; row < ROWS; row++) {
    CHECK(vertexAt(mesh, STEPS, row).position == vertexAt(mesh, 0, row).position);
  }

  for (int step = 0; step <= STEPS; step++) {
    CHECK(distance(vertexAt(mesh, step, 0).position, CENTER) == doctest::Approx(5.0f).epsilon(0.001));
    CHECK(distance(vertexAt(mesh, step, 1).position, CENTER) == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(distance(vertexAt(mesh, step, 2).position, CENTER) == doctest::Approx(13.0f).epsilon(0.001));
  }

  // Угол шага совпадает с тем, по которому считают таблицы цветов
  const float angle = MeshBuilder::getRingAngle(STEPS / 4, STEPS);
  const Rml::Vector2f expected = CENTER + Rml::Vector2f(std::cos(angle), std::sin(angle)) * 10.0f;

  CHECK(distance(vertexAt(mesh, STEPS / 4, 1).position, expected) < 0.001f);
}

TEST_CASE("Кольцо берёт цвет шага из таблицы, мягкий край уходит в прозрачность") {
  std::array<Rml::Colourb, STEPS + 1> inner;
  std::array<Rml::Colourb, STEPS + 1> outer;

  for (int step = 0; step <= STEPS; step++) {
    inner[step] = Rml::Colourb((Rml::byte)step, 0, 0, 255);
    outer[step] = Rml::Colourb(0, (Rml::byte)step, 0, 200);
  }

  MeshBuilder builder;

  builder.addRing({ .center = CENTER, .outerRadius = 10.0f, .steps = STEPS }, inner, outer);

  const Rml::Mesh mesh = builder.takeMesh();

  REQUIRE(mesh.vertices.size() == (size_t)(STEPS + 1) * ROWS);

  for (int step = 0; step <= STEPS; step++) {
    // Круг: внутренние вершины в центре, у каждого сектора там свой цвет, как у conic-gradient
    CHECK(vertexAt(mesh, step, 0).position == CENTER);
    CHECK(vertexAt(mesh, step, 0).colour == inner[step].ToPremultiplied(1.0f));
    CHECK(vertexAt(mesh, step, 1).colour == outer[step].ToPremultiplied(1.0f));
    CHECK(vertexAt(mesh, step, 2).colour.alpha == 0);
  }
}

TEST_CASE("Короткая таблица цветов кольца не выводит за границы") {
  const std::array<Rml::Colourb, 3> colors = { Rml::Colourb(10, 0, 0), Rml::Colourb(20, 0, 0), Rml::Colourb(30, 0, 0) };
  MeshBuilder builder;

  builder.addRing({ .center = CENTER, .outerRadius = 10.0f, .steps = 8 }, colors, {});

  const Rml::Mesh mesh = builder.takeMesh();

  REQUIRE(mesh.vertices.size() == (size_t)9 * ROWS);
  CHECK(vertexAt(mesh, 1, 0).colour == colors[1].ToPremultiplied(1.0f));
  CHECK(vertexAt(mesh, 8, 0).colour == colors[2].ToPremultiplied(1.0f));
  CHECK(vertexAt(mesh, 8, 1).colour.alpha == 0);
}
