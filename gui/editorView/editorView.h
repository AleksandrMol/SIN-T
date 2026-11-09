#pragma once

#include <RmlUi/Core.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "../../engine/modelTypes/modelTypes.h"
#include "../editorBackend/editorBackend.h"
#include "../editorOverlays/editorOverlays.h"
#include "../editorScene/editorScene.h"

class WireLayer;

/**
 * Редактор: показывает модель через EditorScene и переводит действия мыши в команды.
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

    const EditorScene& getScene() const;

  private:
    enum class DRAG_KIND {
      NONE,
      CANVAS,
      BLOCK,
      KNOB,
      WIRE,
      DEPTH,
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
      ModTarget knob{}; // Ссылки по ключу, а не номер в списке: номера меняются вместе со схемой
      RouteKey route{};
      WireDrag wire{};
    };

    Rml::ElementDocument* document;
    EditorBackend& backend;
    EditorScene scene;
    EditorOverlays overlays;

    Rml::Element* canvas;
    Rml::Element* grid;
    Rml::Element* world;
    Rml::Element* presetButton;
    Rml::Element* wireRemove;
    WireLayer* wires;
    WireLayer* draft;

    std::optional<ModuleId> graph;
    std::map<std::optional<ModuleId>, Rml::Vector2f> pans;

    uint64_t seenRevision = UINT64_MAX;
    std::string seenStructure;
    bool isViewChanged = false;

    std::optional<WireRef> selectedWire;

    Drag drag;

    float getScale() const;
    Rml::Vector2f readMouse(const Rml::Event& event) const;

    void rebuild();

    /**
     * Сдвигает схему на прокрутку показанного графа, а сетку под ней — на остаток от прокрутки
     */
    void setWorldOffset();
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

    void restoreSelectedWire();
    void selectWire(std::optional<WireRef> wire);
    void removeSelectedWire();

    void setKnobValue(const KnobView& view, float position);
    void changeKnob(const KnobView& view, float delta, bool isFine);
    void promptKnobValue(const KnobView& view);
    void setDepthFromMouse(float mouseX);

    void showModuleMenu(Rml::Vector2f position, ModulePosition place);
    void showPresetMenu();
    void showRouteSourceMenu(Rml::Vector2f position);
    void showRouteTargetMenu(Rml::Vector2f position, ModSource source);

    void addModule(const std::string& type, ModulePosition place);
    void savePreset();
    void savePresetAs();
    void report(const std::expected<void, std::string>& result);
    void report(const CommandResult& result);
    void showMessage(const std::string& text);
};
