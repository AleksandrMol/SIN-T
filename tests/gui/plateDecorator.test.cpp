#include <doctest/doctest.h>

#include <RmlUi/Core.h>

#include <cmath>
#include <string>

#include "../../gui/plateDecorator/plateDecorator.h"
#include "./editorFixture.h"

namespace {
  // Пластина 200×80 px со срезом 12 px при масштабе 1: обводка 1 px
  const PlateShape SHAPE = {
    .offset = Rml::Vector2f(10.0f, 20.0f),
    .size = Rml::Vector2f(200.0f, 80.0f),
    .chamfer = 12.0f,
    .scale = 1.0f,
    .opacity = 1.0f,
    .textureScale = Rml::Vector2f(1.0f / 512.0f, 1.0f / 512.0f),
  };

  constexpr float OUTLINE = 1.0f;
  constexpr float EPSILON = 0.01f;

  /**
   * Лежит ли точка на пластине: внутри прямоугольника и не в срезанных углах
   */
  bool isOnPlate(Rml::Vector2f point, float inset) {
    const float left = SHAPE.offset.x + inset;
    const float top = SHAPE.offset.y + inset;
    const float right = SHAPE.offset.x + SHAPE.size.x - inset;
    const float bottom = SHAPE.offset.y + SHAPE.size.y - inset;
    const float chamfer = SHAPE.chamfer - inset * (2.0f - std::sqrt(2.0f));

    return point.x >= left - EPSILON && point.x <= right + EPSILON && point.y >= top - EPSILON && point.y <= bottom + EPSILON
      && (point.x - left) + (point.y - top) >= chamfer - EPSILON
      && (right - point.x) + (point.y - top) >= chamfer - EPSILON
      && (right - point.x) + (bottom - point.y) >= chamfer - EPSILON
      && (point.x - left) + (bottom - point.y) >= chamfer - EPSILON;
  }

  float brightness(const Rml::Vertex& vertex) {
    return (float)vertex.colour.red + (float)vertex.colour.green + (float)vertex.colour.blue;
  }

  /**
   * Самая яркая непрозрачная вершина фаски на линии верхнего или нижнего края.
   * У полосы фаски вершины только на концах рёбер, поэтому в выборку попадают и соседние срезы
   */
  float edgeBrightness(const Rml::Mesh& mesh, float y) {
    const float middle = SHAPE.offset.x + SHAPE.size.x * 0.5f;
    float result = -1.0f;

    for (const Rml::Vertex& vertex : mesh.vertices) {
      if (std::fabs(vertex.position.y - y) < 0.1f && std::fabs(vertex.position.x - middle) <= SHAPE.size.x * 0.5f && vertex.colour.alpha > 200) {
        result = std::max(result, brightness(vertex));
      }
    }

    return result;
  }
}

TEST_CASE("Текстура пластины лежит только на пластине и не заходит в срезанные углы") {
  const PlateMeshes meshes = buildPlate(PLATE_VARIANT::PANEL, PLATE_DETAILS::NONE, SHAPE);

  REQUIRE_FALSE(meshes.texture.vertices.empty());

  bool isInside = true;

  for (const Rml::Vertex& vertex : meshes.texture.vertices) {
    isInside = isInside && isOnPlate(vertex.position, OUTLINE);
    // Плитка привязана к углу элемента, как у image(... repeat)
    isInside = isInside && std::fabs(vertex.tex_coord.x - vertex.position.x / 512.0f) < 1e-5f;
  }

  CHECK(isInside);

  // Лицевая сторона тоже обрезана по срезу, а не прямоугольник под текстурой
  bool isFaceInside = true;

  for (const Rml::Vertex& vertex : meshes.base.vertices) {
    // Мягкий край на пиксель шире пластины: только он может выходить наружу
    isFaceInside = isFaceInside && (vertex.colour.alpha == 0 || isOnPlate(vertex.position, 0.0f));
  }

  CHECK(isFaceInside);
}

