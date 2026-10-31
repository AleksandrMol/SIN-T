#include <doctest/doctest.h>

#include <RmlUi/Core.h>

#include <algorithm>
#include <cmath>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../clap/clap.h"
#include "../../engine/preset/preset.h"
#include "../../gui/editorDocument/editorDocument.h"
#include "../../gui/editorView/editorView.h"
#include "../../gui/knob/knob.h"
#include "../../gui/rmlRuntime/rmlRuntime.h"
#include "../../plugin/plugin.h"
#include "../../plugin/pluginEditorBackend.h"

namespace {
  constexpr int WIDTH = 1200;
  constexpr int HEIGHT = 780;
  constexpr int RIGHT_BUTTON = 1;

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

    Rml::Element* cell(ModuleId module, const std::string& parameter) const {
      return this->get(".knob-cell[data-module=\"" + std::to_string(module) + "\"][data-parameter=\"" + parameter + "\"]");
    }

    Rml::Element* knob(ModuleId module, const std::string& parameter) const {
      return this->cell(module, parameter)->QuerySelector("knob");
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

  bool hasConnection(const SynthModel& model, const Connection& connection) {
    return std::ranges::contains(model.getConnections(), connection);
  }

  const ModRoute* findRoute(const SynthModel& model, ModuleId source, ModuleId target, const std::string& parameter) {
    for (const ModRoute& route : model.getRoutes()) {
      if (route.source.module == source && route.target.module == target && route.target.parameter == parameter) {
        return &route;
      }
    }

    return nullptr;
  }
}

TEST_CASE_FIXTURE(EditorFixture, "Редактор показывает главный граф, а внутри генератора его подграф (D-063)") {
  const ModuleId generator = this->findModule("generator");
  const ModuleId filter = this->findModule("filter");

  CHECK(this->view->getGraph() == std::nullopt);
  CHECK(this->find(".block[data-module=\"" + std::to_string(generator) + "\"]") != nullptr);
  CHECK(this->find(".block[data-module=\"" + std::to_string(filter) + "\"]") == nullptr);

  this->doubleClick(this->block(generator)->QuerySelector(".block-title"));

  CHECK(this->view->getGraph() == generator);
  CHECK(this->find(".block[data-module=\"" + std::to_string(filter) + "\"]") != nullptr);
  CHECK(this->find(".block[data-module=\"" + std::to_string(generator) + "\"]") == nullptr);
  CHECK(this->text(this->get(".crumb.current")) == "Generator 1");

  this->click(this->get(".crumb[data-action]"));

  CHECK(this->view->getGraph() == std::nullopt);
  CHECK(this->text(this->get(".crumb.current")) == "Main");
}

TEST_CASE_FIXTURE(EditorFixture, "Кнопка Open тоже открывает подграф (D-063)") {
  this->click(this->get("[data-action=\"open\"]"));
  this->frame();

  CHECK(this->view->getGraph() == this->findModule("generator"));
}

TEST_CASE_FIXTURE(EditorFixture, "Ручки строятся по описанию модуля и показывают значения с подписями (ТЗ §12, D-066)") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");

  CHECK(this->count(".knob-cell[data-module=\"" + std::to_string(oscillator) + "\"]") == 4);
  CHECK(this->text(this->cell(oscillator, "wave")->QuerySelector(".knob-value")) == "Saw");
  CHECK(this->text(this->cell(this->findModule("filter"), "cutoff")->QuerySelector(".knob-value")) == "1000 Hz");
  CHECK(this->text(this->cell(this->findModule("amp"), "velocity")->QuerySelector(".knob-name")) == "Vel sens");
  CHECK(this->count("#macro-cells .knob-cell") == 8);
}

TEST_CASE_FIXTURE(EditorFixture, "Выход и макросы нельзя удалить из редактора: у них нет кнопок (D-034, D-055)") {
  const ModuleId output = this->model().getOutputId();

  CHECK(this->block(output)->QuerySelector("[data-action=\"remove\"]") == nullptr);
  CHECK(this->block(output)->QuerySelector("[data-action=\"toggle\"]") == nullptr);
}

