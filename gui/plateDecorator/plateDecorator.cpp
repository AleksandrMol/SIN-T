#include "./plateDecorator.h"

#include <RmlUi/Core/ElementUtilities.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

#include "../meshBuilder/meshBuilder.h"

namespace {
  constexpr float SQRT2 = std::numbers::sqrt2_v<float>;
  constexpr float DEGREE = std::numbers::pi_v<float> / 180.0f;
  constexpr size_t EDGE_COUNT = 8;
  constexpr int SCREW_STEPS = 24;

  // Размеры в dp, кроме сглаживания края: оно всегда в один пиксель
  constexpr float FEATHER_PX = 1.0f;
  constexpr float OUTLINE = 1.0f;
  constexpr float INNER_SHADOW = 7.0f;
  constexpr float SCREW_RADIUS = 3.2f;
  constexpr float GUSSET = 26.0f;
  constexpr float SHEEN_COLUMN = 32.0f;
  constexpr float GRIP_STEP = 3.0f;
  constexpr int GRIP_GROOVES = 3;
  constexpr int MAX_SHEEN_COLUMNS = 48;

  // Полосы блеска как linear-gradient(100deg, ...) у блоков: положение вдоль полосы и сила, минус — темнее
  constexpr float SHEEN_ANGLE = 100.0f * DEGREE;
  constexpr std::array<std::array<float, 2>, 8> SHEEN_STOPS = { {
    { 0.00f, 0.0f }, { 0.16f, 1.0f }, { 0.32f, 0.0f }, { 0.46f, -1.1f },
    { 0.56f, 0.0f }, { 0.72f, 0.7f }, { 0.88f, 0.0f }, { 1.00f, 0.0f },
  } };

  const Rml::Colourb OUTLINE_COLOR(4, 5, 7);
  const Rml::Colourb WHITE(255, 255, 255);
  const Rml::Colourb BLACK(0, 0, 0);
  const Rml::Colourb SOCKET_COLOR(0, 0, 0, 170);
  const Rml::Colourb HEAD_CENTER_COLOR(58, 64, 73);
  const Rml::Colourb HEAD_DARK_COLOR(14, 16, 19);
  const Rml::Colourb HEAD_LIGHT_COLOR(150, 160, 174);
  const Rml::Colourb SLOT_COLOR(6, 7, 9, 220);
  const Rml::Colourb GLINT_COLOR(255, 255, 255, 110);
  const Rml::Colourb GUSSET_CORNER_COLOR(58, 64, 73);
  const Rml::Colourb GUSSET_EDGE_COLOR(34, 38, 44);
  const Rml::Colourb GUSSET_SHADOW_COLOR(0, 0, 0, 120);
  const Rml::Colourb GUSSET_LIGHT_COLOR(176, 187, 201);
  const Rml::Colourb GUSSET_DARK_COLOR(12, 14, 17);
  const Rml::Colourb GROOVE_DARK_COLOR(0, 0, 0, 190);
  const Rml::Colourb GROOVE_LIGHT_COLOR(200, 210, 224, 80);

  // Свет сверху слева, как у панелей, ручек и логотипа
  const Rml::Vector2f TO_LIGHT = Rml::Vector2f(-1.0f, -1.2f).Normalise();
  const float LIGHT_ANGLE = std::atan2(TO_LIGHT.y, TO_LIGHT.x);

  // Наружные нормали рёбер восьмиугольника по часовой стрелке, начиная с верхнего
  const std::array<Rml::Vector2f, EDGE_COUNT> NORMALS = {
    Rml::Vector2f(0.0f, -1.0f), Rml::Vector2f(1.0f, -1.0f) / SQRT2,
    Rml::Vector2f(1.0f, 0.0f), Rml::Vector2f(1.0f, 1.0f) / SQRT2,
    Rml::Vector2f(0.0f, 1.0f), Rml::Vector2f(-1.0f, 1.0f) / SQRT2,
    Rml::Vector2f(-1.0f, 0.0f), Rml::Vector2f(-1.0f, -1.0f) / SQRT2,
  };

