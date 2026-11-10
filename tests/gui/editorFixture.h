#pragma once

#include <doctest/doctest.h>

#include <RmlUi/Core.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../clap/clap.h"
#include "../../gui/editorDocument/editorDocument.h"
#include "../../gui/editorView/editorView.h"
#include "../../gui/rmlRuntime/rmlRuntime.h"
#include "../../plugin/plugin.h"
#include "../../plugin/pluginEditorBackend.h"

inline constexpr int WIDTH = 1200;
inline constexpr int HEIGHT = 780;
inline constexpr int RIGHT_BUTTON = 1;

/**
 * Отрисовка в никуда: разметка, стили и ввод настоящие, а окно и OpenGL не нужны
 */
class NullRenderer final : public Rml::RenderInterface {
  public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override {
      return 1;
    }

    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}

    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override {
      return 0;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
      return 1;
    }

    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

struct SentEvent {
  uint16_t type;
  clap_id slot;
};

/**
 * Настоящий плагин с настоящим редактором. Мышь ходит через те же вызовы, что и в окне
 */
struct EditorFixture {
  clap_host_t host{};
  clap_host_params_t hostParams{};
  clap_host_state_t hostState{};
  const clap_plugin_t* plugin = nullptr;
  MyPluginInstance* instance = nullptr;
  std::unique_ptr<PluginEditorBackend> backend;
  NullRenderer renderer;
  Rml::Context* context = nullptr;
  Rml::ElementDocument* document = nullptr;
  std::unique_ptr<EditorView> view;
  std::vector<SentEvent> sent;
  int keyboardRequests = 0;
  int dirtyMarks = 0;

  EditorFixture() {
    this->host.clap_version = CLAP_VERSION;
    this->host.host_data = this;
    this->host.request_callback = [](const clap_host_t*) {};
    this->host.get_extension = [](const clap_host_t* host, const char* id) -> const void* {
      auto* self = (EditorFixture*)host->host_data;

      if (!std::strcmp(id, CLAP_EXT_PARAMS)) {
        return &self->hostParams;
      }

      return !std::strcmp(id, CLAP_EXT_STATE) ? &self->hostState : nullptr;
    };
    this->hostParams.rescan = [](const clap_host_t*, clap_param_rescan_flags) {};
    this->hostParams.clear = [](const clap_host_t*, clap_id, clap_param_clear_flags) {};
    this->hostParams.request_flush = [](const clap_host_t*) {};
    this->hostState.mark_dirty = [](const clap_host_t* host) {
      ((EditorFixture*)host->host_data)->dirtyMarks++;
    };

    const auto* factory = (const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);

    this->plugin = factory->create_plugin(factory, &this->host, factory->get_plugin_descriptor(factory, 0)->id);
    REQUIRE(this->plugin->init(this->plugin));

    this->instance = (MyPluginInstance*)this->plugin;
    this->backend = std::make_unique<PluginEditorBackend>(this->instance);

    acquireRmlRuntime();

    this->context = Rml::CreateContext("test", Rml::Vector2i(WIDTH, HEIGHT), &this->renderer);
    this->document = this->context->LoadDocumentFromMemory(EDITOR_DOCUMENT);

    REQUIRE(this->document != nullptr);

    this->document->Show();
    this->view = std::make_unique<EditorView>(this->document, *this->backend, [this] {
      this->keyboardRequests++;
    });
    this->frame();
  }

  ~EditorFixture() {
    this->view.reset();
    Rml::RemoveContext("test");
    Rml::ReleaseRenderManagers();
    releaseRmlRuntime();
    this->plugin->destroy(this->plugin);
  }

  const SynthModel& model() const {
    return *this->instance->model;
  }

  ModuleId findModule(const std::string& type) const {
    for (const auto& [id, module] : this->model().getModules()) {
      if (module.descriptor->type == type) {
        return id;
      }
    }

    FAIL("Нет модуля " << type);
    return 0;
  }

  float value(ModuleId module, const std::string& parameter) const {
    return this->model().findModule(module)->parameters.at(parameter);
  }

  void apply(const Command& command) {
    REQUIRE(my_plug_apply_command(this->instance, command).has_value());
    this->frame();
  }

  void frame() {
    this->view->update();
    this->context->Update();
    this->context->Render();
    this->view->update();
    this->context->Update();
  }

  Rml::Element* find(const std::string& selector) const {
    return this->document->QuerySelector(selector);
  }

  Rml::Element* get(const std::string& selector) const {
    Rml::Element* element = this->find(selector);

    REQUIRE_MESSAGE(element != nullptr, "Нет элемента " << selector);

    return element;
  }

  int count(const std::string& selector) const {
    Rml::ElementList elements;

    this->document->QuerySelectorAll(elements, selector);

    return (int)elements.size();
  }

