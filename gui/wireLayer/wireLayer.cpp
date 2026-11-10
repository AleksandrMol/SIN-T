#include "./wireLayer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

#include "../colorTools/colorTools.h"
#include "../meshBuilder/meshBuilder.h"

namespace {
  const Rml::Colourb SELECTED_COLOR(255, 255, 255);
  // Тёмная подложка под проводом: он читается и на светлом металле блоков, и на линиях сетки
  const Rml::Colourb UNDERLAY_COLOR(0, 0, 0, 160);

  // Размеры в dp
  constexpr float WIRE_WIDTH = 3.0f;
  constexpr float SELECTED_WIDTH = 4.0f;
  constexpr float UNDERLAY = 1.5f;  // на столько подложка шире провода с каждой стороны
  constexpr float DASH_LENGTH = 8.0f;
  constexpr float GAP_LENGTH = 5.0f;
  constexpr float ARROW_LENGTH = 9.0f;
  constexpr float ARROW_WIDTH = 5.0f;

  Rml::Vector2f findCenter(Rml::Element* element) {
    return element->GetAbsoluteOffset(Rml::BoxArea::Border) + element->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
  }

  /**
   * Провод упирается в левый край цели, а не в её центр: стрелка и значение ручки остаются видны
   */
  Rml::Vector2f findEnd(Rml::Element* element) {
    return { element->GetAbsoluteOffset(Rml::BoxArea::Border).x, findCenter(element).y };
  }

  WireCurve makeCurve(Rml::Vector2f start, Rml::Vector2f end) {
    return makeWireCurve({ start.x, start.y }, { end.x, end.y });
  }

  /**
   * Стрелка у цели. grow расширяет её во все стороны: так рисуется подложка
   */
  void drawArrow(MeshBuilder& builder, const WireCurve& curve, Rml::Colourb color, float scale, float grow) {
    const WirePoint tip = curve.back();
    const WirePoint before = curve[curve.size() - 3];
    const Rml::Vector2f direction = Rml::Vector2f(tip.x - before.x, tip.y - before.y).Normalise();
    const Rml::Vector2f normal(-direction.y, direction.x);
    const Rml::Vector2f end = Rml::Vector2f(tip.x, tip.y) + direction * grow;
    const Rml::Vector2f base = Rml::Vector2f(tip.x, tip.y) - direction * (ARROW_LENGTH * scale + grow);
    const float halfWidth = ARROW_WIDTH * scale + grow;

    builder.addTriangle(end, base + normal * halfWidth, base - normal * halfWidth, color);
  }

  /**
   * Штрихи одной длины вдоль кривой: сегменты кривой разной длины, и по ним пунктир выходил бы рваным
   */
  void drawDashes(MeshBuilder& builder, std::span<const Rml::Vector2f> points, float width, Rml::Colourb color, float scale) {
    std::vector<Rml::Vector2f> dash = { points[0] };
    float left = DASH_LENGTH * scale;
    bool isDash = true;

    for (size_t index = 0; index + 1 < points.size(); index++) {
      Rml::Vector2f current = points[index];
      const Rml::Vector2f next = points[index + 1];

      while (true) {
        const float length = (next - current).Magnitude();

        if (length < left) {
          left -= length;
          break;
        }

        current = current + (next - current) * (left / length);

        if (isDash) {
          dash.push_back(current);
          builder.addLine(dash, width, color);
          dash.clear();
        } else {
          dash = { current };
        }

        isDash = !isDash;
        left = (isDash ? DASH_LENGTH : GAP_LENGTH) * scale;
      }

      if (isDash) {
        dash.push_back(next);
      }
    }

    builder.addLine(dash, width, color);
  }

  void drawWire(MeshBuilder& builder, const WireCurve& curve, WIRE_KIND kind, bool isSelected, float scale) {
    const Rml::Colourb color = isSelected ? SELECTED_COLOR : kind == WIRE_KIND::AUDIO ? ACCENT_COLOR : MODULATION_COLOR;
    std::vector<Rml::Vector2f> points;

    points.reserve(curve.size());

    for (const WirePoint& point : curve) {
      points.emplace_back(point.x, point.y);
    }

    const float width = (isSelected ? SELECTED_WIDTH : WIRE_WIDTH) * scale;
    const float grow = UNDERLAY * scale;

    // Сначала подложка, сверху провод. Пунктир с подложкой у каждого штриха, иначе он читался бы сплошной тёмной линией
    if (kind == WIRE_KIND::AUDIO) {
      builder.addLine(points, width + grow * 2.0f, UNDERLAY_COLOR);
      builder.addLine(points, width, color);
    } else {
      drawDashes(builder, points, width + grow * 2.0f, UNDERLAY_COLOR, scale);
      drawDashes(builder, points, width, color, scale);
    }

    drawArrow(builder, curve, UNDERLAY_COLOR, scale, grow);
    drawArrow(builder, curve, color, scale, 0.0f);
  }

  /**
   * Сетка провода, выходящего из нуля
   */
  Rml::Mesh makeShapeMesh(Rml::Vector2f end, WIRE_KIND kind, bool isSelected, float scale) {
    MeshBuilder builder;

    drawWire(builder, makeCurve({ 0.0f, 0.0f }, end), kind, isSelected, scale);

    return builder.takeMesh();
  }

  void appendMesh(Rml::Mesh& target, const Rml::Mesh& part, Rml::Vector2f offset) {
    const int first = (int)target.vertices.size();

    target.vertices.insert(target.vertices.end(), part.vertices.begin(), part.vertices.end());

    for (size_t index = (size_t)first; index < target.vertices.size(); index++) {
      target.vertices[index].position += offset;
    }

    for (const int vertex : part.indices) {
      target.indices.push_back(first + vertex);
    }
  }
}