  struct Look {
    bool hasFace = true;
    Rml::Colourb top = Rml::Colourb(0, 0, 0);
    Rml::Colourb bottom = Rml::Colourb(0, 0, 0);
    float sheen = 0.0f;
    const char* texture = nullptr;
    /** Утопленная: фаска светлая снизу справа, сверху слева падает тень */
    bool isSunken = false;
    Rml::Colourb light = Rml::Colourb(0, 0, 0);
    Rml::Colourb dark = Rml::Colourb(0, 0, 0);
    float bevel = 1.5f;
  };

  Look getLook(PLATE_VARIANT variant) {
    switch (variant) {
      case PLATE_VARIANT::FRAME:
        return { .top = Rml::Colourb(59, 66, 76), .bottom = Rml::Colourb(30, 33, 39), .sheen = 0.07f, .texture = "sint:worn",
          .light = Rml::Colourb(185, 196, 210), .dark = Rml::Colourb(10, 12, 15), .bevel = 2.0f };
      case PLATE_VARIANT::PANEL:
        return { .top = Rml::Colourb(48, 53, 61), .bottom = Rml::Colourb(28, 31, 36), .sheen = 0.06f, .texture = "sint:worn",
          .light = Rml::Colourb(165, 176, 190), .dark = Rml::Colourb(10, 12, 15), .bevel = 1.5f };
      case PLATE_VARIANT::SUNKEN:
        return { .top = Rml::Colourb(13, 15, 18), .bottom = Rml::Colourb(23, 26, 31), .texture = "sint:scuffed", .isSunken = true,
          .light = Rml::Colourb(78, 87, 99), .dark = Rml::Colourb(0, 0, 0), .bevel = 1.5f };
      case PLATE_VARIANT::BUTTON:
        return { .top = Rml::Colourb(74, 82, 93), .bottom = Rml::Colourb(44, 49, 56), .sheen = 0.05f, .texture = "sint:brushed",
          .light = Rml::Colourb(170, 182, 197), .dark = Rml::Colourb(12, 14, 17), .bevel = 1.0f };
      case PLATE_VARIANT::BUTTON_HOVER:
        return { .top = Rml::Colourb(88, 97, 110), .bottom = Rml::Colourb(53, 59, 67), .sheen = 0.06f, .texture = "sint:brushed",
          .light = Rml::Colourb(205, 214, 226), .dark = Rml::Colourb(12, 14, 17), .bevel = 1.0f };
      case PLATE_VARIANT::BUTTON_ACTIVE:
        return { .top = Rml::Colourb(29, 75, 134), .bottom = Rml::Colourb(43, 104, 179), .isSunken = true,
          .light = Rml::Colourb(127, 178, 236), .dark = Rml::Colourb(8, 22, 42), .bevel = 1.0f };
      case PLATE_VARIANT::DISPLAY:
        return { .top = Rml::Colourb(7, 9, 12), .bottom = Rml::Colourb(16, 20, 25), .sheen = 0.03f, .isSunken = true,
          .light = Rml::Colourb(88, 97, 110), .dark = Rml::Colourb(0, 0, 0), .bevel = 1.0f };
      case PLATE_VARIANT::BARE:
        break;
    }

    return { .hasFace = false };
  }

  Rml::Colourb mix(Rml::Colourb from, Rml::Colourb to, float amount) {
    const float clamped = std::clamp(amount, 0.0f, 1.0f);
    const auto channel = [clamped](Rml::byte a, Rml::byte b) {
      return (Rml::byte)std::lround((float)a + ((float)b - (float)a) * clamped);
    };

    return Rml::Colourb(channel(from.red, to.red), channel(from.green, to.green), channel(from.blue, to.blue), channel(from.alpha, to.alpha));
  }

  Rml::Colourb withAlpha(Rml::Colourb color, Rml::byte alpha) {
    color.alpha = alpha;

    return color;
  }

  /**
   * Насколько ребро с этой нормалью смотрит на свет: 1 к свету, 0 от него
   */
  float facing(Rml::Vector2f normal) {
    return 0.5f + 0.5f * normal.DotProduct(TO_LIGHT);
  }

