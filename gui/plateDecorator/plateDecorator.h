#pragma once

#include <RmlUi/Core.h>

/**
 * Вид пластины. Порядок совпадает с ключевыми словами в стилях
 */
enum class PLATE_VARIANT {
  FRAME,          // толстая рама из потёртого металла
  PANEL,          // выпуклая пластина панели
  SUNKEN,         // утопленная тёмная область внутри панели
  BUTTON,
  BUTTON_HOVER,
  BUTTON_ACTIVE,  // кнопка нажата: синяя и утоплена
  DISPLAY,        // тёмное стекло встроенного экрана
  BARE,           // только детали, без самой пластины
};

enum class PLATE_DETAILS {
  NONE,
  SCREWS,   // винты в четырёх углах
  GUSSETS,  // треугольные косынки с винтом в четырёх углах
};

struct PlateShape {
  /** Прямоугольник пластины относительно угла рамки элемента, px */
  Rml::Vector2f offset;
  Rml::Vector2f size;
  /** Длина среза угла под 45°, px. Ноль — прямые углы */
  float chamfer = 0.0f;
  /** dp в пикселях: толщина фаски, винты и косынки растут вместе с масштабом окна */
  float scale = 1.0f;
  float opacity = 1.0f;
  /** Перевод пикселей в координаты текстуры, ноль — без текстуры */
  Rml::Vector2f textureScale;
};

/**
 * Пластина — три слоя: основа (обводка и лицевая сторона), текстура металла поверх лицевой стороны
 * и верх (фаска, внутренняя тень, винты). Текстура лежит только на пластине и не заходит в срезанные углы
 */
struct PlateMeshes {
  Rml::Mesh base;
  Rml::Mesh texture;
  Rml::Mesh top;
};

PlateMeshes buildPlate(PLATE_VARIANT variant, PLATE_DETAILS details, const PlateShape& shape);

/**
 * Имя текстуры металла для вида пластины, пустой указатель — без текстуры
 */
const char* getPlateTexture(PLATE_VARIANT variant);

/**
 * Металлическая пластина со срезанными углами, фаской и винтами, как на референсе D-093.
 * В CSS RmlUi углы только скругляются, а маска рисуется через отдельный слой на каждом кадре,
 * поэтому пластина — свой декоратор, как свой background: decorator: plate(panel 10dp screws).
 * Свет сверху слева, как у ручек и логотипа
 */
class PlateDecoratorInstancer final : public Rml::DecoratorInstancer {
  public:
    PlateDecoratorInstancer();

    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String& name, const Rml::PropertyDictionary& properties,
      const Rml::DecoratorInstancerInterface& instancerInterface) override;

  private:
    Rml::PropertyId variantId;
    Rml::PropertyId chamferId;
    Rml::PropertyId detailsId;
};
