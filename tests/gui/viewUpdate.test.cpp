#include <doctest/doctest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../gui/knob/knob.h"
#include "../../gui/wireLayer/wireLayer.h"
#include "./editorFixture.h"

namespace {
  constexpr size_t MAX_MODULES = 40; // Схема должна расти, но не настолько, чтобы тест стал медленным
  constexpr int STEPS_PER_SEED = 150;

  void collectElements(const Rml::Element* element, std::set<const Rml::Element*>& elements) {
    elements.insert(element);

    for (int index = 0; index < element->GetNumChildren(); index++) {
      collectElements(element->GetChild(index), elements);
    }
  }

  /**
   * Всё, что у элемента задано: класс, атрибуты, стили из кода, текст и значения ручки.
   * Порядок классов и стилей не важен и в запись не входит
   */
  std::string describeElement(Rml::Element* element) {
    std::ostringstream text;
    std::vector<std::string> classes;
    std::map<std::string, std::string> attributes;
    std::map<std::string, std::string> properties;

    Rml::StringUtilities::ExpandString(classes, element->GetClassNames(), ' ');
    std::ranges::sort(classes);

    for (const auto& [name, value] : element->GetAttributes()) {
      if (name != "class") {
        attributes[name] = value.Get<Rml::String>();
      }
    }

    for (const auto& [id, property] : element->GetLocalStyleProperties()) {
      properties[Rml::StyleSheetSpecification::GetPropertyName(id)] = property.ToString();
    }

    text << element->GetTagName();

    for (const std::string& name : classes) {
      text << "." << name;
    }

    for (const auto& [name, value] : attributes) {
      text << " " << name << "=" << value;
    }

    for (const auto& [name, value] : properties) {
      text << " " << name << ":" << value;
    }

    if (const auto* node = dynamic_cast<const Rml::ElementText*>(element)) {
      text << " \"" << node->GetText() << "\"";
    }

    if (const auto* knob = dynamic_cast<const Knob*>(element)) {
      text << " knob " << knob->getValue() << " " << knob->getOrigin() << " " << knob->getModulationLow() << " " << knob->getModulationHigh();
    }

    return text.str();
  }

  void dumpTree(Rml::Element* element, int depth, std::string& text) {
    text += std::string((size_t)depth * 2, ' ') + describeElement(element) + "\n";

    for (int index = 0; index < element->GetNumChildren(); index++) {
      dumpTree(element->GetChild(index), depth + 1, text);
    }
  }

  /**
   * Дерево и списки сцены. Указатели в списках проверяются до чтения: провод или ручка, оставшиеся
   * от удалённого элемента, смотрели бы в освобождённую память
   */
  std::string dumpEditor(Rml::ElementDocument* document, const EditorView& view) {
    const EditorScene& scene = view.getScene();
    std::set<const Rml::Element*> live;
    std::string text;

    const auto describeLive = [&live](Rml::Element* element) {
      REQUIRE(live.contains(element));

      return describeElement(element);
    };

    collectElements(document, live);

    // Сам body не сравнивается: второй документ не показан на экране, поэтому видимость у него другая
    for (int index = 0; index < document->GetNumChildren(); index++) {
      dumpTree(document->GetChild(index), 0, text);
    }

    for (const Wire& wire : dynamic_cast<WireLayer*>(document->GetElementById("wires"))->getWires()) {
      text += "wire " + describeLive(wire.from) + " -> " + describeLive(wire.to) + " " + std::to_string((int)wire.kind) + (wire.isSelected ? " selected\n" : "\n");
    }

    for (const KnobView& knob : scene.getKnobs()) {
      text += "knob " + std::to_string(knob.module) + " " + knob.parameter->key + " " + knob.shownText + " ";
      text += describeLive(knob.knob) + describeLive(knob.cell) + describeLive(knob.valueText) + "\n";
    }

    for (const RouteView& route : scene.getRoutes()) {
      text += "route " + std::to_string(route.key.first.module) + route.key.first.output + ">";
      text += std::to_string(route.key.second.module) + route.key.second.parameter + " ";
      text += describeLive(route.row) + describeLive(route.fill) + describeLive(route.value) + "\n";
    }

    for (const auto& [id, block] : scene.getBlocks()) {
      text += "block " + std::to_string(id) + " " + describeLive(block) + "\n";
    }

    for (Rml::Element* port : scene.getPorts()) {
      text += "port " + describeLive(port) + "\n";
    }

    text += "wire refs " + std::to_string(scene.getWireRefs().size()) + "\n";

    return text;
  }

