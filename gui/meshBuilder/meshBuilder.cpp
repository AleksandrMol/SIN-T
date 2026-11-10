#include "./meshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace {
  constexpr float FEATHER = 1.0f;
  constexpr int ROWS = 4;
  constexpr float ARC_STEP = 0.12f;

  Rml::Vector2f findNormal(std::span<const Rml::Vector2f> points, size_t index) {
    const Rml::Vector2f before = points[index == 0 ? 0 : index - 1];
    const Rml::Vector2f after = points[std::min(index + 1, points.size() - 1)];
    const Rml::Vector2f direction = after - before;

    return Rml::Vector2f(-direction.y, direction.x).Normalise();
  }
}

MeshBuilder::MeshBuilder(float opacity, Rml::Vector2f textureScale) : opacity(opacity), textureScale(textureScale) {}

int MeshBuilder::addVertex(Rml::Vector2f position, Rml::Colourb color, float alpha) {
  Rml::Vertex vertex;

  vertex.position = position;
  vertex.colour = color.ToPremultiplied(alpha * this->opacity);
  vertex.tex_coord = Rml::Vector2f(position.x * this->textureScale.x, position.y * this->textureScale.y);
  this->mesh.vertices.push_back(vertex);

  return (int)this->mesh.vertices.size() - 1;
}

void MeshBuilder::addLine(std::span<const Rml::Vector2f> points, float width, Rml::Colourb color) {
  if (points.size() < 2) {
    return;
  }

  const float inner = std::max(width * 0.5f - FEATHER * 0.5f, 0.0f);
  const float offsets[ROWS] = { -inner - FEATHER, -inner, inner, inner + FEATHER };
  const float alphas[ROWS] = { 0.0f, 1.0f, 1.0f, 0.0f };
  const int first = (int)this->mesh.vertices.size();

  for (size_t index = 0; index < points.size(); index++) {
    const Rml::Vector2f normal = findNormal(points, index);

    for (int row = 0; row < ROWS; row++) {
      this->addVertex(points[index] + normal * offsets[row], color, alphas[row]);
    }

    if (index + 1 == points.size()) {
      continue;
    }

    for (int row = 0; row < ROWS - 1; row++) {
      const int current = first + (int)index * ROWS + row;
      const int next = current + ROWS;

      this->mesh.indices.insert(this->mesh.indices.end(), { current, next, current + 1, current + 1, next, next + 1 });
    }
  }
}

void MeshBuilder::addTriangle(Rml::Vector2f first, Rml::Vector2f second, Rml::Vector2f third, Rml::Colourb color) {
  const int a = this->addVertex(first, color, 1.0f);
  const int b = this->addVertex(second, color, 1.0f);
  const int c = this->addVertex(third, color, 1.0f);

  this->mesh.indices.insert(this->mesh.indices.end(), { a, b, c });
}

void MeshBuilder::addRectangle(Rml::Vector2f from, Rml::Vector2f to, Rml::Colourb color) {
  const Rml::Vector2f points[4] = { from, Rml::Vector2f(to.x, from.y), to, Rml::Vector2f(from.x, to.y) };

  this->addConvexPolygon(points, single(color));
}

void MeshBuilder::addArc(Rml::Vector2f center, float radius, float startAngle, float endAngle, float width, Rml::Colourb color) {
  const int steps = std::max((int)(std::fabs(endAngle - startAngle) / ARC_STEP), 2);
  std::vector<Rml::Vector2f> points;

  points.reserve((size_t)steps + 1);

  for (int index = 0; index <= steps; index++) {
    const float angle = startAngle + (endAngle - startAngle) * (float)index / (float)steps;

    points.emplace_back(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
  }

  this->addLine(points, width, color);
}

float MeshBuilder::getRingAngle(int step, int steps) {
  return 2.0f * std::numbers::pi_v<float> * (float)step / (float)steps;
}

void MeshBuilder::addRing(const Ring& ring, std::span<const Rml::Colourb> innerColors, std::span<const Rml::Colourb> outerColors) {
  constexpr int ROW_COUNT = 3;
  const int steps = std::max(ring.steps, 3);
  const int first = (int)this->mesh.vertices.size();
  // Короткая таблица не выводит за границы: недостающие шаги берут последний цвет, пустая — прозрачный
  const auto pick = [](std::span<const Rml::Colourb> colors, int index) {
    return colors.empty() ? Rml::Colourb(0, 0, 0, 0) : colors[std::min((size_t)index, colors.size() - 1)];
  };

  // Направление поворачивается на шаг умножением, без синуса и косинуса на каждой вершине
  const float stepAngle = getRingAngle(1, steps);
  const float stepCos = std::cos(stepAngle);
  const float stepSin = std::sin(stepAngle);
  Rml::Vector2f direction(1.0f, 0.0f);

  this->mesh.vertices.reserve(this->mesh.vertices.size() + (size_t)(steps + 1) * ROW_COUNT);
  this->mesh.indices.reserve(this->mesh.indices.size() + (size_t)steps * (ROW_COUNT - 1) * 6);

  for (int index = 0; index <= steps; index++) {
    // Последняя вершина ровно на первой, иначе накопленная погрешность оставила бы щель
    if (index == steps) {
      direction = Rml::Vector2f(1.0f, 0.0f);
    }

    const Rml::Colourb outer = pick(outerColors, index);

    this->addVertex(ring.center + direction * ring.innerRadius, pick(innerColors, index), 1.0f);
    this->addVertex(ring.center + direction * ring.outerRadius, outer, 1.0f);
    this->addVertex(ring.center + direction * (ring.outerRadius + ring.feather), outer, 0.0f);

    direction = Rml::Vector2f(direction.x * stepCos - direction.y * stepSin, direction.x * stepSin + direction.y * stepCos);

    if (index == steps) {
      continue;
    }

    for (int row = 0; row < ROW_COUNT - 1; row++) {
      const int current = first + index * ROW_COUNT + row;
      const int next = current + ROW_COUNT;

      this->mesh.indices.insert(this->mesh.indices.end(), { current, next, current + 1, current + 1, next, next + 1 });
    }
  }
}

void MeshBuilder::addConvexPolygon(std::span<const Rml::Vector2f> points, std::span<const Rml::Colourb> colors) {
  if (points.size() < 3 || colors.empty()) {
    return;
  }

  const int first = (int)this->mesh.vertices.size();

  for (size_t index = 0; index < points.size(); index++) {
    this->addVertex(points[index], colors[std::min(index, colors.size() - 1)], 1.0f);
  }

  for (int index = 1; index + 1 < (int)points.size(); index++) {
    this->mesh.indices.insert(this->mesh.indices.end(), { first, first + index, first + index + 1 });
  }
}

Rml::Mesh MeshBuilder::takeMesh() {
  return std::exchange(this->mesh, {});
}