  /**
   * Прямоугольник со срезанными углами на заданном отступе внутрь от края пластины.
   * Срез под 45° при отступе d укорачивается на d·(2 − √2), так полосы фаски одинаковой ширины по всем рёбрам
   */
  struct Frame {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float chamfer = 0.0f;

    float getWidth() const {
      return this->right - this->left;
    }

    float getHeight() const {
      return this->bottom - this->top;
    }

    std::array<Rml::Vector2f, EDGE_COUNT> getCorners() const {
      const float c = this->chamfer;

      return {
        Rml::Vector2f(this->left + c, this->top), Rml::Vector2f(this->right - c, this->top),
        Rml::Vector2f(this->right, this->top + c), Rml::Vector2f(this->right, this->bottom - c),
        Rml::Vector2f(this->right - c, this->bottom), Rml::Vector2f(this->left + c, this->bottom),
        Rml::Vector2f(this->left, this->bottom - c), Rml::Vector2f(this->left, this->top + c),
      };
    }
  };

  Frame insetFrame(const PlateShape& shape, float inset) {
    Frame frame;

    frame.left = shape.offset.x + inset;
    frame.top = shape.offset.y + inset;
    frame.right = std::max(shape.offset.x + shape.size.x - inset, frame.left);
    frame.bottom = std::max(shape.offset.y + shape.size.y - inset, frame.top);

    const float chamfer = shape.chamfer > 0.0f ? std::max(shape.chamfer - inset * (2.0f - SQRT2), 0.0f) : 0.0f;

    frame.chamfer = std::min({ chamfer, frame.getWidth() * 0.5f, frame.getHeight() * 0.5f });

    return frame;
  }

  using EdgeColors = std::array<Rml::Colourb, EDGE_COUNT>;

  EdgeColors uniform(Rml::Colourb color) {
    EdgeColors colors;

    colors.fill(color);

    return colors;
  }

  /**
   * Полоса между двумя восьмиугольниками, у каждого ребра свой цвет: так фаска светлее со стороны света
   */
  void addBand(MeshBuilder& builder, const Frame& outer, const Frame& inner, const EdgeColors& outerColors, const EdgeColors& innerColors) {
    const auto outerCorners = outer.getCorners();
    const auto innerCorners = inner.getCorners();

    for (size_t edge = 0; edge < EDGE_COUNT; edge++) {
      const size_t next = (edge + 1) % EDGE_COUNT;
      const Rml::Vector2f points[4] = { outerCorners[edge], outerCorners[next], innerCorners[next], innerCorners[edge] };
      const Rml::Colourb colors[4] = { outerColors[edge], outerColors[edge], innerColors[edge], innerColors[edge] };

      builder.addConvexPolygon(points, colors);
    }
  }

  float sheenAt(float position) {
    for (size_t index = 1; index < SHEEN_STOPS.size(); index++) {
      if (position <= SHEEN_STOPS[index][0]) {
        const auto& from = SHEEN_STOPS[index - 1];
        const auto& to = SHEEN_STOPS[index];

        return from[1] + (to[1] - from[1]) * (position - from[0]) / (to[0] - from[0]);
      }
    }

    return 0.0f;
  }

  /**
   * Цвет лицевой стороны: сверху вниз от светлого к тёмному и полосы блеска наискосок.
   * Направление и длина полос считаются один раз на пластину
   */
  class FaceShading {
    public:
      FaceShading(const Look& look, const Frame& face) : look(look), face(face) {
        this->direction = Rml::Vector2f(std::sin(SHEEN_ANGLE), -std::cos(SHEEN_ANGLE));
        this->length = std::fabs(face.getWidth() * this->direction.x) + std::fabs(face.getHeight() * this->direction.y);
        this->center = Rml::Vector2f((face.left + face.right) * 0.5f, (face.top + face.bottom) * 0.5f);
      }

