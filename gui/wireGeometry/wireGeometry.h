#pragma once

#include <array>

struct WirePoint {
  float x;
  float y;

  bool operator==(const WirePoint&) const = default;
};

inline constexpr int WIRE_SEGMENTS = 48;

using WireCurve = std::array<WirePoint, WIRE_SEGMENTS + 1>;

/**
 * Кривая Безье провода: выходит из порта вправо и входит в цель слева
 */
WireCurve makeWireCurve(WirePoint start, WirePoint end);

/**
 * Расстояние от точки до провода: по нему определяется, на какой провод попал клик
 */
float measureDistance(const WireCurve& curve, WirePoint point);
