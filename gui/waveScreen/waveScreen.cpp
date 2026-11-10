#include "./waveScreen.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "../meshBuilder/meshBuilder.h"

namespace {
  constexpr int STEPS = 96;

  // Значение у разрыва берётся чуть в стороне от него: в самой точке функция уже за скачком
  constexpr float EDGE = 1e-5f;

  // Соседние куски с такой разницей на стыке — одна линия с изломом, а не скачок
  constexpr float JOIN = 0.01f;

  // Отступ волны от верхнего и нижнего края экрана, dp
  constexpr float MARGIN = 6.0f;

  const Rml::Colourb GRID_COLOR(61, 141, 232, 26);
  const Rml::Colourb ZERO_COLOR(61, 141, 232, 56);
  const Rml::Colourb FILL_COLOR(61, 141, 232, 64);
  const Rml::Colourb FILL_EDGE_COLOR(61, 141, 232, 0);
  const Rml::Colourb GLOW_COLOR(61, 141, 232, 70);
  const Rml::Colourb LINE_COLOR(159, 196, 245);

  float wrap(float value) {
    return value - std::floor(value);
  }

  /**
   * Где по фазе волны стоят разрывы и изломы: на экране там нужна точка, иначе угол срежется
   */
  std::vector<float> findBreaks(WAVE_TYPE type) {
    switch (type) {
      case WAVE_TYPE::SIN:
        return {};
      case WAVE_TYPE::SAW:
        return { 0.0f };
      case WAVE_TYPE::SQR:
      case WAVE_TYPE::TNG:
        return { 0.0f, 0.5f };
    }

    return {};
  }

  /**
   * Заливка от линии волны до нуля: у линии плотнее, к нулю тает. Отрезок, пересекающий ноль, делится в точке пересечения
   */
  void addFill(MeshBuilder& builder, const WaveLine& line, Rml::Vector2f origin, Rml::Vector2f extent) {
    const auto toScreen = [&](Rml::Vector2f point) {
      return Rml::Vector2f(origin.x + point.x * extent.x, origin.y - point.y * extent.y);
    };
    const std::array<Rml::Colourb, 4> quadColors = { FILL_COLOR, FILL_COLOR, FILL_EDGE_COLOR, FILL_EDGE_COLOR };
    const std::array<Rml::Colourb, 3> triangleColors = { FILL_COLOR, FILL_EDGE_COLOR, FILL_EDGE_COLOR };

    for (size_t index = 0; index + 1 < line.size(); index++) {
      const Rml::Vector2f from = line[index];
      const Rml::Vector2f to = line[index + 1];

      if (to.x <= from.x) {
        continue;
      }

      const Rml::Vector2f fromBase = toScreen({ from.x, 0.0f });
      const Rml::Vector2f toBase = toScreen({ to.x, 0.0f });

      if (from.y * to.y >= 0.0f) {
        const std::array<Rml::Vector2f, 4> quad = { toScreen(from), toScreen(to), toBase, fromBase };

        builder.addConvexPolygon(quad, quadColors);
        continue;
      }

      const float share = from.y / (from.y - to.y);
      const Rml::Vector2f cross = toScreen({ from.x + (to.x - from.x) * share, 0.0f });
      const std::array<Rml::Vector2f, 3> first = { toScreen(from), cross, fromBase };
      const std::array<Rml::Vector2f, 3> second = { toScreen(to), toBase, cross };

      builder.addConvexPolygon(first, triangleColors);
      builder.addConvexPolygon(second, triangleColors);
    }
  }
}