      Rml::Colourb at(Rml::Vector2f point) const {
        const float height = this->face.getHeight();
        const Rml::Colourb color = mix(this->look.top, this->look.bottom, height > 0.0f ? (point.y - this->face.top) / height : 0.0f);

        if (this->look.sheen <= 0.0f || this->length <= 0.0f) {
          return color;
        }

        const float along = (point - this->center).DotProduct(this->direction) / this->length + 0.5f;
        const float amount = this->look.sheen * sheenAt(std::clamp(along, 0.0f, 1.0f));

        return amount >= 0.0f ? mix(color, withAlpha(WHITE, color.alpha), amount) : mix(color, withAlpha(BLACK, color.alpha), -amount);
      }

    private:
      const Look& look;
      const Frame& face;
      Rml::Vector2f direction;
      Rml::Vector2f center;
      float length = 0.0f;
  };

  /**
   * Выпуклый многоугольник на стеке: клетка после обрезки четырьмя срезами — не больше восьми точек
   */
  struct Polygon {
    std::array<Rml::Vector2f, 12> points;
    size_t count = 0;

    std::span<const Rml::Vector2f> get() const {
      return { this->points.data(), this->count };
    }
  };

  /**
   * Оставляет часть многоугольника, где a·x + b·y + d ≥ 0 (Сазерленд — Ходжмен)
   */
  Polygon clip(const Polygon& polygon, float a, float b, float d) {
    Polygon result;

    for (size_t index = 0; index < polygon.count; index++) {
      const Rml::Vector2f current = polygon.points[index];
      const Rml::Vector2f previous = polygon.points[(index + polygon.count - 1) % polygon.count];
      const float currentSide = a * current.x + b * current.y + d;
      const float previousSide = a * previous.x + b * previous.y + d;

      if ((currentSide >= 0.0f) != (previousSide >= 0.0f) && result.count < result.points.size()) {
        result.points[result.count++] = previous + (current - previous) * (previousSide / (previousSide - currentSide));
      }

      if (currentSide >= 0.0f && result.count < result.points.size()) {
        result.points[result.count++] = current;
      }
    }

    return result;
  }

  /**
   * Лицевая сторона. Вертикальный градиент линеен, и без блеска хватает одного восьмиугольника.
   * Полосы блеска почти вертикальные и не линейны поперёк, поэтому с ними сторона режется на столбцы,
   * а у срезанных углов столбцы обрезаются по срезу. Сетка собирается заново при каждой перекладке
   * flex-контейнера (RmlUi примеряет размер дважды), поэтому она нарочно редкая и без выделения памяти
   */
  void addFace(MeshBuilder& builder, const Look& look, const Frame& face, float scale) {
    const float width = face.getWidth();
    const float height = face.getHeight();

    if (width <= 0.0f || height <= 0.0f) {
      return;
    }

    const FaceShading shading(look, face);
    std::array<Rml::Colourb, 12> colors;

    if (look.sheen <= 0.0f) {
      const auto corners = face.getCorners();

      for (size_t index = 0; index < corners.size(); index++) {
        colors[index] = shading.at(corners[index]);
      }

      builder.addConvexPolygon(corners, std::span<const Rml::Colourb>(colors.data(), corners.size()));

      return;
    }

    const int columns = std::clamp((int)std::ceil(width / (SHEEN_COLUMN * scale)), 1, MAX_SHEEN_COLUMNS);
    const float c = face.chamfer;

    for (int column = 0; column < columns; column++) {
      const float left = face.left + width * (float)column / (float)columns;
      const float right = face.left + width * (float)(column + 1) / (float)columns;
      Polygon cell;

      cell.points[0] = Rml::Vector2f(left, face.top);
      cell.points[1] = Rml::Vector2f(right, face.top);
      cell.points[2] = Rml::Vector2f(right, face.bottom);
      cell.points[3] = Rml::Vector2f(left, face.bottom);
      cell.count = 4;

      // Срезы задевают только крайние столбцы
      if (c > 0.0f && left < face.left + c) {
        cell = clip(cell, 1.0f, 1.0f, -(face.left + face.top + c));
        cell = clip(cell, 1.0f, -1.0f, face.bottom - face.left - c);
      }

      if (c > 0.0f && right > face.right - c) {
        cell = clip(cell, -1.0f, 1.0f, face.right - face.top - c);
        cell = clip(cell, -1.0f, -1.0f, face.right + face.bottom - c);
      }

      if (cell.count < 3) {
        continue;
      }

      for (size_t index = 0; index < cell.count; index++) {
        colors[index] = shading.at(cell.points[index]);
      }

      builder.addConvexPolygon(cell.get(), std::span<const Rml::Colourb>(colors.data(), cell.count));
    }
  }

