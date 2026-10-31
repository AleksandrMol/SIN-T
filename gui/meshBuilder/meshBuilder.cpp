#include "./meshBuilder.h"

#include <algorithm>
#include <cmath>
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

MeshBuilder::MeshBuilder(float opacity) : opacity(opacity) {}

int MeshBuilder::addVertex(Rml::Vector2f position, Rml::Colourb color, float alpha) {
  Rml::Vertex vertex;

  vertex.position = position;
  vertex.colour = color.ToPremultiplied(alpha * this->opacity);
  vertex.tex_coord = Rml::Vector2f(0.0f, 0.0f);
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

Rml::Mesh MeshBuilder::takeMesh() {
  return std::exchange(this->mesh, {});
}
