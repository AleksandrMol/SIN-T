#pragma once

#include "../parameterControl/parameterControl.h"

/**
 * Числовое поле (D-108): число пишут дочерние элементы, само поле рисует только полоску модуляции под ним.
 * Ввод тот же, что у ручки: перетаскивание, колесо, двойной щелчок, ввод числа, приём провода модуляции
 */
class ValueField : public ParameterControl {
  public:
    using ParameterControl::ParameterControl;

  protected:
    void build(MeshBuilder& builder, Rml::Vector2f size, float scale) override;
};