  std::span<const Rml::Colourb> single(const Rml::Colourb& color) {
    return { &color, 1 };
  }

  /**
   * Винт с полукруглой головкой: гнездо с тенью, головка освещена сверху слева, шлиц и блик
   */
  void addScrew(MeshBuilder& builder, Rml::Vector2f center, float radius, float scale, float slotAngle) {
    static const auto HEAD_COLORS = [] {
      std::array<Rml::Colourb, SCREW_STEPS + 1> colors;

      for (int step = 0; step <= SCREW_STEPS; step++) {
        const float light = 0.5f + 0.5f * std::cos(MeshBuilder::getRingAngle(step, SCREW_STEPS) - LIGHT_ANGLE);

        colors[(size_t)step] = mix(HEAD_DARK_COLOR, HEAD_LIGHT_COLOR, light * light);
      }

      return colors;
    }();

    builder.addRing(
      { .center = center + Rml::Vector2f(0.0f, 0.5f * scale), .outerRadius = radius + 0.6f * scale, .feather = 1.4f * scale, .steps = SCREW_STEPS },
      single(SOCKET_COLOR),
      single(SOCKET_COLOR)
    );
    builder.addRing({ .center = center, .outerRadius = radius, .steps = SCREW_STEPS }, single(HEAD_CENTER_COLOR), HEAD_COLORS);

    const Rml::Vector2f direction(std::cos(slotAngle), std::sin(slotAngle));
    const Rml::Vector2f slot[2] = { center - direction * (radius * 0.7f), center + direction * (radius * 0.7f) };

    builder.addLine(slot, 0.9f * scale, SLOT_COLOR);
    builder.addRing(
      { .center = center + Rml::Vector2f(-0.35f, -0.4f) * radius, .outerRadius = radius * 0.2f, .feather = radius * 0.4f, .steps = 12 },
      single(GLINT_COLOR),
      single(GLINT_COLOR)
    );
  }

  // Шлицы смотрят в разные стороны, как у настоящих винтов
  constexpr std::array<float, 4> SLOT_ANGLES = { 35.0f * DEGREE, 110.0f * DEGREE, 70.0f * DEGREE, 150.0f * DEGREE };

  void addScrews(MeshBuilder& builder, const PlateShape& shape, float edgeInset) {
    const float radius = SCREW_RADIUS * shape.scale;
    const float margin = edgeInset + std::max(shape.chamfer * 0.55f, 2.0f * shape.scale) + radius + 1.5f * shape.scale;

    // На маленькой пластине винты налезли бы на содержимое
    if (shape.size.x < margin * 4.0f || shape.size.y < margin * 2.2f) {
      return;
    }

    const float left = shape.offset.x + margin;
    const float top = shape.offset.y + margin;
    const float right = shape.offset.x + shape.size.x - margin;
    const float bottom = shape.offset.y + shape.size.y - margin;
    const Rml::Vector2f centers[4] = { { left, top }, { right, top }, { right, bottom }, { left, bottom } };

    for (size_t index = 0; index < 4; index++) {
      addScrew(builder, centers[index], radius, shape.scale, SLOT_ANGLES[index]);
    }
  }

