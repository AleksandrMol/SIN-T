#pragma once

#include <RmlUi/Core.h>

/**
 * Шаг сетки холста, dp
 */
constexpr float GRID_CELL_DP = 18.0f;

/**
 * Каждая такая линия ярче остальных: по крупным клеткам удобнее прикидывать расстояние
 */
constexpr int GRID_MAJOR_EVERY = 4;

/**
 * Через столько dp рисунок сетки повторяется. Слой сетки на столько же больше холста, это отступ справа и снизу в стилях
 */
constexpr float GRID_PERIOD_DP = GRID_CELL_DP * (float)GRID_MAJOR_EVERY;

/**
 * Где поставить слой сетки, dp, чтобы её линии шли вместе со схемой, сдвинутой на pan.
 * Слой ездит только в пределах одного периода: на целый период сетка выглядит так же,
 * поэтому сдвиг — остаток от pan, от −период до нуля, а слой всегда закрывает весь холст
 */
Rml::Vector2f getGridOffset(Rml::Vector2f pan);

/**
 * Линии сетки на прямоугольнике size, px, от его угла. scale — dp в пикселях.
 * Линия выглядит прорезанной в металле: тёмная полоска, за ней светлая, как на референсе D-093.
 * Каждая GRID_MAJOR_EVERY-я линия ярче, первая линия у угла — яркая
 */
Rml::Mesh buildGrid(Rml::Vector2f size, float scale);

/**
 * Сетка холста: decorator: grid(). Строится при смене размера элемента, а не при прокрутке:
 * прокрутка двигает сам слой (getGridOffset), как background-position в вебе
 */
class GridDecoratorInstancer final : public Rml::DecoratorInstancer {
  public:
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String& name, const Rml::PropertyDictionary& properties,
      const Rml::DecoratorInstancerInterface& instancerInterface) override;
};
