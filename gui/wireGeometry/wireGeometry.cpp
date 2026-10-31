#include "./wireGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
  // Провод должен заметно отходить от порта, даже когда блоки стоят рядом
  constexpr float MIN_BEND = 60.0f;

  WirePoint interpolate(WirePoint start, WirePoint control1, WirePoint control2, WirePoint end, float t) {
    const float u = 1.0f - t;
    const float a = u * u * u;
    const float b = 3.0f * u * u * t;
    const float c = 3.0f * u * t * t;
    const float d = t * t * t;

    return {
      a * start.x + b * control1.x + c * control2.x + d * end.x,
      a * start.y + b * control1.y + c * control2.y + d * end.y,
    };
  }

  float measureSegment(WirePoint first, WirePoint second, WirePoint point) {
    const float dx = second.x - first.x;
    const float dy = second.y - first.y;
    const float lengthSquared = dx * dx + dy * dy;
    const float t = lengthSquared > 0.0f
      ? std::clamp(((point.x - first.x) * dx + (point.y - first.y) * dy) / lengthSquared, 0.0f, 1.0f)
      : 0.0f;

    return std::hypot(first.x + t * dx - point.x, first.y + t * dy - point.y);
  }
}

WireCurve makeWireCurve(WirePoint start, WirePoint end) {
  const float bend = std::max(MIN_BEND, std::fabs(end.x - start.x) * 0.5f);
  const WirePoint control1 = { start.x + bend, start.y };
  const WirePoint control2 = { end.x - bend, end.y };
  WireCurve curve{};

  for (int index = 0; index <= WIRE_SEGMENTS; index++) {
    curve[(size_t)index] = interpolate(start, control1, control2, end, (float)index / WIRE_SEGMENTS);
  }

  return curve;
}

float measureDistance(const WireCurve& curve, WirePoint point) {
  float distance = std::numeric_limits<float>::max();

  for (size_t index = 0; index + 1 < curve.size(); index++) {
    distance = std::min(distance, measureSegment(curve[index], curve[index + 1], point));
  }

  return distance;
}
