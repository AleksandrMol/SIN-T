#include "./gridDecorator.h"

#include <algorithm>
#include <cmath>

#include "../meshBuilder/meshBuilder.h"

namespace {
  // Полупрозрачные цвета: сетка ложится на любой тон холста, а виньетка сверху приглушает её у краёв
  const Rml::Colourb SHADOW_COLOR(0, 0, 0, 70);
  const Rml::Colourb MINOR_COLOR(96, 136, 178, 26);
  const Rml::Colourb MAJOR_COLOR(110, 156, 204, 46);

  float wrapPan(float pan) {
    const float rest = std::fmod(pan, GRID_PERIOD_DP);

    return rest > 0.0f ? rest - GRID_PERIOD_DP : rest;
  }

  struct GridData {
    Rml::Geometry geometry;
    Rml::Vector2f fillOffset;
  };

  class GridDecorator final : public Rml::Decorator {
    public:
      Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea paintArea) const override {
        Rml::RenderManager* renderManager = element->GetRenderManager();

        if (!renderManager) {
          return INVALID_DECORATORDATAHANDLE;
        }

        const Rml::RenderBox box = element->GetRenderBox(paintArea);
        const float scale = Rml::ElementUtilities::GetDensityIndependentPixelRatio(element);
        auto* data = new GridData{ renderManager->MakeGeometry(buildGrid(box.GetFillSize(), scale)), box.GetFillOffset() };

        return reinterpret_cast<Rml::DecoratorDataHandle>(data);
      }

      void ReleaseElementData(Rml::DecoratorDataHandle elementData) const override {
        delete reinterpret_cast<GridData*>(elementData);
      }

      /**
       * Слой сетки стоит на дробном пикселе, когда схему сдвигают при дробном масштабе.
       * Линии в пиксель шириной тогда расплылись бы на два, поэтому сетка встаёт на целый пиксель
       */
      void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle elementData) const override {
        auto* data = reinterpret_cast<GridData*>(elementData);
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border) + data->fillOffset;

        data->geometry.Render(Rml::Vector2f(std::round(offset.x), std::round(offset.y)));
      }
  };
}

Rml::Vector2f getGridOffset(Rml::Vector2f pan) {
  return Rml::Vector2f(wrapPan(pan.x), wrapPan(pan.y));
}

Rml::Mesh buildGrid(Rml::Vector2f size, float scale) {
  MeshBuilder builder;
  const float cell = GRID_CELL_DP * scale;
  const float width = std::max(1.0f, std::round(scale));

  if (cell < width * 2.0f) {
    return builder.takeMesh();
  }

  for (int index = 0; std::round((float)index * cell) < size.x; index++) {
    const float x = std::round((float)index * cell);
    const Rml::Colourb color = index % GRID_MAJOR_EVERY == 0 ? MAJOR_COLOR : MINOR_COLOR;

    builder.addRectangle(Rml::Vector2f(x - width, 0.0f), Rml::Vector2f(x, size.y), SHADOW_COLOR);
    builder.addRectangle(Rml::Vector2f(x, 0.0f), Rml::Vector2f(x + width, size.y), color);
  }

  for (int index = 0; std::round((float)index * cell) < size.y; index++) {
    const float y = std::round((float)index * cell);
    const Rml::Colourb color = index % GRID_MAJOR_EVERY == 0 ? MAJOR_COLOR : MINOR_COLOR;

    builder.addRectangle(Rml::Vector2f(0.0f, y - width), Rml::Vector2f(size.x, y), SHADOW_COLOR);
    builder.addRectangle(Rml::Vector2f(0.0f, y), Rml::Vector2f(size.x, y + width), color);
  }

  return builder.takeMesh();
}

Rml::SharedPtr<Rml::Decorator> GridDecoratorInstancer::InstanceDecorator(const Rml::String& /*name*/, const Rml::PropertyDictionary& /*properties*/,
  const Rml::DecoratorInstancerInterface& /*instancerInterface*/) {
  return Rml::MakeShared<GridDecorator>();
}