std::vector<WaveLine> traceWave(WAVE_TYPE type, float phaseOffset, int steps) {
  const float offset = wrap(phaseOffset);
  const auto valueAt = [type, offset](float x) {
    return shapeWave(type, wrap(x + offset));
  };

  std::vector<float> bounds = { 0.0f, 1.0f };

  for (const float phase : findBreaks(type)) {
    const float x = wrap(phase - offset);

    if (x > EDGE && x < 1.0f - EDGE) {
      bounds.push_back(x);
    }
  }

  std::ranges::sort(bounds);

  std::vector<WaveLine> lines;
  WaveLine current;

  for (size_t index = 0; index + 1 < bounds.size(); index++) {
    const float start = bounds[index];
    const float end = bounds[index + 1];
    WaveLine piece = { { start, valueAt(start + EDGE) } };

    for (int step = 1; step < steps; step++) {
      const float x = (float)step / (float)steps;

      if (x > start + EDGE && x < end - EDGE) {
        piece.push_back({ x, valueAt(x) });
      }
    }

    piece.push_back({ end, valueAt(end - EDGE) });

    if (current.empty()) {
      current = std::move(piece);
      continue;
    }

    const Rml::Vector2f last = current.back();

    if (std::fabs(last.y - piece.front().y) < JOIN) {
      current.insert(current.end(), piece.begin() + 1, piece.end());
      continue;
    }

    lines.push_back(std::move(current));
    lines.push_back({ last, piece.front() });
    current = std::move(piece);
  }

  lines.push_back(std::move(current));

  return lines;
}

void WaveScreen::setShape(WAVE_TYPE type, float phaseOffset) {
  if (type != this->type || phaseOffset != this->phaseOffset) {
    this->type = type;
    this->phaseOffset = phaseOffset;
    this->isDirty = true;
  }
}

WAVE_TYPE WaveScreen::getType() const {
  return this->type;
}

float WaveScreen::getPhaseOffset() const {
  return this->phaseOffset;
}

void WaveScreen::OnResize() {
  this->isDirty = true;
}

/**
 * Слои снизу вверх: сетка по четвертям периода и линия нуля, заливка до нуля, свечение, сама линия
 */
void WaveScreen::rebuild() {
  const Rml::Vector2f size = this->GetBox().GetSize(Rml::BoxArea::Border);
  const float scale = this->GetContext() ? this->GetContext()->GetDensityIndependentPixelRatio() : 1.0f;
  const Rml::Vector2f origin(0.0f, size.y * 0.5f);
  const Rml::Vector2f extent(size.x, std::max(size.y * 0.5f - MARGIN * scale, 1.0f));

  MeshBuilder builder(this->GetComputedValues().opacity());

  for (int quarter = 1; quarter < 4; quarter++) {
    const float x = std::round(size.x * (float)quarter / 4.0f) + 0.5f;
    const Rml::Vector2f line[2] = { { x, 0.0f }, { x, size.y } };

    builder.addLine(line, scale, GRID_COLOR);
  }

  const float zeroY = std::round(origin.y) + 0.5f;
  const Rml::Vector2f zero[2] = { { 0.0f, zeroY }, { size.x, zeroY } };

  builder.addLine(zero, scale, ZERO_COLOR);

  const std::vector<WaveLine> lines = traceWave(this->type, this->phaseOffset, STEPS);
  std::vector<WaveLine> screenLines;

  for (const WaveLine& line : lines) {
    addFill(builder, line, origin, extent);

    WaveLine& points = screenLines.emplace_back();

    for (const Rml::Vector2f point : line) {
      points.push_back({ origin.x + point.x * extent.x, origin.y - point.y * extent.y });
    }
  }

  for (const WaveLine& points : screenLines) {
    builder.addLine(points, 5.0f * scale, GLOW_COLOR);
  }

  for (const WaveLine& points : screenLines) {
    builder.addLine(points, 1.6f * scale, LINE_COLOR);
  }

  this->geometry = this->GetRenderManager()->MakeGeometry(builder.takeMesh());
  this->isDirty = false;
}

void WaveScreen::OnRender() {
  if (!this->GetRenderManager()) {
    return;
  }

  if (this->isDirty) {
    this->rebuild();
  }

  this->geometry.Render(this->GetAbsoluteOffset(Rml::BoxArea::Border));
}
