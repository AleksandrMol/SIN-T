#pragma once

#include <RmlUi/Core.h>

#include <span>
#include <vector>

/**
 * Простейшая отрисовка для своих элементов: линии и треугольники с мягким краем.
 * Заливка кривых в CSS недоступна, поэтому ручки и провода рисуются сеткой, как path в canvas
 */
class MeshBuilder {
  public:
    /**
     * Прозрачность CSS к своей сетке не применяется, поэтому элемент передаёт её сам
     */
    explicit MeshBuilder(float opacity = 1.0f);

    /**
     * Линия через точки. Край на 1 пиксель уходит в прозрачность, поэтому она сглажена
     */
    void addLine(std::span<const Rml::Vector2f> points, float width, Rml::Colourb color);

    void addTriangle(Rml::Vector2f first, Rml::Vector2f second, Rml::Vector2f third, Rml::Colourb color);

    /**
     * Дуга окружности. Углы в радианах, отсчёт от оси X по часовой стрелке на экране
     */
    void addArc(Rml::Vector2f center, float radius, float startAngle, float endAngle, float width, Rml::Colourb color);

    Rml::Mesh takeMesh();

  private:
    Rml::Mesh mesh;
    float opacity;

    int addVertex(Rml::Vector2f position, Rml::Colourb color, float alpha);
};
