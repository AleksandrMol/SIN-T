#pragma once

#include <RmlUi/Core.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "../wireGeometry/wireGeometry.h"

enum class WIRE_KIND {
  AUDIO,
  MODULATION,
};

/**
 * Провод между двумя элементами. Концы берутся из их положения при каждой отрисовке,
 * поэтому провод сам идёт за передвинутым блоком
 */
struct Wire {
  Rml::Element* from;
  Rml::Element* to;
  WIRE_KIND kind;
  bool isSelected = false;
};

/**
 * Слой проводов (ТЗ §8.1). Аудио — сплошные линии, модуляция — оранжевый пунктир, стрелка показывает направление.
 * Под каждым проводом тёмная подложка для контраста. Порты лежат выше слоя: провод уходит под гнездо.
 * Концы читаются на каждом кадре, а сетка считается только для изменившихся проводов (D-090)
 */
class WireLayer : public Rml::Element {
  public:
    using Rml::Element::Element;

    void setWires(std::vector<Wire> wires);
    const std::vector<Wire>& getWires() const;
    void selectWire(std::optional<size_t> index);

    /**
     * Провод, который пользователь тянет мышью: второй конец пока не прикреплён
     */
    void setDraft(Rml::Element* from, WIRE_KIND kind);
    void moveDraft(Rml::Vector2f point);
    void clearDraft();

    /**
     * Ближайший провод не дальше tolerance от точки в координатах окна
     */
    std::optional<size_t> findWire(Rml::Vector2f point, float tolerance) const;

    /**
     * Середина провода: там стоит кнопка удаления
     */
    Rml::Vector2f findMiddle(size_t index) const;

    /**
     * Сетка, нарисованная на последнем кадре
     */
    const Rml::Mesh& getMesh() const;

    /**
     * Та же сетка, посчитанная с нуля. В работе не нужна: по ней проверяется, что готовые куски не устарели
     */
    Rml::Mesh buildMesh() const;

    /**
     * Счётчики для проверки: сколько раз считалась сетка одного провода и собиралась общая
     */
    uint64_t getShapedWireCount() const;
    uint64_t getAssembledMeshCount() const;

  protected:
    void OnRender() override;

  private:
    /**
     * Всё, от чего зависит вид провода. Концы здесь уже числами: элементы могли сдвинуться
     */
    struct WireShape {
      Rml::Vector2f start;
      Rml::Vector2f end;
      WIRE_KIND kind;
      bool isSelected;

      bool operator==(const WireShape&) const = default;
    };

    /**
     * Сетка провода относительно его начала: сдвиг всего провода её не меняет,
     * поэтому при движении холста или блока с обоими концами считать нечего
     */
    struct ShapedWire {
      WireShape shape;
      Rml::Mesh mesh;
    };

    std::vector<Wire> wires;
    Rml::Element* draftFrom = nullptr;
    WIRE_KIND draftKind = WIRE_KIND::AUDIO;
    Rml::Vector2f draftPoint;

    std::vector<ShapedWire> shapedWires;
    float shapedScale = 0.0f;
    Rml::Geometry geometry;
    uint64_t shapedWireCount = 0;
    uint64_t assembledMeshCount = 0;

    std::vector<WireShape> collectShapes() const;
    float getScale() const;
    void syncGeometry();
};