TEST_CASE("Без координат текстуры и у пластины без металла текстуры нет") {
  PlateShape shape = SHAPE;

  shape.textureScale = Rml::Vector2f(0.0f, 0.0f);

  CHECK(buildPlate(PLATE_VARIANT::PANEL, PLATE_DETAILS::NONE, shape).texture.vertices.empty());
  CHECK(buildPlate(PLATE_VARIANT::DISPLAY, PLATE_DETAILS::NONE, SHAPE).texture.vertices.empty());
  CHECK(getPlateTexture(PLATE_VARIANT::DISPLAY) == nullptr);
  CHECK(std::string(getPlateTexture(PLATE_VARIANT::FRAME)) == "sint:worn");
}

TEST_CASE("Фаска выпуклой пластины светлая сверху, утопленной — снизу") {
  const float top = SHAPE.offset.y + OUTLINE;
  const float bottom = SHAPE.offset.y + SHAPE.size.y - OUTLINE;
  const PlateMeshes raised = buildPlate(PLATE_VARIANT::PANEL, PLATE_DETAILS::NONE, SHAPE);
  const PlateMeshes sunken = buildPlate(PLATE_VARIANT::SUNKEN, PLATE_DETAILS::NONE, SHAPE);

  REQUIRE(edgeBrightness(raised.top, top) >= 0.0f);
  REQUIRE(edgeBrightness(raised.top, bottom) >= 0.0f);
  CHECK(edgeBrightness(raised.top, top) > edgeBrightness(raised.top, bottom));
  CHECK(edgeBrightness(sunken.top, bottom) > edgeBrightness(sunken.top, top));
}

TEST_CASE("Винты и косынки появляются, только если пластине хватает места") {
  const size_t plain = buildPlate(PLATE_VARIANT::PANEL, PLATE_DETAILS::NONE, SHAPE).top.vertices.size();

  CHECK(buildPlate(PLATE_VARIANT::PANEL, PLATE_DETAILS::SCREWS, SHAPE).top.vertices.size() > plain);

  PlateShape small = SHAPE;

  small.size = Rml::Vector2f(40.0f, 20.0f);

  const size_t smallPlain = buildPlate(PLATE_VARIANT::BUTTON, PLATE_DETAILS::NONE, small).top.vertices.size();

  CHECK(buildPlate(PLATE_VARIANT::BUTTON, PLATE_DETAILS::SCREWS, small).top.vertices.size() == smallPlain);

  // Только детали: сама пластина не рисуется
  const PlateMeshes bare = buildPlate(PLATE_VARIANT::BARE, PLATE_DETAILS::NONE, SHAPE);

  CHECK(bare.base.vertices.empty());
  CHECK(bare.texture.vertices.empty());
  CHECK(bare.top.vertices.empty());

  PlateShape screen = SHAPE;

  screen.size = Rml::Vector2f(400.0f, 300.0f);

  CHECK_FALSE(buildPlate(PLATE_VARIANT::BARE, PLATE_DETAILS::GUSSETS, screen).top.vertices.empty());
}

TEST_CASE("Без среза пластина прямоугольная") {
  PlateShape shape = SHAPE;

  shape.chamfer = 0.0f;

  const PlateMeshes meshes = buildPlate(PLATE_VARIANT::PANEL, PLATE_DETAILS::NONE, shape);
  bool hasCorner = false;

  for (const Rml::Vertex& vertex : meshes.texture.vertices) {
    hasCorner = hasCorner || (std::fabs(vertex.position.x - (shape.offset.x + OUTLINE)) < EPSILON && std::fabs(vertex.position.y - (shape.offset.y + OUTLINE)) < EPSILON);
  }

  CHECK(hasCorner);
}

TEST_CASE_FIXTURE(EditorFixture, "Пластины в стилях редактора разбираются декоратором plate") {
  for (const char* selector : { "#logo-plate", "#macros", "#macros-well", "#matrix-rows", "#screen", "[data-action=\"save\"]" }) {
    CAPTURE(selector);

    const Rml::Property* property = this->get(selector)->GetProperty("decorator");

    REQUIRE(property != nullptr);

    const Rml::DecoratorsPtr decorators = property->Get<Rml::DecoratorsPtr>();

    REQUIRE(decorators);
    REQUIRE_FALSE(decorators->list.empty());
    CHECK(decorators->list[0].type == "plate");
    // Без зарегистрированного декоратора RmlUi оставил бы объявление без исполнителя и ничего не нарисовал
    CHECK(decorators->list[0].instancer != nullptr);
  }
}
