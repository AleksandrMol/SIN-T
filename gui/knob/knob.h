#pragma once

#include "../parameterControl/parameterControl.h"

/**
 * Металлическая ручка с дугами значения и модуляции
 */
class Knob : public ParameterControl {
  public:
    using ParameterControl::ParameterControl;

  protected:
    void build(MeshBuilder& builder, Rml::Vector2f size, float scale) override;
};