TEST_CASE_FIXTURE(EditorFixture, "Движение ручки вверх увеличивает значение, хост получает начало, значения и конец (D-067)") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");
  const clap_id slot = *this->instance->slots.findSlot(filter, "cutoff");
  const float before = this->value(filter, "cutoff");
  const Rml::Vector2f start = this->center(this->knob(filter, "cutoff"));

  this->drainHostEvents();
  this->moveMouse(start);
  this->press();
  this->moveMouse(start + Rml::Vector2f(0.0f, -30.0f));

  CHECK(this->value(filter, "cutoff") > before);

  const std::vector<SentEvent> started = this->drainHostEvents();

  REQUIRE(started.size() == 2);
  CHECK(started[0].type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
  CHECK(started[1].type == CLAP_EVENT_PARAM_VALUE);
  CHECK(started[1].slot == slot);

  this->moveMouse(start + Rml::Vector2f(0.0f, -60.0f));
  this->release();

  const std::vector<SentEvent> finished = this->drainHostEvents();

  REQUIRE(finished.size() == 2);
  CHECK(finished[0].type == CLAP_EVENT_PARAM_VALUE);
  CHECK(finished[1].type == CLAP_EVENT_PARAM_GESTURE_END);

  // Текст под ручкой идёт за значением
  CHECK(this->text(this->cell(filter, "cutoff")->QuerySelector(".knob-value")) == formatParameter(*this->instance->slots.find(slot)->descriptor, this->value(filter, "cutoff")));
}

TEST_CASE_FIXTURE(EditorFixture, "Значение идёт на всю шкалу и упирается в границу") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");
  const Rml::Vector2f start = this->center(this->knob(filter, "cutoff"));

  this->moveMouse(start);
  this->press();

  for (int step = 1; step <= 6; step++) {
    this->moveMouse(start + Rml::Vector2f(0.0f, -100.0f * (float)step));
  }

  CHECK(this->value(filter, "cutoff") == 20000.0f);

  for (int step = 1; step <= 8; step++) {
    this->moveMouse(start + Rml::Vector2f(0.0f, -500.0f + 100.0f * (float)step));
  }

  this->release();

  CHECK(this->value(filter, "cutoff") < 1000.0f);
}

TEST_CASE_FIXTURE(EditorFixture, "Shift даёт тонкую настройку") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");
  const Rml::Vector2f start = this->center(this->knob(filter, "cutoff"));
  const ParameterDescriptor& parameter = *this->instance->slots.find(*this->instance->slots.findSlot(filter, "cutoff"))->descriptor;
  const float before = toNormalized(parameter, this->value(filter, "cutoff"));

  this->moveMouse(start);
  this->press();
  this->moveMouse(start + Rml::Vector2f(0.0f, -44.0f), Rml::Input::KM_SHIFT);
  this->release();

  const float fine = toNormalized(parameter, this->value(filter, "cutoff")) - before;

  CHECK(fine == doctest::Approx(0.02f).epsilon(0.05));
}

TEST_CASE_FIXTURE(EditorFixture, "Ступенчатая ручка не теряет мелкие движения и выдаёт только целые значения") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const Rml::Vector2f start = this->center(this->knob(oscillator, "wave"));

  this->moveMouse(start);
  this->press();

  // До верхней ступени нужно пройти 0.25 шкалы, это 55 точек
  for (int step = 1; step <= 60; step++) {
    this->moveMouse(start + Rml::Vector2f(0.0f, -(float)step));

    const float value = this->value(oscillator, "wave");

    CHECK(value == std::round(value));
  }

  this->release();

  CHECK(this->value(oscillator, "wave") == 4.0f);
  CHECK(this->text(this->cell(oscillator, "wave")->QuerySelector(".knob-value")) == "Triangle");
}

TEST_CASE_FIXTURE(EditorFixture, "Двойной клик возвращает значение, с которым параметр загружен (D-061)") {
  this->openGenerator();

  const ModuleId amp = this->findModule("amp");
  const ModuleId filter = this->findModule("filter");

  this->apply(SetParameter{ filter, "cutoff", 300.0f });
  this->apply(SetParameter{ amp, "gain", 0.5f });

  this->doubleClick(this->knob(filter, "cutoff"));
  this->doubleClick(this->knob(amp, "gain"));

  CHECK(this->value(filter, "cutoff") == 1000.0f);

  // В описании 1, а в пресете 0: сброс на 1 сломал бы огибающую (D-043)
  CHECK(this->value(amp, "gain") == 0.0f);
}

TEST_CASE_FIXTURE(EditorFixture, "Колесо двигает ручку, ступенчатую ровно на одну ступень") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const ModuleId filter = this->findModule("filter");
  const float cutoff = this->value(filter, "cutoff");

  this->moveMouse(this->center(this->knob(filter, "cutoff")));
  this->context->ProcessMouseWheel(Rml::Vector2f(0.0f, -1.0f), 0);
  this->frame();
  CHECK(this->value(filter, "cutoff") > cutoff);

  this->moveMouse(this->center(this->knob(oscillator, "wave")));
  this->context->ProcessMouseWheel(Rml::Vector2f(0.0f, 1.0f), 0);
  this->frame();
  CHECK(this->value(oscillator, "wave") == 2.0f);
}

