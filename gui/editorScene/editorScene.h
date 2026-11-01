#pragma once

#include <RmlUi/Core.h>

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../../engine/modelTypes/modelTypes.h"
#include "../editorBackend/editorBackend.h"

class Knob;
class WireLayer;

using WireRef = std::variant<Connection, RouteKey>;

struct KnobView {
  Knob* knob;
  Rml::Element* cell;
  Rml::Element* valueText;
  ModuleId module;
  const ParameterDescriptor* parameter;
  std::string shownText;
};

struct RouteView {
  Rml::Element* row;
  Rml::Element* fill;
  Rml::Element* value;
  RouteKey key;
};

/**
 * Отображение модели в элементы, как render компонента: блоки, ручки, порты, провода, макросы и матрица.
 * Команд не отправляет и про мышь не знает. Какой граф показан, решает EditorView (ТЗ §8.2)
 */
class EditorScene final {
  public:
    EditorScene(Rml::ElementDocument* document, EditorBackend& backend);

    /**
     * Всё, что требует перестроить элементы. Значения параметров, глубины и положения сюда не входят:
     * они меняются на месте, поэтому ручка не пересоздаётся под мышью
     */
    std::string makeStructureKey(std::optional<ModuleId> graph) const;

    void rebuild(std::optional<ModuleId> graph);

    /**
     * Переносит значения модели на готовые элементы. heldBlock ведёт мышь: его положение уйдёт в модель, когда его отпустят
     */
    void refresh(const Rml::Element* heldBlock);

    const std::vector<KnobView>& getKnobs() const;
    const std::vector<Rml::Element*>& getPorts() const;
    const std::vector<WireRef>& getWireRefs() const;

    const KnobView* findKnob(const Rml::Element* element) const;
    const KnobView* findKnob(const ModTarget& target) const;
    const KnobView* findKnobByCell(const Rml::Element* cell) const;

    /**
     * Строка матрицы, в которой лежит элемент
     */
    const RouteView* findRoute(Rml::Element* element) const;

    void setBlockPosition(Rml::Element* block, ModulePosition position);

    std::string makeSourceLabel(const ModSource& source) const;
    std::string makeTargetLabel(const ModTarget& target) const;

  private:
    EditorBackend& backend;

    Rml::Element* world;
    Rml::Element* breadcrumbs;
    Rml::Element* sources;
    Rml::Element* macroCells;
    Rml::Element* matrixRows;
    Rml::Element* presetButton;
    WireLayer* wires;

    std::optional<ModuleId> graph;
    std::string seenPresetName;

    std::vector<KnobView> knobs;
    std::vector<RouteView> routes;
    std::map<ModuleId, Rml::Element*> blocks;
    std::vector<Rml::Element*> ports;
    std::vector<WireRef> wireRefs;

    void refreshKnob(KnobView& view);
    void refreshRoute(RouteView& view);

    void buildBreadcrumbs();
    void buildSources();
    void buildBlocks();
    void buildBlock(const ModuleInstance& module, ModulePosition position);
    void buildKnob(Rml::Element* parent, const ModuleInstance& module, const ParameterDescriptor& parameter);
    void buildPorts(Rml::Element* block, const ModuleInstance& module);
    Rml::Element* addPort(Rml::Element* parent, ModuleId module, const std::string& port, const std::string& className);
    void buildWires();
    void buildMacros();
    void buildMatrix();

    Rml::Element* findPort(ModuleId module, const std::string& port, bool isModulation, bool isInput) const;
    bool hasOffCanvasRoute(const ModuleInstance& target, const ParameterDescriptor& parameter) const;
};
