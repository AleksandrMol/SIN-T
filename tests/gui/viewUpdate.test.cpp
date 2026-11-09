#include <doctest/doctest.h>

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <ranges>
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

    WireLayer* wires() const {
      return dynamic_cast<WireLayer*>(this->document->GetElementById("wires"));
    }

    WireLayer* draft() const {
      return dynamic_cast<WireLayer*>(this->document->GetElementById("draft"));
    }

    void render() {
      this->context->Update();
      this->context->Render();
    }

    /**
     * Точка в пикселях окна: от масштаба и раскладки она не зависит
     */
    Rml::Element* addPoint(int x, int y) {
      Rml::Element* point = this->document->AppendChild(this->document->CreateElement("div"));

      point->SetAttribute("style", "position: absolute; left: " + std::to_string(x) + "px; top: " + std::to_string(y) + "px; width: 0px; height: 0px;");

      return point;
    }

    /**
     * Нарисованная сетка совпадает с посчитанной с нуля: куски не устарели
     */
    void checkWireMesh() {
      this->render();

      CHECK(this->wires()->getMesh() == this->wires()->buildMesh());
      CHECK(this->draft()->getMesh() == this->draft()->buildMesh());
    }

    void scrollCanvas(int direction) {
      const Rml::Vector2f corner = this->get("#canvas")->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(5.0f, 5.0f);

      this->context->ProcessMouseMove((int)corner.x, (int)corner.y, 0);
      this->context->ProcessMouseWheel(Rml::Vector2f(0.0f, (float)direction), 0);
      this->frame();
    }

    /**
     * Сколько проводов пришлось посчитать и сколько раз собрать общую сетку за время правки
     */
    template <typename Action>
    std::pair<uint64_t, uint64_t> countWork(Action action) {
      this->render();

      const uint64_t shaped = this->wires()->getShapedWireCount();
      const uint64_t assembled = this->wires()->getAssembledMeshCount();

      action();
      this->render();

      return { this->wires()->getShapedWireCount() - shaped, this->wires()->getAssembledMeshCount() - assembled };
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

TEST_CASE("Нарисованная сетка проводов совпадает с посчитанной с нуля (D-090)") {
  for (uint32_t seed = 1; seed <= 4; seed++) {
    CAPTURE(seed);

    ViewUpdateFixture fixture(seed);

    for (int step = 0; step < STEPS_PER_SEED; step++) {
      CAPTURE(step);

      const auto& wires = fixture.wires()->getWires();
      const size_t kind = fixture.pick(10);

      if (kind == 0) {
        fixture.scrollCanvas(fixture.pick(2) == 0 ? -1 : 1);
      } else if (kind == 1 && !wires.empty()) {
        // Клик по середине провода выделяет его, если сверху нет блока
        fixture.render();

        const Rml::Vector2f middle = fixture.wires()->findMiddle(fixture.pick(wires.size()));

        fixture.moveMouse(middle);
        fixture.press();
        fixture.release();
      } else {
        fixture.stepRandom();
      }

      fixture.checkWireMesh();
    }
  }
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Правка значения не трогает сетку проводов (D-090)") {
  this->openGenerator();

  const ModuleId filter = this->findModule("filter");
  const auto work = this->countWork([this, filter] {
    this->apply(SetParameter{ filter, "cutoff", 1234.0f });
  });

  CHECK(this->wires()->getWires().size() > 1);
  CHECK(work.first == 0);
  CHECK(work.second == 0);
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Сдвиг холста только собирает готовые куски проводов заново (D-090)") {
  this->openGenerator();

  const auto work = this->countWork([this] {
    this->scrollCanvas(1);
  });

  CHECK(work.first == 0);
  CHECK(work.second == 1);
  this->checkWireMesh();
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Перенос блока считает только провода с одним концом в нём (D-090)") {
  this->openGenerator();
  this->render();

  const ModuleId amp = this->findModule("amp");
  Rml::Element* block = this->block(amp);
  const auto& list = this->wires()->getWires();
  const auto touched = std::ranges::count_if(list, [block](const Wire& wire) {
    return block->Contains(wire.from) != block->Contains(wire.to);
  });

  REQUIRE(touched > 0);
  REQUIRE((size_t)touched < list.size());

  const auto work = this->countWork([this, amp] {
    this->apply(SetModulePosition{ amp, { 333.0f, 222.0f } });
  });

  CHECK(work.first == (uint64_t)touched);
  CHECK(work.second == 1);
  this->checkWireMesh();
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Выделение пересчитывает один провод, смена масштаба все (D-090)") {
  this->openGenerator();
  this->render();

  const Rml::Vector2f middle = this->wires()->findMiddle(0);
  const auto selected = this->countWork([this, middle] {
    this->moveMouse(middle);
    this->press();
    this->release();
  });

  CHECK(std::ranges::count_if(this->wires()->getWires(), &Wire::isSelected) == 1);
  CHECK(selected.first == 1);
  this->checkWireMesh();

  const auto scaled = this->countWork([this] {
    this->context->SetDensityIndependentPixelRatio(1.5f);
    this->frame();
  });

  CHECK(scaled.first == this->wires()->getWires().size());
  this->checkWireMesh();
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Удалённый провод исчезает из сетки, а без проводов она пуста (D-090)") {
  this->openGenerator();

  const std::vector<Connection> connections = this->model().getConnections();
  const std::vector<ModRoute> routes = this->model().getRoutes();

  for (const Connection& connection : connections) {
    this->apply(Disconnect{ connection });
  }

  for (const ModRoute& route : routes) {
    this->apply(RemoveRoute{ route.source, route.target });
  }

  this->render();

  CHECK(this->wires()->getWires().empty());
  CHECK(this->wires()->getMesh().vertices.empty());
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Провод под мышью рисуется и исчезает после отпускания (D-090)") {
  this->openGenerator();

  const Rml::Vector2f start = this->center(this->get(".block .port.modulation.output"));

  this->moveMouse(start);
  this->press();
  this->moveMouse(start + Rml::Vector2f(30.0f, 30.0f));
  this->checkWireMesh();

  CHECK_FALSE(this->draft()->getMesh().vertices.empty());

  this->moveMouse(start + Rml::Vector2f(60.0f, 10.0f));
  this->checkWireMesh();
  this->release();
  this->checkWireMesh();

  CHECK(this->draft()->getMesh().vertices.empty());
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Сетка лежит там, где концы проводов, индексы указывают на свои вершины (D-090)") {
  Rml::Element* firstStart = this->addPoint(100, 100);
  Rml::Element* firstEnd = this->addPoint(300, 200);
  Rml::Element* secondStart = this->addPoint(600, 400);
  Rml::Element* secondEnd = this->addPoint(900, 500);

  this->wires()->setWires({ { firstStart, firstEnd, WIRE_KIND::AUDIO }, { secondStart, secondEnd, WIRE_KIND::AUDIO } });
  this->render();

  const Rml::Mesh& mesh = this->wires()->getMesh();

  REQUIRE_FALSE(mesh.vertices.empty());

  const auto [left, right] = std::ranges::minmax(mesh.vertices | std::views::transform([](const Rml::Vertex& vertex) {
    return vertex.position.x;
  }));
  const auto [top, bottom] = std::ranges::minmax(mesh.vertices | std::views::transform([](const Rml::Vertex& vertex) {
    return vertex.position.y;
  }));

  CHECK(left == doctest::Approx(100.0f).epsilon(0.02));
  CHECK(right == doctest::Approx(900.0f).epsilon(0.02));
  CHECK(top == doctest::Approx(100.0f).epsilon(0.05));
  CHECK(bottom == doctest::Approx(500.0f).epsilon(0.02));

  // Второй провод ссылается на свои вершины, а не на вершины первого
  CHECK(*std::ranges::max_element(mesh.indices) == (int)mesh.vertices.size() - 1);
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Провод с теми же концами перерисовывается при смене типа (D-090)") {
  Rml::Element* start = this->addPoint(100, 100);
  Rml::Element* end = this->addPoint(300, 200);

  this->wires()->setWires({ { start, end, WIRE_KIND::AUDIO } });
  this->checkWireMesh();

  this->wires()->setWires({ { start, end, WIRE_KIND::MODULATION } });
  this->checkWireMesh();
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Провод с теми же концами перерисовывается при смене масштаба (D-090)") {
  Rml::Element* start = this->addPoint(100, 100);
  Rml::Element* end = this->addPoint(300, 200);

  this->wires()->setWires({ { start, end, WIRE_KIND::MODULATION } });
  this->checkWireMesh();

  this->context->SetDensityIndependentPixelRatio(2.0f);
  this->checkWireMesh();
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Новый провод с нулевыми концами не считается уже посчитанным (D-090)") {
  Rml::Element* point = this->addPoint(0, 0);

  this->wires()->setWires({});
  this->render();
  this->wires()->setWires({ { point, point, WIRE_KIND::AUDIO } });
  this->checkWireMesh();

  CHECK_FALSE(this->wires()->getMesh().vertices.empty());
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Под проводом тёмная подложка шире него, сначала подложка, потом провод") {
  Rml::Element* start = this->addPoint(100, 300);
  Rml::Element* end = this->addPoint(900, 300);

  for (const WIRE_KIND kind : { WIRE_KIND::AUDIO, WIRE_KIND::MODULATION }) {
    CAPTURE((int)kind);
    this->wires()->setWires({ { start, end, kind } });
    this->render();

    const Rml::Mesh& mesh = this->wires()->getMesh();
    float darkReach = 0.0f;
    float colorReach = 0.0f;
    size_t firstDark = mesh.vertices.size();
    size_t firstColor = mesh.vertices.size();

    // Только середина провода: у стрелки своя форма
    for (size_t index = 0; index < mesh.vertices.size(); index++) {
      const Rml::Vertex& vertex = mesh.vertices[index];

      if (vertex.colour.alpha == 0 || vertex.position.x < 200.0f || vertex.position.x > 700.0f) {
        continue;
      }

      const float reach = std::fabs(vertex.position.y - 300.0f);
      const bool isDark = vertex.colour.red == 0 && vertex.colour.green == 0 && vertex.colour.blue == 0;

      if (isDark) {
        darkReach = std::max(darkReach, reach);
        firstDark = std::min(firstDark, index);
      } else {
        colorReach = std::max(colorReach, reach);
        firstColor = std::min(firstColor, index);
      }
    }

    REQUIRE(firstColor < mesh.vertices.size());
    REQUIRE(firstDark < mesh.vertices.size());
    CHECK(darkReach >= colorReach + 1.0f);
    // Вершины рисуются по порядку: подложка раньше, иначе она закрыла бы провод
    CHECK(firstDark < firstColor);
  }
}

TEST_CASE_FIXTURE(ViewUpdateFixture, "Гнездо порта лежит над проводами: провод уходит под оправу") {
  REQUIRE(this->count(".port") > 0);
  CHECK(this->get(".port")->GetZIndex() > this->wires()->GetZIndex());
  // Тянущийся провод и меню остаются над портами
  CHECK(this->get(".port")->GetZIndex() < this->draft()->GetZIndex());
}