TEST_CASE_FIXTURE(EditorFixture, "Значение вводится числом и подписью, ошибка не меняет параметр") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");
  const ModuleId oscillator = this->findModule("oscillator");
  Rml::Element* prompt = this->get("#prompt");
  Rml::Element* input = this->get("#prompt-input");

  CHECK(prompt->IsClassSet("hidden"));

  this->click(this->knob(filter, "cutoff"), RIGHT_BUTTON);

  CHECK_FALSE(prompt->IsClassSet("hidden"));
  CHECK(this->keyboardRequests == 1);
  CHECK(input->GetAttribute<Rml::String>("value", "") == "1000 Hz");

  input->SetAttribute("value", "440 Hz");
  this->click(this->get("[data-action=\"prompt-accept\"]"));

  CHECK(this->value(filter, "cutoff") == 440.0f);
  CHECK(prompt->IsClassSet("hidden"));

  this->click(this->knob(oscillator, "wave"), RIGHT_BUTTON);
  input->SetAttribute("value", "triangle");
  this->context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
  this->frame();

  CHECK(this->value(oscillator, "wave") == 4.0f);

  this->click(this->knob(filter, "cutoff"), RIGHT_BUTTON);
  input->SetAttribute("value", "много");
  this->click(this->get("[data-action=\"prompt-accept\"]"));

  CHECK(this->value(filter, "cutoff") == 440.0f);
  CHECK_FALSE(this->view->getMessage().empty());

  this->click(this->knob(filter, "cutoff"), RIGHT_BUTTON);
  input->SetAttribute("value", "5000");
  this->click(this->get("[data-action=\"prompt-cancel\"]"));

  CHECK(this->value(filter, "cutoff") == 440.0f);
}

TEST_CASE_FIXTURE(EditorFixture, "Автоматизация хоста двигает ручку без пересоздания элементов (ТЗ §8.2)") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");
  const clap_id slot = *this->instance->slots.findSlot(filter, "cutoff");
  auto* knob = dynamic_cast<Knob*>(this->knob(filter, "cutoff"));

  this->instance->slotInbox.post(slot, 0.25f);
  this->frame();

  CHECK(knob->getValue() == doctest::Approx(0.25f));
  CHECK(this->knob(filter, "cutoff") == knob);
}

TEST_CASE_FIXTURE(EditorFixture, "Новый проект перестраивает вид и выходит из исчезнувшего генератора") {
  this->openGenerator();

  const std::string saved = saveModel(this->model());
  const ModuleId generator = this->findModule("generator");

  this->view->openGraph(generator);
  this->frame();

  SynthModel empty(&this->instance->registry);

  REQUIRE(my_plug_load_preset(this->instance, saveModel(empty), "Empty").has_value());
  this->frame();

  CHECK(this->view->getGraph() == std::nullopt);
  CHECK(this->count(".block") == 1);
  CHECK(this->text(this->get("#preset-button")) == "Empty");

  REQUIRE(my_plug_load_preset(this->instance, saved, "").has_value());
  this->frame();

  CHECK(this->count(".block") == 2);
  CHECK(this->text(this->get("#preset-button")) == "Init");
}

TEST_CASE_FIXTURE(EditorFixture, "Модуль добавляется из меню, автоматически расставленные блоки не прыгают (D-068)") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const Rml::Vector2f before = this->block(oscillator)->GetAbsoluteOffset(Rml::BoxArea::Border);
  const size_t count = this->model().getModules().size();

  this->click(this->get("[data-action=\"add-module\"]"));
  this->clickMenu("LFO");

  REQUIRE(this->model().getModules().size() == count + 1);

  const ModuleId lfo = this->findModule("lfo");

  CHECK(this->model().findModule(lfo)->owner == this->findModule("generator"));
  CHECK(this->model().findModule(lfo)->position.has_value());
  CHECK(this->model().findModule(oscillator)->position.has_value());
  CHECK(this->block(oscillator)->GetAbsoluteOffset(Rml::BoxArea::Border).x == before.x);
  CHECK(this->block(oscillator)->GetAbsoluteOffset(Rml::BoxArea::Border).y == before.y);
  CHECK(this->get("#popup")->IsClassSet("hidden"));
}