void WireLayer::setWires(std::vector<Wire> wires) {
  this->wires = std::move(wires);
}

const std::vector<Wire>& WireLayer::getWires() const {
  return this->wires;
}

void WireLayer::selectWire(std::optional<size_t> index) {
  for (size_t current = 0; current < this->wires.size(); current++) {
    this->wires[current].isSelected = current == index;
  }
}

void WireLayer::setDraft(Rml::Element* from, WIRE_KIND kind) {
  this->draftFrom = from;
  this->draftKind = kind;
  this->draftPoint = findCenter(from);
}

void WireLayer::moveDraft(Rml::Vector2f point) {
  this->draftPoint = point;
}

void WireLayer::clearDraft() {
  this->draftFrom = nullptr;
}

std::optional<size_t> WireLayer::findWire(Rml::Vector2f point, float tolerance) const {
  std::optional<size_t> nearest;
  float nearestDistance = std::numeric_limits<float>::max();

  for (size_t index = 0; index < this->wires.size(); index++) {
    const Wire& wire = this->wires[index];
    const float distance = measureDistance(makeCurve(findCenter(wire.from), findEnd(wire.to)), { point.x, point.y });

    if (distance <= tolerance && distance < nearestDistance) {
      nearest = index;
      nearestDistance = distance;
    }
  }

  return nearest;
}

Rml::Vector2f WireLayer::findMiddle(size_t index) const {
  const Wire& wire = this->wires[index];
  const WireCurve curve = makeCurve(findCenter(wire.from), findEnd(wire.to));
  const WirePoint middle = curve[curve.size() / 2];

  return { middle.x, middle.y };
}

const Rml::Mesh& WireLayer::getMesh() const {
  static const Rml::Mesh EMPTY;

  // Геометрии нет, пока слой не рисовался или рисовать нечего
  return this->geometry ? this->geometry.GetMesh() : EMPTY;
}

Rml::Mesh WireLayer::buildMesh() const {
  const float scale = this->getScale();
  Rml::Mesh mesh;

  for (const WireShape& shape : this->collectShapes()) {
    appendMesh(mesh, makeShapeMesh(shape.end - shape.start, shape.kind, shape.isSelected, scale), shape.start);
  }

  return mesh;
}

uint64_t WireLayer::getShapedWireCount() const {
  return this->shapedWireCount;
}

uint64_t WireLayer::getAssembledMeshCount() const {
  return this->assembledMeshCount;
}

std::vector<WireLayer::WireShape> WireLayer::collectShapes() const {
  std::vector<WireShape> shapes;

  shapes.reserve(this->wires.size() + 1);

  for (const Wire& wire : this->wires) {
    shapes.push_back({ findCenter(wire.from), findEnd(wire.to), wire.kind, wire.isSelected });
  }

  if (this->draftFrom) {
    shapes.push_back({ findCenter(this->draftFrom), this->draftPoint, this->draftKind, false });
  }

  return shapes;
}

float WireLayer::getScale() const {
  return this->GetContext()->GetDensityIndependentPixelRatio();
}

/**
 * Как ключи в React: провода сравниваются с прошлым кадром, считаются только изменившиеся.
 * Если ничего не сдвинулось, рисуется готовая геометрия и общая сетка не собирается
 */
void WireLayer::syncGeometry() {
  const std::vector<WireShape> shapes = this->collectShapes();
  const float scale = this->getScale();
  bool isChanged = shapes.size() != this->shapedWires.size();

  if (scale != this->shapedScale) {
    this->shapedScale = scale;
    this->shapedWires.clear();
    isChanged = true;
  }

  this->shapedWires.resize(shapes.size());

  for (size_t index = 0; index < shapes.size(); index++) {
    const WireShape& shape = shapes[index];
    ShapedWire& shaped = this->shapedWires[index];
    const WireShape& known = shaped.shape;

    // Пустая сетка у нового провода: совпасть с записью по умолчанию он может, а готового куска у него нет
    if (shaped.mesh && shape == known) {
      continue;
    }

    isChanged = true;

    const bool isSameForm = shaped.mesh
      && shape.end - shape.start == known.end - known.start
      && shape.kind == known.kind
      && shape.isSelected == known.isSelected;

    if (!isSameForm) {
      shaped.mesh = makeShapeMesh(shape.end - shape.start, shape.kind, shape.isSelected, scale);
      this->shapedWireCount++;
    }

    shaped.shape = shape;
  }

  if (!isChanged) {
    return;
  }

  size_t vertexCount = 0;
  size_t indexCount = 0;

  for (const ShapedWire& shaped : this->shapedWires) {
    vertexCount += shaped.mesh.vertices.size();
    indexCount += shaped.mesh.indices.size();
  }

  Rml::Mesh mesh;

  mesh.vertices.reserve(vertexCount);
  mesh.indices.reserve(indexCount);

  for (const ShapedWire& shaped : this->shapedWires) {
    appendMesh(mesh, shaped.mesh, shaped.shape.start);
  }

  this->geometry = this->GetRenderManager()->MakeGeometry(std::move(mesh));
  this->assembledMeshCount++;
}

void WireLayer::OnRender() {
  if (!this->GetRenderManager()) {
    return;
  }

  this->syncGeometry();

  if (!this->shapedWires.empty()) {
    this->geometry.Render(Rml::Vector2f(0.0f, 0.0f));
  }
}