  /**
   * Первая расходящаяся строка: всё дерево в сообщении теста прочитать невозможно
   */
  std::string findDifference(const std::string& actual, const std::string& expected) {
    std::istringstream actualLines(actual);
    std::istringstream expectedLines(expected);
    std::string actualLine;
    std::string expectedLine;
    int number = 1;

    while (true) {
      const bool hasActual = (bool)std::getline(actualLines, actualLine);
      const bool hasExpected = (bool)std::getline(expectedLines, expectedLine);

      if (!hasActual && !hasExpected) {
        return "";
      }

      if (!hasActual || !hasExpected || actualLine != expectedLine) {
        return "Строка " + std::to_string(number) + "\nсейчас: " + (hasActual ? actualLine : "<нет>") + "\nзаново: " + (hasExpected ? expectedLine : "<нет>");
      }

      number++;
    }
  }

  struct ViewUpdateFixture : EditorFixture {
    std::mt19937 random;

    explicit ViewUpdateFixture(uint32_t seed = 1) : random(seed) {}

    size_t pick(size_t count) {
      return this->random() % count;
    }

    float pickUnit() {
      return (float)this->pick(1001) / 1000.0f;
    }

    template <typename T>
    const T& pickFrom(const std::vector<T>& list) {
      return list[this->pick(list.size())];
    }

    std::vector<ModuleId> listModules() const {
      return this->filterModules([](const ModuleInstance&) {
        return true;
      });
    }

    std::vector<ModuleId> listGenerators() const {
      return this->filterModules([](const ModuleInstance& module) {
        return module.descriptor->role == MODULE_ROLE::GENERATOR;
      });
    }

    /**
     * Модули, подходящие для правки: случайная пара почти никогда не совпала бы по портам
     */
    template <typename Predicate>
    std::vector<ModuleId> filterModules(Predicate predicate) const {
      std::vector<ModuleId> ids;

      for (const auto& [id, module] : this->model().getModules()) {
        if (predicate(module)) {
          ids.push_back(id);
        }
      }

      return ids;
    }

    /**
     * Соединение внутри одного уровня, чаще всего допустимое. Если его нет, пусто
     */
    std::optional<Command> makeConnect() {
      const SynthModel& model = this->model();
      const auto hasOutputs = [](const ModuleInstance& module) {
        return !module.descriptor->outputs.empty();
      };
      const std::vector<ModuleId> sources = this->filterModules(hasOutputs);

      if (sources.empty()) {
        return std::nullopt;
      }

      const ModuleInstance& source = *model.findModule(this->pickFrom(sources));
      const std::vector<ModuleId> targets = this->filterModules([&source](const ModuleInstance& module) {
        return module.owner == source.owner && module.id != source.id && !module.descriptor->inputs.empty();
      });

      if (targets.empty()) {
        return std::nullopt;
      }

      const ModuleInstance& target = *model.findModule(this->pickFrom(targets));
      const auto& outputs = source.descriptor->outputs;
      const auto& inputs = target.descriptor->inputs;

      return Connect{ { source.id, outputs[this->pick(outputs.size())].key, target.id, inputs[this->pick(inputs.size())].key } };
    }

    std::optional<Command> makeAddRoute() {
      const SynthModel& model = this->model();
      const std::vector<ModuleId> sources = this->filterModules([](const ModuleInstance& module) {
        return !module.descriptor->modOutputs.empty();
      });
      const std::vector<ModuleId> targets = this->filterModules([](const ModuleInstance& module) {
        return !module.descriptor->parameters.empty();
      });

      if (sources.empty() || targets.empty()) {
        return std::nullopt;
      }

      const ModuleInstance& source = *model.findModule(this->pickFrom(sources));
      const ModuleInstance& target = *model.findModule(this->pickFrom(targets));
      const auto& outputs = source.descriptor->modOutputs;
      const auto& parameters = target.descriptor->parameters;

      return AddRoute{ {
        { source.id, outputs[this->pick(outputs.size())].key },
        { target.id, parameters[this->pick(parameters.size())].key },
        this->pickUnit() * 2.0f - 1.0f,
      } };
    }

    Command makeAddModule() {
      std::vector<std::string> types;

      for (const ModuleDescriptor* descriptor : this->backend->getRegistry().getAll()) {
        if (descriptor->role == MODULE_ROLE::REGULAR || descriptor->role == MODULE_ROLE::GENERATOR) {
          types.push_back(descriptor->type);
        }
      }

      const std::vector<ModuleId> generators = this->listGenerators();
      std::optional<ModuleId> owner;

      if (!generators.empty() && this->pick(2) == 0) {
        owner = this->pickFrom(generators);
      }

      return AddModule{ this->pickFrom(types), owner };
    }

