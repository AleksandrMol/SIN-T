#pragma once

#include <RmlUi/Core.h>

class MeshBuilder;

/**
 * Свой элемент, который рисует себя сеткой: ручка, поле, мини-экран (D-094, D-108).
 * Сетка строится заново только после markDirty или смены размера, в остальных кадрах рисуется готовая
 */
class MeshElement : public Rml::Element {
  public:
    using Rml::Element::Element;

  protected:
    void OnRender() override;
    void OnResize() override;

    /**
     * Данные для рисования изменились: на следующем кадре сетка строится заново
     */
    void markDirty();

    /**
     * Рисует элемент от угла его рамки. scale — dp в пикселях
     */
    virtual void build(MeshBuilder& builder, Rml::Vector2f size, float scale) = 0;

  private:
    bool isDirty = true;
    Rml::Geometry geometry;
};