TEST_CASE_FIXTURE(EditorFixture, "Меню модулей подчиняется правилам графа (D-004)") {
  this->click(this->get("[data-action=\"add-module\"]"));

  Rml::ElementList items;

  this->document->QuerySelectorAll(items, ".menu-item");

  const auto hasItem = [this](const std::string& label) {
    Rml::ElementList current;

    this->document->QuerySelectorAll(current, ".menu-item");

    return std::ranges::any_of(current, [&label](Rml::Element* item) {
      return item->GetInnerRML() == label;
    });
  };

  CHECK(hasItem("Generator"));
  CHECK(hasItem("Filter"));
  CHECK_FALSE(hasItem("Output"));
  CHECK_FALSE(hasItem("Macros"));

  this->context->ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
  this->openGenerator();
  this->click(this->get("[data-action=\"add-module\"]"));

  CHECK_FALSE(hasItem("Generator"));
  CHECK(hasItem("Filter"));
}

TEST_CASE_FIXTURE(EditorFixture, "Правый клик по холсту добавляет модуль в это место") {
  Rml::Element* canvas = this->get("#canvas");
  const Rml::Vector2f point = canvas->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(700.0f, 300.0f);

  this->moveMouse(point);
  this->press(RIGHT_BUTTON);
  this->release(RIGHT_BUTTON);
  this->clickMenu("Filter");

  const auto added = std::ranges::find_if(this->model().getModules(), [](const auto& entry) {
    return !entry.second.owner && entry.second.descriptor->type == "filter";
  });

  REQUIRE(added != this->model().getModules().end());
  REQUIRE(added->second.position.has_value());
  CHECK(added->second.position->x == doctest::Approx(700.0f));
  CHECK(added->second.position->y == doctest::Approx(300.0f));
}

TEST_CASE_FIXTURE(EditorFixture, "Клик мимо меню закрывает его") {
  this->click(this->get("[data-action=\"add-module\"]"));
  CHECK_FALSE(this->get("#popup")->IsClassSet("hidden"));

  this->moveMouse(this->center(this->get("#canvas")));
  this->press();
  this->release();

  CHECK(this->get("#popup")->IsClassSet("hidden"));
}

TEST_CASE_FIXTURE(EditorFixture, "Блок передвигается, положение в модели и в пресете (D-065)") {
  const ModuleId output = this->model().getOutputId();
  Rml::Element* title = this->block(output)->QuerySelector(".block-title");
  const Rml::Vector2f before = this->block(output)->GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f start = this->center(title);

  this->dragTo(start, start + Rml::Vector2f(100.0f, 50.0f));

  const Rml::Vector2f after = this->block(output)->GetAbsoluteOffset(Rml::BoxArea::Border);

  CHECK(after.x == doctest::Approx(before.x + 100.0f).epsilon(0.02));
  CHECK(after.y == doctest::Approx(before.y + 50.0f).epsilon(0.02));
  REQUIRE(this->model().findModule(output)->position.has_value());

  const std::string text = saveModel(this->model());

  CHECK(text.find("\"position\"") != std::string::npos);
  CHECK(this->dirtyMarks > 0);
}

TEST_CASE_FIXTURE(EditorFixture, "Сдвиг холста не меняет модель, блок после сдвига ставится верно") {
  const ModuleId output = this->model().getOutputId();
  Rml::Element* canvas = this->get("#canvas");
  const Rml::Vector2f free = canvas->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(900.0f, 400.0f);
  const int marks = this->dirtyMarks;
  const Rml::Vector2f before = this->block(output)->GetAbsoluteOffset(Rml::BoxArea::Border);

  this->dragTo(free, free + Rml::Vector2f(-40.0f, 30.0f));

  CHECK(this->dirtyMarks == marks);
  CHECK_FALSE(this->model().findModule(output)->position.has_value());
  CHECK(this->block(output)->GetAbsoluteOffset(Rml::BoxArea::Border).x == doctest::Approx(before.x - 40.0f).epsilon(0.02));

  const Rml::Vector2f start = this->center(this->block(output)->QuerySelector(".block-title"));

  this->dragTo(start, start + Rml::Vector2f(10.0f, 10.0f));

  const ModulePosition position = *this->model().findModule(output)->position;

  // В модели положение без сдвига холста
  CHECK(position.x == doctest::Approx(before.x + 10.0f - canvas->GetAbsoluteOffset(Rml::BoxArea::Border).x).epsilon(0.02));
}