  Rml::Element* block(ModuleId module) const {
    return this->get(".block[data-module=\"" + std::to_string(module) + "\"]");
  }

  /**
   * Ячейка параметра: у ручки это обёртка с подписью, у поля само поле (D-108)
   */
  Rml::Element* cell(ModuleId module, const std::string& parameter) const {
    return this->get("[data-module=\"" + std::to_string(module) + "\"][data-parameter=\"" + parameter + "\"]");
  }

  /**
   * То, что тянут мышью: ручка или поле
   */
  Rml::Element* knob(ModuleId module, const std::string& parameter) const {
    Rml::Element* cell = this->cell(module, parameter);

    return cell->HasAttribute("data-drag") ? cell : cell->QuerySelector("knob");
  }

  /**
   * Текст значения у ручки или поля
   */
  std::string shownValue(ModuleId module, const std::string& parameter) const {
    Rml::Element* cell = this->cell(module, parameter);
    Rml::Element* value = cell->QuerySelector(".knob-value");

    return this->text(value ? value : cell->QuerySelector(".field-value"));
  }

  Rml::Element* port(ModuleId module, const std::string& port, const std::string& kind) const {
    return this->get(".port." + kind + "[data-module=\"" + std::to_string(module) + "\"][data-port=\"" + port + "\"]");
  }

  Rml::Vector2f center(Rml::Element* element) const {
    return element->GetAbsoluteOffset(Rml::BoxArea::Border) + element->GetBox().GetSize(Rml::BoxArea::Border) * 0.5f;
  }

  void moveMouse(Rml::Vector2f point, int modifiers = 0) {
    this->context->ProcessMouseMove((int)point.x, (int)point.y, modifiers);
    this->frame();
  }

  void press(int button = 0) {
    this->context->ProcessMouseButtonDown(button, 0);
    this->frame();
  }

  void release(int button = 0) {
    this->context->ProcessMouseButtonUp(button, 0);
    this->frame();
  }

  void click(Rml::Element* element, int button = 0) {
    this->moveMouse(this->center(element));
    this->press(button);
    this->release(button);

    // Иначе следующий клик рядом по времени превратился бы в двойной
    this->context->ProcessMouseMove(1, 1, 0);
    this->context->Update();
  }

  void doubleClick(Rml::Element* element) {
    const Rml::Vector2f point = this->center(element);

    this->moveMouse(point);
    this->press();
    this->release();
    this->press();
    this->release();
  }

  void dragTo(Rml::Vector2f from, Rml::Vector2f to, int modifiers = 0) {
    this->moveMouse(from);
    this->press();
    this->moveMouse(from + (to - from) * 0.5f, modifiers);
    this->moveMouse(to, modifiers);
    this->release();
  }

  void drag(Rml::Element* from, Rml::Element* to) {
    this->dragTo(this->center(from), this->center(to));
  }

  void clickMenu(const std::string& label) {
    Rml::ElementList items;

    this->document->QuerySelectorAll(items, ".menu-item");

    for (Rml::Element* item : items) {
      if (item->GetInnerRML() == label) {
        this->click(item);
        return;
      }
    }

    FAIL("Нет пункта меню " << label);
  }

  std::vector<SentEvent> drainHostEvents() {
    const clap_output_events_t output = {
      .ctx = this,
      .try_push = [](const clap_output_events_t* list, const clap_event_header_t* event) {
        auto* self = (EditorFixture*)list->ctx;

        if (event->type == CLAP_EVENT_PARAM_VALUE) {
          self->sent.push_back({ event->type, ((const clap_event_param_value_t*)event)->param_id });
        } else {
          self->sent.push_back({ event->type, ((const clap_event_param_gesture_t*)event)->param_id });
        }

        return true;
      },
    };

    my_plug_send_host_events(this->instance, &output);

    return std::exchange(this->sent, {});
  }

  std::string text(Rml::Element* element) const {
    return element->GetInnerRML();
  }

  ModuleId voiceOutput() const {
    for (const auto& [id, module] : this->model().getModules()) {
      if (module.owner && module.descriptor->role == MODULE_ROLE::OUTPUT) {
        return id;
      }
    }

    FAIL("Нет выхода голоса");
    return 0;
  }

  void openGenerator() {
    this->view->openGraph(this->findModule("generator"));
    this->frame();
  }
};

inline bool hasConnection(const SynthModel& model, const Connection& connection) {
  return std::ranges::contains(model.getConnections(), connection);
}

inline const ModRoute* findRoute(const SynthModel& model, ModuleId source, ModuleId target, const std::string& parameter) {
  for (const ModRoute& route : model.getRoutes()) {
    if (route.source.module == source && route.target.module == target && route.target.parameter == parameter) {
      return &route;
    }
  }

  return nullptr;
}
