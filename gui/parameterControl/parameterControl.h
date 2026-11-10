#pragma once

#include "../meshElement/meshElement.h"

/**
 * Элемент, который показывает значение параметра: ручка или поле (D-108). Только рисует, ввод обрабатывает редактор.
 * Все значения 0..1 в положении ручки, поэтому логарифмические параметры выглядят равномерно.
 * Сетка строится заново только после смены значения или размера
 */
class ParameterControl : public MeshElement {
  public:
    using MeshElement::MeshElement;

    void setValue(float value);

    /**
     * Откуда идёт дуга значения: у параметра с нулём посередине это центр
     */
    void setOrigin(float origin);

    /**
     * Границы, которых значение достигает под модуляцией. Равные границы — модуляции нет
     */
    void setModulation(float low, float high);

    float getValue() const;
    float getOrigin() const;
    float getModulationLow() const;
    float getModulationHigh() const;

  private:
    float value = 0.0f;
    float origin = 0.0f;
    float modulationLow = 0.0f;
    float modulationHigh = 0.0f;

    void change(float& field, float next);
};
