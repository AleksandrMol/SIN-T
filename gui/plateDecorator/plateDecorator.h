#pragma once

#include <RmlUi/Core.h>

#include "../plateMesh/plateMesh.h"

/**
 * Металлическая пластина со срезанными углами, фаской и винтами, как на референсе D-093.
 * В CSS RmlUi углы только скругляются, а маска рисуется через отдельный слой на каждом кадре,
 * поэтому пластина — свой декоратор, как свой background: decorator: plate(panel 10dp screws).
 * Свет сверху слева, как у ручек и логотипа
 */
class PlateDecoratorInstancer final : public Rml::DecoratorInstancer {
  public:
    PlateDecoratorInstancer();

    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String& name, const Rml::PropertyDictionary& properties,
      const Rml::DecoratorInstancerInterface& instancerInterface) override;

  private:
    Rml::PropertyId variantId;
    Rml::PropertyId chamferId;
    Rml::PropertyId detailsId;
};