    /**
     * Случайная правка. Часть из них модель отклонит, это тоже проверка: вид не должен измениться.
     * Если для правки нечего выбрать, переключается блок
     */
    Command makeCommand() {
      const SynthModel& model = this->model();
      const ModuleInstance& module = *model.findModule(this->pickFrom(this->listModules()));
      const ModuleDescriptor& descriptor = *module.descriptor;

      switch (this->pick(12)) {
        case 0:
        case 1:
        case 2:
          if (this->listModules().size() < MAX_MODULES) {
            return this->makeAddModule();
          }

          break;

        case 3:
          return RemoveModule{ module.id };

        case 4:
          if (const auto command = this->makeConnect()) {
            return *command;
          }

          break;

        case 5:
          if (!model.getConnections().empty()) {
            return Disconnect{ this->pickFrom(model.getConnections()) };
          }

          break;

        case 6:
          if (const auto command = this->makeAddRoute()) {
            return *command;
          }

          break;

        case 7:
          if (!model.getRoutes().empty()) {
            const ModRoute& route = this->pickFrom(model.getRoutes());

            return RemoveRoute{ route.source, route.target };
          }

          break;

        case 8:
          if (!model.getRoutes().empty()) {
            const ModRoute& route = this->pickFrom(model.getRoutes());

            return SetRouteDepth{ route.source, route.target, this->pickUnit() * 2.0f - 1.0f };
          }

          break;

        case 9:
          if (!descriptor.parameters.empty()) {
            const ParameterDescriptor& parameter = descriptor.parameters[this->pick(descriptor.parameters.size())];

            return SetParameter{ module.id, parameter.key, parameter.minValue + (parameter.maxValue - parameter.minValue) * this->pickUnit() };
          }

          break;

        case 10:
          return SetModulePosition{ module.id, { this->pickUnit() * 900.0f, this->pickUnit() * 500.0f } };

        default:
          break;
      }

      return SetModuleEnabled{ module.id, !module.enabled };
    }

    void stepRandom() {
      const size_t kind = this->pick(60);

      if (kind == 0) {
        REQUIRE(this->backend->loadDefaultPreset().has_value());
      } else if (kind < 7) {
        std::vector<ModuleId> generators = this->listGenerators();

        // Пустой вариант — главный граф
        generators.push_back(0);

        const ModuleId graph = this->pickFrom(generators);

        this->view->openGraph(graph != 0 ? std::optional(graph) : std::nullopt);
      } else {
        (void)my_plug_apply_command(this->instance, this->makeCommand());
      }

      this->frame();
    }

    /**
     * Строит рядом второй редактор по той же модели: он видит её впервые, поэтому строит всё целиком
     */
    void checkMatchesFullBuild() {
      Rml::ElementDocument* other = this->context->LoadDocumentFromMemory(EDITOR_DOCUMENT);

      REQUIRE(other != nullptr);

      {
        EditorView fresh(other, *this->backend, [] {});

        fresh.openGraph(this->view->getGraph());
        fresh.update();

        const std::string difference = findDifference(dumpEditor(this->document, *this->view), dumpEditor(other, fresh));

        CHECK_MESSAGE(difference.empty(), difference);
      }

      other->Close();
      this->context->Update();
    }
  };
}

TEST_CASE("Вид после цепочки правок совпадает с построенным заново (D-079)") {
  for (uint32_t seed = 1; seed <= 6; seed++) {
    CAPTURE(seed);

    ViewUpdateFixture fixture(seed);

    for (int step = 0; step < STEPS_PER_SEED; step++) {
      CAPTURE(step);

      fixture.stepRandom();
      fixture.checkMatchesFullBuild();
    }
  }
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Вид совпадает с построенным заново, когда слотов не хватило и когда они освободились (D-011)") {
  const auto findUnexported = [this]() -> std::optional<ModuleId> {
    for (const auto& [id, module] : this->model().getModules()) {
      for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
        if (!this->backend->isExported(id, parameter.key)) {
          return id;
        }
      }
    }

    return std::nullopt;
  };

  ModuleId last = 0;

  while (!findUnexported()) {
    const CommandResult result = my_plug_apply_command(this->instance, AddModule{ "lfo", std::nullopt });

    REQUIRE(result.has_value());
    last = **result;
  }

  this->frame();
  CHECK(this->count(".knob-mark.unexported") > 0);
  this->checkMatchesFullBuild();

  REQUIRE(my_plug_apply_command(this->instance, RemoveModule{ this->findModule("lfo") }).has_value());
  this->frame();
  this->checkMatchesFullBuild();

  REQUIRE(my_plug_apply_command(this->instance, RemoveModule{ last }).has_value());
  this->frame();
  this->checkMatchesFullBuild();
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Вид совпадает с построенным заново после блока, отпущенного мышью") {
  const ModuleId amp = this->findModule("amp");

  this->openGenerator();

  const Rml::Vector2f start = this->center(this->block(amp)->QuerySelector(".block-title"));

  this->dragTo(start, start + Rml::Vector2f(40.0f, 30.0f));
  this->checkMatchesFullBuild();
}
