#include "./valueField.h"

#include <array>

#include "../meshBuilder/meshBuilder.h"

namespace {
  const Rml::Colourb MODULATION_COLOR(240, 146, 58);

  // Полоска лежит у нижнего края поля и не доходит до его боков на этот отступ, dp
  constexpr float BAR_INSET = 3.0f;
  constexpr float BAR_BOTTOM = 1.0f;
  constexpr float BAR_HEIGHT = 2.0f;
}

/**
 * Полоска модуляции идёт по ширине поля от нижней до верхней границы, которых достигает значение
 */
void ValueField::build(MeshBuilder& builder, Rml::Vector2f size, float scale) {
  const float low = this->getModulationLow();
  const float high = this->getModulationHigh();

  if (high <= low) {
    return;
  }

  const float inset = BAR_INSET * scale;
  const float width = size.x - 2.0f * inset;
  const float left = inset + low * width;
  const float right = inset + high * width;
  const float bottom = size.y - BAR_BOTTOM * scale;
  const float top = bottom - BAR_HEIGHT * scale;
  const std::array<Rml::Vector2f, 4> bar = {
    Rml::Vector2f(left, top),
    Rml::Vector2f(right, top),
    Rml::Vector2f(right, bottom),
    Rml::Vector2f(left, bottom),
  };

  builder.addConvexPolygon(bar, { &MODULATION_COLOR, 1 });
}
