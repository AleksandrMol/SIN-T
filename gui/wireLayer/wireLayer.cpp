#include "./wireLayer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

#include "../meshBuilder/meshBuilder.h"

namespace {
  const Rml::Colourb AUDIO_COLOR(122, 162, 247);
  const Rml::Colourb MODULATION_COLOR(255, 158, 100);
  const Rml::Colourb SELECTED_COLOR(255, 255, 255);

  constexpr float DASH_LENGTH = 7.0f;
  constexpr float GAP_LENGTH = 5.0f;
  constexpr float ARROW_LENGTH = 8.0f;
  constexpr float ARROW_WIDTH = 4.5f;

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

  void drawArrow(MeshBuilder& builder, const WireCurve& curve, Rml::Colourb color, float scale) {
    const WirePoint tip = curve.back();
    const WirePoint before = curve[curve.size() - 3];
    const Rml::Vector2f direction = Rml::Vector2f(tip.x - before.x, tip.y - before.y).Normalise();
    const Rml::Vector2f normal(-direction.y, direction.x);
    const Rml::Vector2f end(tip.x, tip.y);
    const Rml::Vector2f base = end - direction * (ARROW_LENGTH * scale);

    builder.addTriangle(end, base + normal * (ARROW_WIDTH * scale), base - normal * (ARROW_WIDTH * scale), color);
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
    const Rml::Colourb color = isSelected ? SELECTED_COLOR : kind == WIRE_KIND::AUDIO ? AUDIO_COLOR : MODULATION_COLOR;
    std::vector<Rml::Vector2f> points;

    points.reserve(curve.size());

    for (const WirePoint& point : curve) {
      points.emplace_back(point.x, point.y);
    }

    const float width = (isSelected ? 3.0f : 2.0f) * scale;

    if (kind == WIRE_KIND::AUDIO) {
      builder.addLine(points, width, color);
    } else {
      drawDashes(builder, points, width, color, scale);
    }

    drawArrow(builder, curve, color, scale);
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

void WireLayer::OnRender() {
  if (!this->GetRenderManager() || (this->wires.empty() && !this->draftFrom)) {
    return;
  }

  const float scale = this->GetContext()->GetDensityIndependentPixelRatio();
  MeshBuilder builder;

  for (const Wire& wire : this->wires) {
    drawWire(builder, makeCurve(findCenter(wire.from), findEnd(wire.to)), wire.kind, wire.isSelected, scale);
  }

  if (this->draftFrom) {
    drawWire(builder, makeCurve(findCenter(this->draftFrom), this->draftPoint), this->draftKind, false, scale);
  }

  Rml::Geometry geometry = this->GetRenderManager()->MakeGeometry(builder.takeMesh());

  geometry.Render(Rml::Vector2f(0.0f, 0.0f));
}
