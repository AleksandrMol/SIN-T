#pragma once

#include <RmlUi/Core.h>

#include <vector>

#include "../../engine/oscillator/oscillator.h"

using WaveLine = std::vector<Rml::Vector2f>;

/**
 * Один период волны со сдвигом фазы, как его рисует экран: x 0..1 по ширине, y — значение волны -1..1.
 * Гладкие куски идут отдельными линиями, скачок — вертикальный отрезок из двух точек.
 * steps — число отрезков на весь период у гладкой части
 */
std::vector<WaveLine> traceWave(WAVE_TYPE type, float phaseOffset, int steps);

/**
 * Мини-экран осциллятора (D-094, D-108): форма волны по параметрам модуля, а не по звуку.
 * Сетка строится только при смене формы или размера, поэтому в простое кадров нет
 */
class WaveScreen : public Rml::Element {
  public:
    using Rml::Element::Element;

    /**
     * phaseOffset — сдвиг фазы в долях периода 0..1
     */
    void setShape(WAVE_TYPE type, float phaseOffset);

    WAVE_TYPE getType() const;
    float getPhaseOffset() const;

  protected:
    void OnRender() override;
    void OnResize() override;

  private:
    WAVE_TYPE type = WAVE_TYPE::SAW;
    float phaseOffset = 0.0f;
    bool isDirty = true;
    Rml::Geometry geometry;

    void rebuild();
};