  /**
   * Насечка посередине: бороздки поперёк длинной стороны. Свет сверху слева, поэтому у бороздки
   * тёмная кромка, а за ней светлая: свет падает на дальнюю стенку
   */
  void addGrip(MeshBuilder& builder, const PlateShape& shape, float edgeInset) {
    const bool isTall = shape.size.y >= shape.size.x;
    const float along = isTall ? shape.size.y : shape.size.x;
    const float across = isTall ? shape.size.x : shape.size.y;
    const float step = GRIP_STEP * shape.scale;
    const float span = step * (float)(GRIP_GROOVES - 1);
    const float margin = edgeInset + shape.scale;
    const float half = across * 0.5f - margin;

    // На короткой пластине насечка налезла бы на фаску
    if (along < span + step * 4.0f || half < shape.scale) {
      return;
    }

    const Rml::Vector2f center = shape.offset + shape.size * 0.5f;
    const Rml::Vector2f lengthAxis = isTall ? Rml::Vector2f(0.0f, 1.0f) : Rml::Vector2f(1.0f, 0.0f);
    const Rml::Vector2f crossAxis(lengthAxis.y, lengthAxis.x);

    for (int index = 0; index < GRIP_GROOVES; index++) {
      const Rml::Vector2f middle = center + lengthAxis * (step * (float)index - span * 0.5f);
      const Rml::Vector2f dark[2] = { middle - crossAxis * half, middle + crossAxis * half };
      const Rml::Vector2f light[2] = { dark[0] + lengthAxis * shape.scale, dark[1] + lengthAxis * shape.scale };

      builder.addLine(dark, shape.scale, GROOVE_DARK_COLOR);
      builder.addLine(light, shape.scale, GROOVE_LIGHT_COLOR);
    }
  }

  /**
   * Треугольные косынки в углах, как у рамы экрана: пластина, кромка по гипотенузе, тень и винт
   */
  void addGussets(MeshBuilder& builder, const PlateShape& shape) {
    const float size = GUSSET * shape.scale;

    if (shape.size.x < size * 3.0f || shape.size.y < size * 3.0f) {
      return;
    }

    const float left = shape.offset.x;
    const float top = shape.offset.y;
    const float right = shape.offset.x + shape.size.x;
    const float bottom = shape.offset.y + shape.size.y;
    const struct {
      Rml::Vector2f corner;
      Rml::Vector2f along;
      Rml::Vector2f down;
    } corners[4] = {
      { { left, top }, { 1.0f, 0.0f }, { 0.0f, 1.0f } },
      { { right, top }, { -1.0f, 0.0f }, { 0.0f, 1.0f } },
      { { right, bottom }, { -1.0f, 0.0f }, { 0.0f, -1.0f } },
      { { left, bottom }, { 1.0f, 0.0f }, { 0.0f, -1.0f } },
    };

    for (size_t index = 0; index < 4; index++) {
      const auto& gusset = corners[index];
      const Rml::Vector2f first = gusset.corner + gusset.along * size;
      const Rml::Vector2f second = gusset.corner + gusset.down * size;
      // Гипотенуза смотрит внутрь экрана
      const Rml::Vector2f normal = (gusset.along + gusset.down).Normalise();
      const Rml::Vector2f shadow[2] = { first + normal * (1.5f * shape.scale), second + normal * (1.5f * shape.scale) };
      const Rml::Vector2f edge[2] = { first, second };
      const Rml::Vector2f points[3] = { gusset.corner, first, second };
      const Rml::Colourb colors[3] = { GUSSET_CORNER_COLOR, GUSSET_EDGE_COLOR, GUSSET_EDGE_COLOR };
      const float light = facing(normal);

      builder.addLine(shadow, 2.5f * shape.scale, GUSSET_SHADOW_COLOR);
      builder.addConvexPolygon(points, colors);
      builder.addLine(edge, 1.2f * shape.scale, mix(GUSSET_DARK_COLOR, GUSSET_LIGHT_COLOR, std::pow(light, 1.5f)));
      addScrew(builder, gusset.corner + (gusset.along + gusset.down) * (size * 0.3f), SCREW_RADIUS * shape.scale * 0.85f, shape.scale, SLOT_ANGLES[index]);
    }
  }

  struct PlateData {
    Rml::Geometry base;
    Rml::Geometry texture;
    Rml::Geometry top;
    /** Геометрия живёт в своём окне: у каждого окна свой контекст OpenGL */
    Rml::RenderManager* renderManager = nullptr;
    PlateShape shape;
    /** Сколько ссылок RmlUi держит на эти данные: элементы с той же формой делят одну сетку */
    int users = 1;
  };