TEST_CASE_FIXTURE(EditorFixture, "Модуль удаляется и отключается кнопками блока (ТЗ §3.3)") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");

  this->click(this->block(filter)->QuerySelector("[data-action=\"toggle\"]"));

  CHECK_FALSE(this->model().findModule(filter)->enabled);
  CHECK(this->block(filter)->IsClassSet("disabled"));

  this->click(this->block(filter)->QuerySelector("[data-action=\"toggle\"]"));

  CHECK(this->model().findModule(filter)->enabled);

  this->click(this->block(filter)->QuerySelector("[data-action=\"remove\"]"));

  CHECK(this->model().findModule(filter) == nullptr);
  CHECK(this->find(".block[data-module=\"" + std::to_string(filter) + "\"]") == nullptr);
}

TEST_CASE_FIXTURE(EditorFixture, "Удаление генератора убирает его подграф и маршруты из матрицы (ТЗ §3.3)") {
  CHECK(this->count(".route") == 1);

  this->click(this->block(this->findModule("generator"))->QuerySelector("[data-action=\"remove\"]"));

  CHECK(this->count(".route") == 0);
  CHECK(this->count(".matrix-empty") == 1);
  CHECK(this->model().getRoutes().empty());
}

TEST_CASE_FIXTURE(EditorFixture, "Аудиопровод тянется от выхода ко входу и наоборот (ТЗ §8.1)") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const ModuleId amp = this->findModule("amp");
  const Connection forward = { oscillator, "out", amp, "in" };

  this->drag(this->port(oscillator, "out", "output"), this->port(amp, "in", "input"));
  CHECK(hasConnection(this->model(), forward));

  this->apply(Disconnect{ forward });

  this->drag(this->port(amp, "in", "input"), this->port(oscillator, "out", "output"));
  CHECK(hasConnection(this->model(), forward));
}

TEST_CASE_FIXTURE(EditorFixture, "Провод можно отпустить на блок с одним подходящим портом") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const ModuleId amp = this->findModule("amp");

  this->drag(this->port(oscillator, "out", "output"), this->block(amp)->QuerySelector(".block-title"));

  CHECK(hasConnection(this->model(), { oscillator, "out", amp, "in" }));
}

TEST_CASE_FIXTURE(EditorFixture, "Недопустимые провода не создаются и не подсвечиваются (D-007)") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const ModuleId filter = this->findModule("filter");
  const ModuleId amp = this->findModule("amp");
  const size_t connections = this->model().getConnections().size();
  Rml::Element* target = this->port(filter, "in", "input");
  const Rml::Vector2f start = this->center(this->port(amp, "out", "output"));

  this->moveMouse(start);
  this->press();
  this->moveMouse(start + Rml::Vector2f(-5.0f, 0.0f));
  this->moveMouse(this->center(target));

  // Цикл amp -> filter -> amp запрещён, а вход осциллятора отсутствует
  CHECK_FALSE(target->IsClassSet("legal"));

  this->release();

  CHECK(this->model().getConnections().size() == connections);
  CHECK(this->view->getMessage().empty());

  // Подсветка не остаётся после отпускания
  this->drag(this->port(oscillator, "out", "output"), this->get("#canvas"));

  CHECK(this->count(".legal") == 0);
  CHECK(this->model().getConnections().size() == connections);
}

TEST_CASE_FIXTURE(EditorFixture, "Провод модуляции тянется на ручку, новый маршрут виден в матрице (D-064)") {
  this->openGenerator();

  const ModuleId env = this->findModule("env");
  const ModuleId filter = this->findModule("filter");
  const auto modulation = [this](ModuleId module, const std::string& port) {
    return this->port(module, port, "modulation");
  };

  this->drag(modulation(env, "out"), this->knob(filter, "cutoff"));

  const ModRoute* route = findRoute(this->model(), env, filter, "cutoff");

  REQUIRE(route != nullptr);
  CHECK(route->depth == 0.5f);
  CHECK(this->count(".route") == 2);
}

TEST_CASE_FIXTURE(EditorFixture, "Ступенчатая ручка без модуляции и повторный маршрут не принимают провод (D-025)") {
  this->openGenerator();

  const ModuleId env = this->findModule("env");
  const ModuleId amp = this->findModule("amp");
  const size_t routes = this->model().getRoutes().size();

  // Маршрут ENV -> gain уже есть в пресете
  this->drag(this->port(env, "out", "modulation"), this->knob(amp, "gain"));

  CHECK(this->model().getRoutes().size() == routes);

  // Цель не подсвечена, поэтому отпускание на неё просто ничего не делает, без сообщения об ошибке
  CHECK(this->view->getMessage().empty());
}

