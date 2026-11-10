#pragma once

#include <RmlUi/Core.h>

class MeshBuilder;

/**
 * Элемент, который показывает значение параметра: ручка или поле (D-108). Только рисует, ввод обрабатывает редактор.
 * Все значения 0..1 в положении ручки, поэтому логарифмические параметры выглядят равномерно.
 * Сетка строится заново только после смены значения или размера
 */
class ParameterControl : public Rml::Element {
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

    /**
     * Рисует элемент от угла его рамки. scale — dp в пикселях
     */
    virtual void build(MeshBuilder& builder, Rml::Vector2f size, float scale) = 0;

  private:
    float value = 0.0f;
    float origin = 0.0f;
    float modulationLow = 0.0f;
    float modulationHigh = 0.0f;
    bool isDirty = true;
    Rml::Geometry geometry;

    void change(float& field, float next);
};
