#pragma once

#include <RmlUi/Core.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "../../engine/modelTypes/modelTypes.h"
#include "../editorBackend/editorBackend.h"

class Knob;
class WireLayer;

/**
 * Редактор: строит блоки, провода и матрицу по модели и переводит действия мыши в команды.
 * Состояние схемы хранится только в модели, здесь лишь её отображение (ТЗ §8.2): команда меняет модель,
 * а вид догоняет её в update, откуда бы изменение ни пришло. Как компонент, который рендерит store.
 * Окна и OpenGL не знает, поэтому проверяется тестами на пустой отрисовке
 */
class EditorView final : public Rml::EventListener {
  public:
    /**
     * requestKeyboard вызывается, когда нужен ввод текста: остальное время клавиатура остаётся у DAW
     */
    EditorView(Rml::ElementDocument* document, EditorBackend& backend, std::function<void()> requestKeyboard);
    ~EditorView() override;

    /**
     * Приводит вид в соответствие с моделью. Возвращает true, если кадр нужно перерисовать
     */
    bool update();

    void ProcessEvent(Rml::Event& event) override;

    /**
     * Какой граф показан: генератор или главный, если пусто
     */
    std::optional<ModuleId> getGraph() const;
    void openGraph(std::optional<ModuleId> graph);

    /**
     * Показанное сообщение об ошибке. Пусто, если его нет
     */
    std::string getMessage() const;

  private:
    using WireRef = std::variant<Connection, RouteKey>;

    enum class DRAG_KIND {
      NONE,
      CANVAS,
      BLOCK,
      KNOB,
      WIRE,
      DEPTH,
    };

    struct KnobView {
      Knob* knob;
      Rml::Element* cell;
      Rml::Element* valueText;
      ModuleId module;
      const ParameterDescriptor* parameter;
      std::string shownText;
    };

    struct RouteView {
      Rml::Element* fill;
      Rml::Element* value;
      RouteKey key;
    };

    /**
     * Откуда тянется провод: порт аудио либо выход модуляции
     */
    struct WireDrag {
      ModuleId module = 0;
      std::string port;
      bool isModulation = false;
      bool isInput = false;
    };

    struct Drag {
      DRAG_KIND kind = DRAG_KIND::NONE;
      Rml::Element* element = nullptr;
      Rml::Vector2f grabOffset{};
      Rml::Vector2f lastMouse{};
      float value = 0.0f; // Положение ручки: копится, чтобы тонкая настройка и ступени не терялись
      size_t index = 0; // Ручка или маршрут
      WireDrag wire{};
    };

    Rml::ElementDocument* document;
    EditorBackend& backend;
    std::function<void()> requestKeyboard;

    Rml::Element* canvas;
    Rml::Element* world;
    Rml::Element* breadcrumbs;
    Rml::Element* sources;
    Rml::Element* macroCells;
    Rml::Element* matrixRows;
    Rml::Element* presetButton;
    Rml::Element* wireRemove;
    Rml::Element* popup;
    Rml::Element* prompt;
    Rml::Element* promptLabel;
    Rml::Element* promptInput;
    Rml::Element* toast;
    WireLayer* wires;
    WireLayer* draft;

    std::optional<ModuleId> graph;
    std::map<std::optional<ModuleId>, Rml::Vector2f> pans;

    uint64_t seenRevision = UINT64_MAX;
    std::string seenStructure;
    std::string seenPresetName;
    bool isViewChanged = false;

    std::vector<KnobView> knobs;
    std::vector<RouteView> routes;
    std::map<ModuleId, Rml::Element*> blocks;
    std::vector<Rml::Element*> ports;
    std::vector<WireRef> wireRefs;
    std::optional<WireRef> selectedWire;

    Drag drag;
    std::vector<std::function<void()>> menuActions;
    std::function<void(const std::string&)> promptAction;
    std::optional<std::chrono::steady_clock::time_point> toastDeadline;

    float getScale() const;
    Rml::Vector2f readMouse(const Rml::Event& event) const;

    std::string makeStructureKey() const;
    void rebuild();
    void refresh();
    void refreshKnob(KnobView& view);
    void refreshRoute(RouteView& view);
    void clearChildren(Rml::Element* element);
    Rml::Element* addElement(Rml::Element* parent, const std::string& tag, const std::string& className, const std::string& text = "");

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
    Rml::Element* findKnobElement(ModuleId module, const std::string& parameter) const;
    bool hasOffCanvasRoute(const ModuleInstance& target, const ParameterDescriptor& parameter) const;
    std::string makeSourceLabel(const ModSource& source) const;
    std::string makeTargetLabel(const ModTarget& target) const;

    void setWorldOffset();
    void setBlockPosition(Rml::Element* block, ModulePosition position);
    void freezeLayout();
    ModulePosition findFreePosition(ModulePosition start) const;

    void handleMouseDown(Rml::Event& event);
    void handleClick(Rml::Event& event);
    void handleDoubleClick(Rml::Event& event);
    void handleDragStart(Rml::Event& event);
    void handleDrag(Rml::Event& event);
    void handleDrop(Rml::Event& event);
    void handleDragEnd();
    void handleScroll(Rml::Event& event);
    void handleKey(Rml::Event& event);
    void runAction(const std::string& action, Rml::Element* element);

    void startWireDrag(Rml::Element* element);
    void markLegalTargets();
    void clearLegalTargets();
    void finishWireDrag(Rml::Element* target);

    void selectWire(std::optional<WireRef> wire);
    void removeSelectedWire();

    void setKnobValue(const KnobView& view, float position);
    void changeKnob(KnobView& view, float delta, bool isFine);
    void promptKnobValue(const KnobView& view);
    KnobView* findKnob(const Rml::Element* element);
    void setDepthFromMouse(float mouseX);

    void showMenu(Rml::Vector2f position, std::vector<std::pair<std::string, std::function<void()>>> items);
    void hideMenu();
    void showModuleMenu(Rml::Vector2f position, ModulePosition place);
    void showPresetMenu();
    void showRouteSourceMenu(Rml::Vector2f position);
    void showRouteTargetMenu(Rml::Vector2f position, ModSource source);

    void showPrompt(const std::string& label, const std::string& value, std::function<void(const std::string&)> action);
    void acceptPrompt();
    void hidePrompt();

    void addModule(const std::string& type, ModulePosition place);
    void savePreset();
    void savePresetAs();
    void report(const std::expected<void, std::string>& result);
    void report(const CommandResult& result);
    void showMessage(const std::string& text);
};