TEST_CASE_FIXTURE(EditorFixture, "Макрос в панели: маршрут между уровнями виден меткой и в матрице (D-063)") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");

  CHECK(this->cell(filter, "cutoff")->QuerySelector(".knob-mark") == nullptr);

  this->drag(this->port(MACROS_MODULE_ID, "macro1", "modulation"), this->knob(filter, "cutoff"));

  REQUIRE(findRoute(this->model(), MACROS_MODULE_ID, filter, "cutoff") != nullptr);
  CHECK(this->cell(filter, "cutoff")->QuerySelector(".knob-mark") != nullptr);
  CHECK(this->count(".route") == 2);

  // Макрос сам себя не модулирует
  this->drag(this->port(MACROS_MODULE_ID, "macro1", "modulation"), this->knob(MACROS_MODULE_ID, "macro2"));

  CHECK(this->model().getRoutes().size() == 2);
}

TEST_CASE_FIXTURE(EditorFixture, "Velocity доступна внутри генератора (D-052)") {
  this->openGenerator();

  const ModuleId generator = this->findModule("generator");
  const ModuleId filter = this->findModule("filter");

  this->drag(this->port(generator, "velocity", "modulation"), this->knob(filter, "cutoff"));

  CHECK(findRoute(this->model(), generator, filter, "cutoff") != nullptr);
}

TEST_CASE_FIXTURE(EditorFixture, "Клик по проводу выделяет его, кнопка удаляет") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const ModuleId filter = this->findModule("filter");
  const Connection connection = { oscillator, "out", filter, "in" };
  Rml::Element* remove = this->get("#wire-remove");
  Rml::Element* canvas = this->get("#canvas");

  CHECK(remove->IsClassSet("hidden"));

  // Провод идёт между портами почти горизонтально, поэтому его середину видно посреди них
  const Rml::Vector2f from = this->center(this->port(oscillator, "out", "output"));
  const Rml::Vector2f to = this->port(filter, "in", "input")->GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f middle = (from + to + Rml::Vector2f(0.0f, 7.0f)) * 0.5f;

  REQUIRE(this->context->GetElementAtPoint(middle) == canvas);

  this->moveMouse(middle);
  this->press();
  this->release();

  CHECK_FALSE(remove->IsClassSet("hidden"));

  this->click(remove);

  CHECK_FALSE(hasConnection(this->model(), connection));
  CHECK(remove->IsClassSet("hidden"));
}

TEST_CASE_FIXTURE(EditorFixture, "Клавиша Delete удаляет выделенный провод, клик мимо снимает выделение") {
  this->openGenerator();

  const ModuleId oscillator = this->findModule("oscillator");
  const ModuleId filter = this->findModule("filter");
  const Connection connection = { oscillator, "out", filter, "in" };
  const Rml::Vector2f from = this->center(this->port(oscillator, "out", "output"));
  const Rml::Vector2f to = this->port(filter, "in", "input")->GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f middle = (from + to + Rml::Vector2f(0.0f, 7.0f)) * 0.5f;

  this->moveMouse(middle);
  this->press();
  this->release();
  this->moveMouse(this->center(this->get("#canvas")));
  this->press();
  this->release();

  CHECK(this->get("#wire-remove")->IsClassSet("hidden"));

  this->context->ProcessKeyDown(Rml::Input::KI_DELETE, 0);
  this->frame();
  CHECK(hasConnection(this->model(), connection));

  this->moveMouse(middle);
  this->press();
  this->release();
  this->context->ProcessKeyDown(Rml::Input::KI_DELETE, 0);
  this->frame();

  CHECK_FALSE(hasConnection(this->model(), connection));
}

TEST_CASE_FIXTURE(EditorFixture, "Глубина меняется ползунком матрицы, маршрут удаляется кнопкой (ТЗ §6.3)") {
  const ModRoute route = this->model().getRoutes().front();
  Rml::Element* slider = this->get(".route-depth");
  const float width = slider->GetBox().GetSize(Rml::BoxArea::Border).x;
  const Rml::Vector2f left = slider->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(0.0f, 5.0f);

  this->moveMouse(left + Rml::Vector2f(width * 0.5f, 0.0f));
  this->press();
  this->moveMouse(left + Rml::Vector2f(width * 0.25f, 0.0f));
  this->release();

  CHECK(this->model().getRoutes().front().depth == doctest::Approx(-0.5f).epsilon(0.03));
  CHECK(this->text(this->get(".route-value")) == "-0.50");

  this->click(this->get("[data-action=\"remove-route\"]"));

  CHECK(this->model().getRoutes().empty());
  CHECK(findRoute(this->model(), route.source.module, route.target.module, route.target.parameter) == nullptr);
}