  class PlateDecorator final : public Rml::Decorator {
    public:
      PlateDecorator(PLATE_VARIANT variant, PLATE_DETAILS details, Rml::NumericValue chamfer, Rml::Texture texture) :
        variant(variant), details(details), chamfer(chamfer) {
        this->textureIndex = this->AddTexture(texture);
      }

      /**
       * RmlUi просит данные заново после каждой раскладки, в которой у элемента хоть раз менялся размер.
       * Флекс раскладывает элементы сначала на пробу, потом окончательно, поэтому так бывает почти в каждом кадре
       * с раскладкой, даже если итоговый размер прежний. Пластина тогда отдаёт уже построенную сетку:
       * RmlUi освобождает старые данные после получения новых именно ради такого повтора.
       * Сетка задана относительно угла элемента, поэтому одинаковые элементы, например ячейки матрицы, делят одну
       */
      Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea paintArea) const override {
        Rml::RenderManager* renderManager = element->GetRenderManager();

        if (!renderManager) {
          return INVALID_DECORATORDATAHANDLE;
        }

        const Rml::RenderBox box = element->GetRenderBox(paintArea);
        const float scale = Rml::ElementUtilities::GetDensityIndependentPixelRatio(element);
        Rml::Vector2f textureScale(0.0f, 0.0f);

        if (this->textureIndex >= 0) {
          const Rml::Vector2i dimensions = this->GetTexture(this->textureIndex).GetDimensions();

          // Плитка того же размера, что у image(... repeat): пиксель картинки на dp
          if (dimensions.x > 0 && dimensions.y > 0) {
            textureScale = Rml::Vector2f(1.0f / ((float)dimensions.x * scale), 1.0f / ((float)dimensions.y * scale));
          }
        }

        const PlateShape shape = {
          .offset = box.GetFillOffset(),
          .size = box.GetFillSize(),
          .chamfer = element->ResolveLength(this->chamfer),
          .scale = scale,
          .opacity = element->GetComputedValues().opacity(),
          .textureScale = textureScale,
        };
        const auto built = std::ranges::find_if(this->built, [renderManager, &shape](const PlateData* data) {
          return data->renderManager == renderManager && data->shape == shape;
        });

        if (built != this->built.end()) {
          (*built)->users++;

          return reinterpret_cast<Rml::DecoratorDataHandle>(*built);
        }

        PlateMeshes meshes = buildPlate(this->variant, this->details, shape);
        auto* data = new PlateData{
          .base = renderManager->MakeGeometry(std::move(meshes.base)),
          .texture = renderManager->MakeGeometry(std::move(meshes.texture)),
          .top = renderManager->MakeGeometry(std::move(meshes.top)),
          .renderManager = renderManager,
          .shape = shape,
        };

        this->built.push_back(data);

        return reinterpret_cast<Rml::DecoratorDataHandle>(data);
      }

      void ReleaseElementData(Rml::DecoratorDataHandle elementData) const override {
        auto* data = reinterpret_cast<PlateData*>(elementData);

        if (--data->users > 0) {
          return;
        }

        std::erase(this->built, data);
        delete data;
      }

      void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle elementData) const override {
        auto* data = reinterpret_cast<PlateData*>(elementData);
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);

        data->base.Render(offset);

        if (this->textureIndex >= 0) {
          data->texture.Render(offset, this->GetTexture(this->textureIndex));
        }

        data->top.Render(offset);
      }

    private:
      PLATE_VARIANT variant;
      PLATE_DETAILS details;
      Rml::NumericValue chamfer;
      int textureIndex = -1;
      /** Построенные сетки, по одной на форму. Сетка живёт, пока её держит хоть один элемент */
      mutable std::vector<PlateData*> built;
  };
}

