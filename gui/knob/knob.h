#pragma once

#include <RmlUi/Core.h>

/**
 * Ручка: только рисует значение, ввод обрабатывает редактор.
 * Все значения 0..1 в положении ручки, поэтому логарифмические параметры выглядят равномерно
 */
class Knob : public Rml::Element {
  public:
    using Rml::Element::Element;

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

  protected:
    void OnRender() override;
    void OnResize() override;

  private:
    float value = 0.0f;
    float origin = 0.0f;
    float modulationLow = 0.0f;
    float modulationHigh = 0.0f;
    bool isDirty = true;
    Rml::Geometry geometry;

    void rebuild();
    void change(float& field, float next);
};
