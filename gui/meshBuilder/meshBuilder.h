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

    struct Ring {
      Rml::Vector2f center;
      /** Ноль — сплошной круг */
      float innerRadius = 0.0f;
      float outerRadius = 0.0f;
      /** Сколько пикселей внешний край уходит в прозрачность: 1 — просто сглаживание, больше — размытая тень */
      float feather = 1.0f;
      int steps = 64;
    };

    /**
     * Угол шага кольца: от оси X по часовой стрелке, как у addArc, шаг steps совпадает с шагом 0
     */
    static float getRingAngle(int step, int steps);

    /**
     * Кольцо с цветом по кругу отдельно на внутреннем и внешнем краю, как conic-gradient.
     * Цветов либо один на всё кольцо, либо steps + 1, по одному на шаг из getRingAngle.
     * Таблицу цветов можно посчитать один раз и отдавать каждой ручке, поэтому цвет не функция.
     * У круга с нулевым внутренним радиусом каждый сектор сходится в центр своим цветом,
     * поэтому получается настоящий конический градиент, а не смешанное пятно в середине
     */
    void addRing(const Ring& ring, std::span<const Rml::Colourb> innerColors, std::span<const Rml::Colourb> outerColors);

    Rml::Mesh takeMesh();

  private:
    Rml::Mesh mesh;
    float opacity;

    int addVertex(Rml::Vector2f position, Rml::Colourb color, float alpha);
};