PlateMeshes buildPlate(PLATE_VARIANT variant, PLATE_DETAILS details, const PlateShape& shape) {
  const Look look = getLook(variant);
  MeshBuilder base(shape.opacity);
  MeshBuilder texture(shape.opacity, shape.textureScale);
  MeshBuilder top(shape.opacity);
  const float outline = OUTLINE * shape.scale;
  const float bevel = look.bevel * shape.scale;
  const bool hasSize = shape.size.x > 0.0f && shape.size.y > 0.0f;

  if (look.hasFace && hasSize) {
    const Frame feather = insetFrame(shape, -FEATHER_PX);
    const Frame edge = insetFrame(shape, 0.0f);
    const Frame face = insetFrame(shape, outline);
    const Frame bevelEnd = insetFrame(shape, outline + bevel);

    addBand(base, feather, edge, uniform(withAlpha(OUTLINE_COLOR, 0)), uniform(OUTLINE_COLOR));
    addBand(base, edge, face, uniform(OUTLINE_COLOR), uniform(OUTLINE_COLOR));
    addFace(base, look, face, shape.scale);

    if (look.texture && shape.textureScale.x > 0.0f && shape.textureScale.y > 0.0f) {
      const auto corners = face.getCorners();

      texture.addConvexPolygon(corners, single(WHITE));
    }

    EdgeColors bevelOuter;
    EdgeColors bevelInner;
    EdgeColors shadowOuter;

    for (size_t index = 0; index < EDGE_COUNT; index++) {
      const float light = facing(NORMALS[index]);
      const float lit = std::pow(look.isSunken ? 1.0f - light : light, 1.5f);

      bevelOuter[index] = mix(look.dark, look.light, lit);
      bevelInner[index] = withAlpha(bevelOuter[index], 60);
      shadowOuter[index] = withAlpha(BLACK, (Rml::byte)std::lround(150.0f * std::pow(light, 1.5f)));
    }

    if (look.isSunken) {
      addBand(top, face, insetFrame(shape, outline + INNER_SHADOW * shape.scale), shadowOuter, uniform(withAlpha(BLACK, 0)));
    }

    addBand(top, face, bevelEnd, bevelOuter, bevelInner);
  }

  if (hasSize && details == PLATE_DETAILS::SCREWS) {
    addScrews(top, shape, look.hasFace ? outline + bevel : 0.0f);
  }

  if (hasSize && details == PLATE_DETAILS::GUSSETS) {
    addGussets(top, shape);
  }

  if (hasSize && details == PLATE_DETAILS::GRIP) {
    addGrip(top, shape, look.hasFace ? outline + bevel : 0.0f);
  }

  return { base.takeMesh(), texture.takeMesh(), top.takeMesh() };
}

const char* getPlateTexture(PLATE_VARIANT variant) {
  return getLook(variant).texture;
}

PlateDecoratorInstancer::PlateDecoratorInstancer() {
  this->variantId = this->RegisterProperty("variant", "panel")
    .AddParser("keyword", "frame, panel, sunken, button, button-hover, button-active, display, bare")
    .GetId();
  this->chamferId = this->RegisterProperty("chamfer", "0dp").AddParser("length").GetId();
  this->detailsId = this->RegisterProperty("details", "none").AddParser("keyword", "none, screws, gussets, grip").GetId();
  this->RegisterShorthand("decorator", "variant, chamfer, details", Rml::ShorthandType::FallThrough);
}

Rml::SharedPtr<Rml::Decorator> PlateDecoratorInstancer::InstanceDecorator(const Rml::String& /*name*/, const Rml::PropertyDictionary& properties,
  const Rml::DecoratorInstancerInterface& instancerInterface) {
  const Rml::Property* variantProperty = properties.GetProperty(this->variantId);
  const Rml::Property* chamferProperty = properties.GetProperty(this->chamferId);
  const Rml::Property* detailsProperty = properties.GetProperty(this->detailsId);

  if (!variantProperty || !chamferProperty || !detailsProperty) {
    return nullptr;
  }

  const auto variant = (PLATE_VARIANT)variantProperty->Get<int>();
  Rml::Texture texture;

  if (const char* name = getPlateTexture(variant)) {
    texture = instancerInterface.GetTexture(name);
  }

  return Rml::MakeShared<PlateDecorator>(variant, (PLATE_DETAILS)detailsProperty->Get<int>(), chamferProperty->GetNumericValue(), texture);
}