TEST_CASE_FIXTURE(EditorFixture, "Маршрут между уровнями добавляется из матрицы (ТЗ §6.2)") {
  this->click(this->get("[data-action=\"add-route\"]"));
  this->clickMenu("Macro 3");
  this->frame();
  this->clickMenu("G1 Filter 1 Cutoff");

  const ModuleId filter = this->findModule("filter");

  CHECK(findRoute(this->model(), MACROS_MODULE_ID, filter, "cutoff") != nullptr);
  CHECK(this->count(".route") == 2);
}

TEST_CASE_FIXTURE(EditorFixture, "Меню целей маршрута показывает только допустимое (D-004)") {
  const auto listItems = [this] {
    Rml::ElementList items;
    std::vector<std::string> labels;

    this->document->QuerySelectorAll(items, ".menu-item");

    for (Rml::Element* item : items) {
      labels.push_back(item->GetInnerRML());
    }

    return labels;
  };

  this->click(this->get("[data-action=\"add-route\"]"));
  this->clickMenu("G1 ENV 1");
  this->frame();

  const std::vector<std::string> labels = listItems();

  CHECK(std::ranges::contains(labels, "G1 Filter 1 Cutoff"));

  // Этот маршрут уже есть, а параметр генератора не модулируется
  CHECK_FALSE(std::ranges::contains(labels, "G1 Amp 1 Gain"));
  CHECK_FALSE(std::ranges::contains(labels, "G1 Polyphony"));
}

TEST_CASE_FIXTURE(EditorFixture, "Пресеты: сохранение как новый, обновление только осознанно, загрузка из списка (ТЗ §9)") {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sint-gui-presets";

  std::filesystem::remove_all(directory);
  this->instance->presetLibrary.emplace(directory);

  const ModuleId filter = this->findModule("filter");

  this->apply(SetParameter{ filter, "cutoff", 700.0f });

  // У пресета по умолчанию нет файла, поэтому Save спрашивает имя
  this->click(this->get("[data-action=\"save\"]"));
  CHECK_FALSE(this->get("#prompt")->IsClassSet("hidden"));

  this->get("#prompt-input")->SetAttribute("value", "Бас");
  this->click(this->get("[data-action=\"prompt-accept\"]"));

  CHECK(this->text(this->get("#preset-button")) == "Бас");

  this->apply(SetParameter{ filter, "cutoff", 800.0f });

  // Новый файл с занятым именем не заменяет старый
  this->click(this->get("[data-action=\"save-as\"]"));
  this->click(this->get("[data-action=\"prompt-accept\"]"));

  CHECK_FALSE(this->view->getMessage().empty());
  CHECK(loadModel(&this->instance->registry, *this->instance->presetLibrary->read("Бас"))->findModule(filter)->parameters.at("cutoff") == 700.0f);

  this->click(this->get("[data-action=\"save\"]"));

  CHECK(loadModel(&this->instance->registry, *this->instance->presetLibrary->read("Бас"))->findModule(filter)->parameters.at("cutoff") == 800.0f);

  this->click(this->get("#preset-button"));
  this->clickMenu("Init");
  this->frame();

  CHECK(this->value(filter, "cutoff") == 1000.0f);
  CHECK(this->text(this->get("#preset-button")) == "Init");

  this->click(this->get("#preset-button"));
  this->clickMenu("Бас");
  this->frame();

  CHECK(this->value(filter, "cutoff") == 800.0f);

  std::filesystem::remove_all(directory);
}

TEST_CASE_FIXTURE(EditorFixture, "Текст из файла не становится разметкой") {
  REQUIRE(my_plug_load_preset(this->instance, saveModel(this->model()), "<b>x</b>&").has_value());
  this->frame();

  CHECK(this->get("#preset-button")->GetNumChildren() == 1);
  CHECK(this->get("#preset-button")->QuerySelector("b") == nullptr);
}

TEST_CASE_FIXTURE(EditorFixture, "Тип неизвестного модуля из файла не становится разметкой") {
  const std::string text = R"({
    "version": 1, "nextModuleId": 5,
    "modules": [
      { "id": 1, "type": "output", "typeVersion": 1 },
      { "id": 2, "type": "<b>x</b>", "typeVersion": 7 }
    ],
    "connections": [],
    "routes": []
  })";

  REQUIRE(my_plug_load_preset(this->instance, text, "Stub").has_value());
  this->frame();

  CHECK(this->block(2)->QuerySelector(".block-title b") == nullptr);
}

