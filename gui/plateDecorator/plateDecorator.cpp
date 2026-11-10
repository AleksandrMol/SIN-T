#include "./plateDecorator.h"

#include <RmlUi/Core/ElementUtilities.h>

#include <algorithm>
#include <vector>

namespace {
  struct PlateData {
    Rml::Geometry base;
    Rml::Geometry texture;
    Rml::Geometry top;
    /** Геометрия живёт в своём окне: у каждого окна свой контекст OpenGL */
    Rml::RenderManager* renderManager = nullptr;
    PlateShape shape;
    /** Сколько ссылок RmlUi держит на эти данные: элементы с той же формой делят одну сетку */
    int users = 1;
  };

  class PlateDecorator final : public Rml::Decorator {
    public:
      PlateDecorator(PLATE_VARIANT variant, PLATE_DETAILS details, Rml::NumericValue chamfer, Rml::Texture texture) :
        variant(variant), details(details), chamfer(chamfer) {
        this->textureIndex = this->AddTexture(texture);
      }

      /**
       * RmlUi просит данные заново после каждой раскладки, в которой у элемента хоть раз менялся размер.
       * Флекс раскладывает элементы сначала на пробу, потом окончательно, поэтому так бывает почти в каждом кадре
       * с раскладкой, даже если итоговый размер прежний. Пластина тогда отдаёт уже построенную сетку:
       * RmlUi освобождает старые данные после получения новых именно ради такого повтора.
       * Сетка задана относительно угла элемента, поэтому одинаковые элементы, например ячейки матрицы, делят одну
       */
      Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea paintArea) const override {
        Rml::RenderManager* renderManager = element->GetRenderManager();

        if (!renderManager) {
          return INVALID_DECORATORDATAHANDLE;
        }

        const Rml::RenderBox box = element->GetRenderBox(paintArea);
        const float scale = Rml::ElementUtilities::GetDensityIndependentPixelRatio(element);
        Rml::Vector2f textureScale(0.0f, 0.0f);

        if (this->textureIndex >= 0) {
          const Rml::Vector2i dimensions = this->GetTexture(this->textureIndex).GetDimensions();

          // Плитка того же размера, что у image(... repeat): пиксель картинки на dp
          if (dimensions.x > 0 && dimensions.y > 0) {
            textureScale = Rml::Vector2f(1.0f / ((float)dimensions.x * scale), 1.0f / ((float)dimensions.y * scale));
          }
        }

        const PlateShape shape = {
          .offset = box.GetFillOffset(),
          .size = box.GetFillSize(),
          .chamfer = element->ResolveLength(this->chamfer),
          .scale = scale,
          .opacity = element->GetComputedValues().opacity(),
          .textureScale = textureScale,
        };
        const auto built = std::ranges::find_if(this->built, [renderManager, &shape](const PlateData* data) {
          return data->renderManager == renderManager && data->shape == shape;
        });

        if (built != this->built.end()) {
          (*built)->users++;

          return reinterpret_cast<Rml::DecoratorDataHandle>(*built);
        }

        PlateMeshes meshes = buildPlate(this->variant, this->details, shape);
        auto* data = new PlateData{
          .base = renderManager->MakeGeometry(std::move(meshes.base)),
          .texture = renderManager->MakeGeometry(std::move(meshes.texture)),
          .top = renderManager->MakeGeometry(std::move(meshes.top)),
          .renderManager = renderManager,
          .shape = shape,
        };

        this->built.push_back(data);

        return reinterpret_cast<Rml::DecoratorDataHandle>(data);
      }

      void ReleaseElementData(Rml::DecoratorDataHandle elementData) const override {
        auto* data = reinterpret_cast<PlateData*>(elementData);

        if (--data->users > 0) {
          return;
        }

        std::erase(this->built, data);
        delete data;
      }

      void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle elementData) const override {
        auto* data = reinterpret_cast<PlateData*>(elementData);
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);

        data->base.Render(offset);

        if (this->textureIndex >= 0) {
          data->texture.Render(offset, this->GetTexture(this->textureIndex));
        }

        data->top.Render(offset);
      }

    private:
      PLATE_VARIANT variant;
      PLATE_DETAILS details;
      Rml::NumericValue chamfer;
      int textureIndex = -1;
      /** Построенные сетки, по одной на форму. Сетка живёт, пока её держит хоть один элемент */
      mutable std::vector<PlateData*> built;
  };
}

PlateDecoratorInstancer::PlateDecoratorInstancer() {
  this->variantId = this->RegisterProperty("variant", "panel")
    .AddParser("keyword", "frame, panel, sunken, button, button-hover, button-active, display, bare")
    .GetId();
  this->chamferId = this->RegisterProperty("chamfer", "0dp").AddParser("length").GetId();
  this->detailsId = this->RegisterProperty("details", "none").AddParser("keyword", "none, screws, gussets, grip").GetId();
  this->RegisterShorthand("decorator", "variant, chamfer, details", Rml::ShorthandType::FallThrough);
}

Rml::SharedPtr<Rml::Decorator> PlateDecoratorInstancer::InstanceDecorator(const Rml::String& /*name*/, const Rml::PropertyDictionary& properties,
  const Rml::DecoratorInstancerInterface& instancerInterface) {
  const Rml::Property* variantProperty = properties.GetProperty(this->variantId);
  const Rml::Property* chamferProperty = properties.GetProperty(this->chamferId);
  const Rml::Property* detailsProperty = properties.GetProperty(this->detailsId);

  if (!variantProperty || !chamferProperty || !detailsProperty) {
    return nullptr;
  }

  const auto variant = (PLATE_VARIANT)variantProperty->Get<int>();
  Rml::Texture texture;

  if (const char* name = getPlateTexture(variant)) {
    texture = instancerInterface.GetTexture(name);
  }

  return Rml::MakeShared<PlateDecorator>(variant, (PLATE_DETAILS)detailsProperty->Get<int>(), chamferProperty->GetNumericValue(), texture);
}
