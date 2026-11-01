#pragma once

#include <RmlUi/Core.h>

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
 * Слой проводов (ТЗ §8.1). Аудио — сплошные линии, модуляция — оранжевый пунктир, стрелка показывает направление
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

  protected:
    void OnRender() override;

  private:
    std::vector<Wire> wires;
    Rml::Element* draftFrom = nullptr;
    WIRE_KIND draftKind = WIRE_KIND::AUDIO;
    Rml::Vector2f draftPoint;
};