TEST_CASE_FIXTURE(EditorFixture, "Неизвестный модуль виден, двигается и не даёт менять свои данные (D-021, D-065)") {
  const std::string text = R"({
    "version": 1, "nextModuleId": 5,
    "modules": [
      { "id": 1, "type": "output", "typeVersion": 1 },
      { "id": 2, "type": "future.chorus", "typeVersion": 7, "parameters": { "rate": 0.5 } }
    ],
    "connections": [ { "fromModule": 2, "fromPort": "out", "toModule": 1, "toPort": "in" } ],
    "routes": []
  })";

  REQUIRE(my_plug_load_preset(this->instance, text, "Stub").has_value());
  this->frame();

  Rml::Element* stub = this->block(2);

  CHECK(stub->IsClassSet("stub"));
  CHECK(stub->QuerySelector("knob") == nullptr);
  CHECK(stub->QuerySelector("[data-action=\"toggle\"]") == nullptr);

  const Rml::Vector2f start = this->center(stub->QuerySelector(".block-title"));

  this->dragTo(start, start + Rml::Vector2f(30.0f, 30.0f));

  CHECK(this->model().findModule(2)->position.has_value());
  CHECK(saveModel(this->model()).find("future.chorus") != std::string::npos);
}

TEST_CASE_FIXTURE(EditorFixture, "Ошибка загрузки пресета видна и не меняет модель") {
  const std::filesystem::path directory = std::filesystem::temp_directory_path() / "sint-gui-bad-preset";

  std::filesystem::remove_all(directory);
  this->instance->presetLibrary.emplace(directory);

  REQUIRE(this->instance->presetLibrary->write("Bad", "не JSON", PRESET_WRITE::CREATE).has_value());

  const SynthModel before = this->model();

  this->click(this->get("#preset-button"));
  this->clickMenu("Bad");
  this->frame();

  CHECK(this->model() == before);
  CHECK_FALSE(this->view->getMessage().empty());
  CHECK(this->text(this->get("#preset-button")) == "Init");

  std::filesystem::remove_all(directory);
}

TEST_CASE_FIXTURE(EditorFixture, "Параметр без слота работает, но помечен: хост его не видит (D-011)") {
  this->openGenerator();

  const ModuleId generator = this->findModule("generator");

  // 7 параметров на ENV: столько модулей заведомо не помещаются в пул из 1024 слотов
  for (int index = 0; index < 150; index++) {
    REQUIRE(my_plug_apply_command(this->instance, AddModule{ "env", generator }).has_value());
  }

  this->frame();

  CHECK(this->count(".knob-mark.unexported") > 0);
}

TEST_CASE_FIXTURE(EditorFixture, "Числовая локаль хоста не ломает расположение и подписи (D-025)") {
  // Хост мог поменять локаль на весь процесс. В этой среде её может не быть, тогда проверка бессмысленна
  const std::string previous = std::setlocale(LC_NUMERIC, nullptr);

  if (!std::setlocale(LC_NUMERIC, "ru_RU.UTF-8") && !std::setlocale(LC_NUMERIC, "de_DE.UTF-8")) {
    MESSAGE("Нет локали с десятичной запятой, проверка пропущена");
    return;
  }

  const ModuleId output = this->model().getOutputId();

  this->apply(SetModulePosition{ output, { 300.5f, 120.25f } });

  const Rml::Vector2f offset = this->block(output)->GetAbsoluteOffset(Rml::BoxArea::Border) - this->get("#canvas")->GetAbsoluteOffset(Rml::BoxArea::Border);

  CHECK(offset.x == doctest::Approx(300.5f).epsilon(0.01));
  CHECK(offset.y == doctest::Approx(120.25f).epsilon(0.01));
  CHECK(this->text(this->get(".route-value")) == "+1.00");

  std::setlocale(LC_NUMERIC, previous.c_str());
}

TEST_CASE("Закрытие и открытие редактора не накапливает память (без OpenGL)") {
  const auto readMemory = [] {
    FILE* file = std::fopen("/proc/self/statm", "r");
    long size = 0;
    long resident = 0;

    REQUIRE(file != nullptr);
    REQUIRE(std::fscanf(file, "%ld %ld", &size, &resident) == 2);
    std::fclose(file);

    return resident * 4;
  };

  // Первые окна заполняют кэши шрифтов и аллокатора, рост считается после них
  for (int index = 0; index < 20; index++) {
    EditorFixture fixture;
  }

  const long before = readMemory();

  for (int index = 0; index < 100; index++) {
    EditorFixture fixture;
  }

  const long growth = readMemory() - before;

  MESSAGE("Рост за 100 открытий: " << growth << " КБ");
  CHECK(growth < 500);
}
