#include "./editorView.h"

#include <algorithm>
#include <cmath>
#include <charconv>

#include "../../engine/slotTable/slotTable.h"
#include "../graphLayout/graphLayout.h"
#include "../knob/knob.h"
#include "../wireLayer/wireLayer.h"

namespace {
  constexpr float PORT_FIRST_TOP = 34.0f;
  constexpr float PORT_STEP = 24.0f;
  constexpr float KNOB_DRAG_RANGE = 220.0f; // Столько точек мышью проходит вся шкала
  constexpr float FINE_FACTOR = 0.1f;
  constexpr float WHEEL_STEP = 0.02f;
  constexpr float PAN_WHEEL_STEP = 40.0f;
  constexpr float WIRE_TOLERANCE = 6.0f;
  constexpr float NEW_ROUTE_DEPTH = 0.5f;
  constexpr float FREE_POSITION_STEP = 28.0f;
  constexpr auto TOAST_TIME = std::chrono::seconds(5);
  constexpr uint32_t WIDE_BLOCK_KNOBS = 3;
  constexpr float DISABLED_OPACITY = 0.45f;

  constexpr Rml::EventId LISTENED_EVENTS[] = {
    Rml::EventId::Mousedown,
    Rml::EventId::Click,
    Rml::EventId::Dblclick,
    Rml::EventId::Dragstart,
    Rml::EventId::Drag,
    Rml::EventId::Dragdrop,
    Rml::EventId::Dragend,
    Rml::EventId::Mousescroll,
  };

  /**
   * Ближайший предок с атрибутом, начиная с самого элемента. Как closest() в DOM
   */
  Rml::Element* findClosest(Rml::Element* element, const char* attribute) {
    for (Rml::Element* current = element; current; current = current->GetParentNode()) {
      if (current->HasAttribute(attribute)) {
        return current;
      }
    }

    return nullptr;
  }

  std::string readText(Rml::Element* element, const char* attribute) {
    return element->GetAttribute<Rml::String>(attribute, "");
  }

  ModuleId readModule(Rml::Element* element) {
    return (ModuleId)element->GetAttribute<int>("data-module", 0);
  }

  /**
   * Длина числом: не нужно собирать текст, который RmlUi тут же разбирал бы обратно
   */
  Rml::Property makeLength(float value) {
    return Rml::Property(value, Rml::Unit::DP);
  }

  /**
   * Не зависит от локали. std::format здесь нельзя: ему нужна libstdc++ новее той, что есть в среде Reaper (D-019)
   */
  std::string formatDepth(float depth) {
    char text[16];
    const auto written = std::to_chars(text, text + sizeof(text), std::fabs(depth), std::chars_format::fixed, 2);

    return (depth < 0.0f ? "-" : "+") + std::string(text, written.ptr);
  }

  bool isModulator(const ModuleInstance& module) {
    return !module.descriptor->modOutputs.empty();
  }

  /**
   * Лежит ли блок модуля на показанном холсте. Только тогда к нему можно провести линию (D-063)
   */
  bool isOnGraph(const ModuleInstance& module, std::optional<ModuleId> graph) {
    return module.owner == graph && module.descriptor->role != MODULE_ROLE::MACROS;
  }
}

EditorView::EditorView(Rml::ElementDocument* document, EditorBackend& backend, std::function<void()> requestKeyboard)
  : document(document), backend(backend), requestKeyboard(std::move(requestKeyboard)) {
  this->canvas = document->GetElementById("canvas");
  this->world = document->GetElementById("world");
  this->breadcrumbs = document->GetElementById("breadcrumbs");
  this->sources = document->GetElementById("sources");
  this->macroCells = document->GetElementById("macro-cells");
  this->matrixRows = document->GetElementById("matrix-rows");
  this->presetButton = document->GetElementById("preset-button");
  this->wireRemove = document->GetElementById("wire-remove");
  this->popup = document->GetElementById("popup");
  this->prompt = document->GetElementById("prompt");
  this->promptLabel = document->GetElementById("prompt-label");
  this->promptInput = document->GetElementById("prompt-input");
  this->toast = document->GetElementById("toast");
  this->wires = dynamic_cast<WireLayer*>(document->GetElementById("wires"));
  this->draft = dynamic_cast<WireLayer*>(document->GetElementById("draft"));

  for (const Rml::EventId id : LISTENED_EVENTS) {
    document->AddEventListener(id, this);
  }

  // Поле ввода забирает Enter себе, поэтому клавиши слушаются до него, как с capture в DOM
  document->AddEventListener(Rml::EventId::Keydown, this, true);
}

EditorView::~EditorView() {
  for (const Rml::EventId id : LISTENED_EVENTS) {
    this->document->RemoveEventListener(id, this);
  }

  this->document->RemoveEventListener(Rml::EventId::Keydown, this, true);
}

std::optional<ModuleId> EditorView::getGraph() const {
  return this->graph;
}

void EditorView::openGraph(std::optional<ModuleId> graph) {
  this->graph = graph;
  this->seenStructure.clear();
  this->seenRevision = UINT64_MAX;
  this->selectedWire.reset();
}

std::string EditorView::getMessage() const {
  return this->toastDeadline ? std::string(this->toast->GetInnerRML()) : "";
}

float EditorView::getScale() const {
  return this->document->GetContext()->GetDensityIndependentPixelRatio();
}

Rml::Vector2f EditorView::readMouse(const Rml::Event& event) const {
  return { event.GetParameter("mouse_x", 0.0f), event.GetParameter("mouse_y", 0.0f) };
}

// /////////////////////////////
// // Синхронизация с моделью //
// /////////////////////////////

/**
 * Всё, что требует перестроить элементы. Значения параметров, глубины и положения сюда не входят:
 * они меняются на месте, поэтому ручка не пересоздаётся под мышью
 */
std::string EditorView::makeStructureKey() const {
  const SynthModel& model = this->backend.getModel();
  std::string key = this->graph ? std::to_string(*this->graph) : "main";

  for (const auto& [id, module] : model.getModules()) {
    key += "|" + std::to_string(id) + ":" + module.descriptor->type + ":" + (module.enabled ? "1" : "0");
    key += ":" + makeModuleLabel(model, module);
  }

  for (const Connection& connection : model.getConnections()) {
    key += "|" + std::to_string(connection.fromModule) + connection.fromPort + ">" + std::to_string(connection.toModule) + connection.toPort;
  }

  for (const ModRoute& route : model.getRoutes()) {
    key += "|" + std::to_string(route.source.module) + route.source.output + "~" + std::to_string(route.target.module) + route.target.parameter;
  }

  return key;
}

bool EditorView::update() {
  bool isChanged = std::exchange(this->isViewChanged, false);

  if (this->toastDeadline && std::chrono::steady_clock::now() >= *this->toastDeadline) {
    this->toastDeadline.reset();
    this->toast->SetClass("hidden", true);
    isChanged = true;
  }

  const uint64_t revision = this->backend.getRevision();

  if (revision == this->seenRevision) {
    return isChanged;
  }

  this->seenRevision = revision;

  const SynthModel& model = this->backend.getModel();

  // Генератор могли удалить: новый проект, другой пресет
  if (this->graph && !model.findModule(*this->graph)) {
    this->graph.reset();
  }

  std::string structure = this->makeStructureKey();

  if (structure != this->seenStructure) {
    this->seenStructure = std::move(structure);
    this->rebuild();
  }

  this->refresh();

  return true;
}

void EditorView::clearChildren(Rml::Element* element) {
  while (Rml::Element* child = element->GetFirstChild()) {
    element->RemoveChild(child);
  }
}

Rml::Element* EditorView::addElement(Rml::Element* parent, const std::string& tag, const std::string& className, const std::string& text) {
  Rml::Element* element = parent->AppendChild(this->document->CreateElement(tag));

  if (!className.empty()) {
    element->SetAttribute("class", className);
  }

  if (!text.empty()) {
    element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
  }

  return element;
}

void EditorView::rebuild() {
  // Перестройка уничтожает элементы: начатое действие продолжать не с чем
  if (this->drag.kind != DRAG_KIND::NONE && this->drag.kind != DRAG_KIND::CANVAS) {
    if (this->drag.kind == DRAG_KIND::KNOB) {
      const KnobView& view = this->knobs[this->drag.index];

      this->backend.endGesture(view.module, view.parameter->key);
    }

    this->drag = {};
    this->draft->clearDraft();
  }

  this->knobs.clear();
  this->routes.clear();
  this->blocks.clear();
  this->ports.clear();

  this->buildBreadcrumbs();
  this->buildSources();
  this->buildBlocks();
  this->buildMacros();
  this->buildMatrix();
  this->buildWires();
  this->setWorldOffset();
}

void EditorView::refresh() {
  const SynthModel& model = this->backend.getModel();
  const std::string presetName = this->backend.getPresetName();

  for (KnobView& view : this->knobs) {
    this->refreshKnob(view);
  }

  for (RouteView& view : this->routes) {
    this->refreshRoute(view);
  }

  const auto layout = computeLayout(model, this->graph);

  for (const auto& [id, block] : this->blocks) {
    // Блок под мышью ведёт мышь, положение уйдёт в модель, когда его отпустят
    if (this->drag.kind == DRAG_KIND::BLOCK && this->drag.element == block) {
      continue;
    }

    const ModuleInstance* module = model.findModule(id);

    this->setBlockPosition(block, module->position.value_or(layout.at(id)));
  }

  if (this->presetButton->GetInnerRML().empty() || presetName != this->seenPresetName) {
    this->seenPresetName = presetName;
    this->presetButton->SetInnerRML(Rml::StringUtilities::EncodeRml(presetName.empty() ? "Init" : presetName));
  }
}

void EditorView::refreshKnob(KnobView& view) {
  const SynthModel& model = this->backend.getModel();
  const float real = model.findModule(view.module)->parameters.at(view.parameter->key);
  const float base = toNormalized(*view.parameter, real);
  float low = base;
  float high = base;

  for (const ModRoute& route : model.getRoutes()) {
    if (route.target.module != view.module || route.target.parameter != view.parameter->key) {
      continue;
    }

    const ModuleInstance* source = model.findModule(route.source.module);
    const auto& outputs = source->descriptor->modOutputs;
    const auto output = std::ranges::find_if(outputs, [&route](const ModOutputDescriptor& candidate) {
      return candidate.key == route.source.output;
    });
    const bool isBipolar = output != outputs.end() && output->polarity == MOD_POLARITY::BIPOLAR;

    if (isBipolar) {
      low -= std::fabs(route.depth);
      high += std::fabs(route.depth);
    } else {
      low += std::min(route.depth, 0.0f);
      high += std::max(route.depth, 0.0f);
    }
  }

  view.knob->setValue(base);
  view.knob->setModulation(std::clamp(low, 0.0f, 1.0f), std::clamp(high, 0.0f, 1.0f));

  std::string text = formatParameter(*view.parameter, real);

  if (text != view.shownText) {
    view.valueText->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    view.shownText = std::move(text);
  }
}

void EditorView::refreshRoute(RouteView& view) {
  const auto& routes = this->backend.getModel().getRoutes();
  const auto route = std::ranges::find_if(routes, [&view](const ModRoute& candidate) {
    return candidate.source == view.key.first && candidate.target == view.key.second;
  });

  if (route == routes.end()) {
    return;
  }

  const float depth = route->depth;

  view.fill->SetProperty(Rml::PropertyId::Left, Rml::Property(depth < 0.0f ? 50.0f * (1.0f + depth) : 50.0f, Rml::Unit::PERCENT));
  view.fill->SetProperty(Rml::PropertyId::Width, Rml::Property(50.0f * std::fabs(depth), Rml::Unit::PERCENT));
  view.value->SetInnerRML(formatDepth(depth));
}

void EditorView::buildBreadcrumbs() {
  this->clearChildren(this->breadcrumbs);

  Rml::Element* main = this->addElement(this->breadcrumbs, "div", this->graph ? "crumb" : "crumb current", "Main");

  if (!this->graph) {
    return;
  }

  main->SetAttribute("data-action", "close-graph");

  const SynthModel& model = this->backend.getModel();

  this->addElement(this->breadcrumbs, "div", "crumb-separator", "›");
  this->addElement(this->breadcrumbs, "div", "crumb current", makeModuleLabel(model, *model.findModule(*this->graph)));
}

/**
 * Выходы модуляции самого генератора видны внутри него: velocity нужна как раз голосу (D-052)
 */
void EditorView::buildSources() {
  this->clearChildren(this->sources);

  if (!this->graph) {
    return;
  }

  const ModuleInstance* generator = this->backend.getModel().findModule(*this->graph);

  for (const ModOutputDescriptor& output : generator->descriptor->modOutputs) {
    Rml::Element* source = this->addElement(this->sources, "div", "source", output.name);

    this->addPort(source, generator->id, output.key, "port modulation");
  }
}

void EditorView::buildBlocks() {
  this->clearChildren(this->world);

  const SynthModel& model = this->backend.getModel();
  const auto layout = computeLayout(model, this->graph);

  for (const auto& [id, module] : model.getModules()) {
    if (isOnGraph(module, this->graph)) {
      this->buildBlock(module, module.position.value_or(layout.at(id)));
    }
  }
}

void EditorView::buildBlock(const ModuleInstance& module, ModulePosition position) {
  const SynthModel& model = this->backend.getModel();
  const ModuleDescriptor& descriptor = *module.descriptor;
  const bool isOutput = descriptor.role == MODULE_ROLE::OUTPUT;
  const bool isGenerator = descriptor.role == MODULE_ROLE::GENERATOR;
  const bool isSmall = module.isStub() || (descriptor.parameters.size() < WIDE_BLOCK_KNOBS && !isGenerator);
  std::string className = "block";

  className += isSmall ? " small" : "";
  className += module.enabled ? "" : " disabled";
  className += module.isStub() ? " stub" : "";

  Rml::Element* block = this->addElement(this->world, "div", className);

  block->SetAttribute("data-drag", "block");
  block->SetAttribute("data-module", (int)module.id);
  this->blocks[module.id] = block;
  this->setBlockPosition(block, position);

  // Дробное число в тексте стилей RmlUi читает по локали процесса, а её мог поменять хост: 0.45 стало бы нулём
  if (!module.enabled) {
    block->SetProperty(Rml::PropertyId::Opacity, Rml::Property(DISABLED_OPACITY, Rml::Unit::NUMBER));
  }

  Rml::Element* header = this->addElement(block, "div", "block-header");

  this->addElement(header, "div", "block-title", isOutput ? descriptor.name : makeModuleLabel(model, module));

  if (!isOutput) {
    if (!module.isStub()) {
      Rml::Element* power = this->addElement(header, "div", "block-button");

      power->SetAttribute("data-action", "toggle");
      power->SetAttribute("data-module", (int)module.id);
      this->addElement(power, "div", "power-dot");
    }

    Rml::Element* remove = this->addElement(header, "div", "block-button", "×");

    remove->SetAttribute("data-action", "remove");
    remove->SetAttribute("data-module", (int)module.id);
  }

  Rml::Element* body = this->addElement(block, "div", "block-body");

  if (module.isStub()) {
    this->addElement(block, "div", "block-note", "Unknown module");
  } else {
    for (const ParameterDescriptor& parameter : descriptor.parameters) {
      this->buildKnob(body, module, parameter);
    }
  }

  if (isGenerator) {
    Rml::Element* open = this->addElement(block, "div", "button open-button", "Open voice ›");

    open->SetAttribute("data-action", "open");
    open->SetAttribute("data-module", (int)module.id);
  }

  this->buildPorts(block, module);
}

void EditorView::buildKnob(Rml::Element* parent, const ModuleInstance& module, const ParameterDescriptor& parameter) {
  Rml::Element* cell = this->addElement(parent, "div", "knob-cell");

  cell->SetAttribute("data-module", (int)module.id);
  cell->SetAttribute("data-parameter", parameter.key);

  auto* knob = dynamic_cast<Knob*>(this->addElement(cell, "knob", ""));

  knob->SetAttribute("data-drag", "knob");

  // У параметра с нулём внутри диапазона дуга идёт от нуля: так видно знак
  knob->setOrigin(parameter.minValue < 0.0f && parameter.maxValue > 0.0f ? toNormalized(parameter, 0.0f) : 0.0f);

  this->addElement(cell, "div", "knob-name", parameter.shortName.empty() ? parameter.name : parameter.shortName);

  Rml::Element* value = this->addElement(cell, "div", "knob-value");

  if (this->hasOffCanvasRoute(module, parameter)) {
    this->addElement(cell, "div", "knob-mark");
  }

  // Слотов не хватило: параметр работает, но хост его не видит (D-011)
  if (!this->backend.isExported(module.id, parameter.key)) {
    this->addElement(cell, "div", "knob-mark unexported");
  }

  this->knobs.push_back({ knob, cell, value, module.id, &parameter, "" });
}

/**
 * Есть ли маршрут на параметр, который нельзя нарисовать линией: его источник на другом уровне (D-063)
 */
bool EditorView::hasOffCanvasRoute(const ModuleInstance& target, const ParameterDescriptor& parameter) const {
  const SynthModel& model = this->backend.getModel();

  return std::ranges::any_of(model.getRoutes(), [&](const ModRoute& route) {
    return route.target.module == target.id
      && route.target.parameter == parameter.key
      && !isOnGraph(*model.findModule(route.source.module), this->graph);
  });
}

Rml::Element* EditorView::addPort(Rml::Element* parent, ModuleId module, const std::string& port, const std::string& className) {
  Rml::Element* element = this->addElement(parent, "div", className);

  element->SetAttribute("data-module", (int)module);
  element->SetAttribute("data-port", port);
  element->SetAttribute("data-drag", "wire");
  this->ports.push_back(element);

  return element;
}

void EditorView::buildPorts(Rml::Element* block, const ModuleInstance& module) {
  const ModuleDescriptor& descriptor = *module.descriptor;

  const auto placeRow = [](Rml::Element* port, size_t row) {
    port->SetProperty(Rml::PropertyId::Top, makeLength(PORT_FIRST_TOP + (float)row * PORT_STEP));
  };

  for (size_t index = 0; index < descriptor.inputs.size(); index++) {
    placeRow(this->addPort(block, module.id, descriptor.inputs[index].key, "port audio input"), index);
  }

  for (size_t index = 0; index < descriptor.outputs.size(); index++) {
    placeRow(this->addPort(block, module.id, descriptor.outputs[index].key, "port audio output"), index);
  }

  for (size_t index = 0; index < descriptor.modOutputs.size(); index++) {
    const ModOutputDescriptor& output = descriptor.modOutputs[index];
    const size_t row = descriptor.outputs.size() + index;

    placeRow(this->addPort(block, module.id, output.key, "port modulation output"), row);

    // Единственный выход модулятора в подписи не нуждается
    if (output.key != "out") {
      placeRow(this->addElement(block, "div", "port-label", output.name), row);
    }
  }
}

Rml::Element* EditorView::findPort(ModuleId module, const std::string& port, bool isModulation, bool isInput) const {
  for (Rml::Element* element : this->ports) {
    if (readModule(element) == module
      && readText(element, "data-port") == port
      && element->IsClassSet("modulation") == isModulation
      && element->IsClassSet("input") == isInput) {
      return element;
    }
  }

  return nullptr;
}

Rml::Element* EditorView::findKnobElement(ModuleId module, const std::string& parameter) const {
  for (const KnobView& view : this->knobs) {
    if (view.module == module && view.parameter->key == parameter) {
      return view.knob;
    }
  }

  return nullptr;
}

void EditorView::buildWires() {
  const SynthModel& model = this->backend.getModel();
  std::vector<Wire> list;

  this->wireRefs.clear();

  for (const Connection& connection : model.getConnections()) {
    Rml::Element* from = this->findPort(connection.fromModule, connection.fromPort, false, false);
    Rml::Element* to = this->findPort(connection.toModule, connection.toPort, false, true);

    if (from && to) {
      list.push_back({ from, to, WIRE_KIND::AUDIO });
      this->wireRefs.emplace_back(connection);
    }
  }

  for (const ModRoute& route : model.getRoutes()) {
    Rml::Element* from = this->findPort(route.source.module, route.source.output, true, false);
    Rml::Element* to = this->findKnobElement(route.target.module, route.target.parameter);

    // Источник из панели или с другого уровня на холсте не лежит: такой маршрут виден как метка на ручке (D-063)
    if (from && to && isOnGraph(*model.findModule(route.source.module), this->graph)) {
      list.push_back({ from, to, WIRE_KIND::MODULATION });
      this->wireRefs.emplace_back(RouteKey{ route.source, route.target });
    }
  }

  this->wires->setWires(std::move(list));

  const auto selected = this->selectedWire ? std::ranges::find(this->wireRefs, *this->selectedWire) : this->wireRefs.end();

  if (selected == this->wireRefs.end()) {
    this->selectWire(std::nullopt);
  } else {
    this->wires->selectWire((size_t)(selected - this->wireRefs.begin()));
  }
}

void EditorView::buildMacros() {
  this->clearChildren(this->macroCells);

  const ModuleInstance& macros = *this->backend.getModel().findModule(MACROS_MODULE_ID);

  for (size_t index = 0; index < macros.descriptor->parameters.size(); index++) {
    this->buildKnob(this->macroCells, macros, macros.descriptor->parameters[index]);

    const ModOutputDescriptor& output = macros.descriptor->modOutputs[index];

    this->addPort(this->knobs.back().cell, MACROS_MODULE_ID, output.key, "port modulation");
  }
}

std::string EditorView::makeSourceLabel(const ModSource& source) const {
  const SynthModel& model = this->backend.getModel();
  const ModuleInstance& module = *model.findModule(source.module);
  const auto& outputs = module.descriptor->modOutputs;
  const auto output = std::ranges::find_if(outputs, [&source](const ModOutputDescriptor& candidate) {
    return candidate.key == source.output;
  });
  const std::string outputName = output != outputs.end() ? output->name : source.output;

  if (module.descriptor->role == MODULE_ROLE::MACROS) {
    return outputName;
  }

  if (module.descriptor->role == MODULE_ROLE::GENERATOR) {
    return "G" + std::to_string(findModuleNumber(model, module)) + " " + outputName;
  }

  const std::string label = makeModuleLabel(model, module);

  if (module.owner) {
    return "G" + std::to_string(findModuleNumber(model, *model.findModule(*module.owner))) + " " + label;
  }

  return label;
}

std::string EditorView::makeTargetLabel(const ModTarget& target) const {
  const SynthModel& model = this->backend.getModel();
  const ModuleInstance& module = *model.findModule(target.module);

  for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
    if (parameter.key == target.parameter) {
      return makeParameterName(model, module, parameter);
    }
  }

  return target.parameter;
}

void EditorView::buildMatrix() {
  this->clearChildren(this->matrixRows);

  const auto& list = this->backend.getModel().getRoutes();

  if (list.empty()) {
    this->addElement(this->matrixRows, "div", "matrix-empty", "No routes. Drag an orange output onto a knob.");
    return;
  }

  for (size_t index = 0; index < list.size(); index++) {
    const ModRoute& route = list[index];
    Rml::Element* row = this->addElement(this->matrixRows, "div", "route");

    this->addElement(row, "div", "route-source", this->makeSourceLabel(route.source));
    this->addElement(row, "div", "route-target", this->makeTargetLabel(route.target));

    Rml::Element* slider = this->addElement(row, "div", "route-depth");

    slider->SetAttribute("data-drag", "depth");
    slider->SetAttribute("data-route", (int)index);

    Rml::Element* fill = this->addElement(slider, "div", "route-fill");

    this->addElement(slider, "div", "route-center");

    Rml::Element* value = this->addElement(row, "div", "route-value");
    Rml::Element* remove = this->addElement(row, "div", "block-button", "×");

    remove->SetAttribute("data-action", "remove-route");
    remove->SetAttribute("data-route", (int)index);

    this->routes.push_back({ fill, value, { route.source, route.target } });
  }
}

void EditorView::setWorldOffset() {
  const Rml::Vector2f pan = this->pans[this->graph];

  this->world->SetProperty(Rml::PropertyId::Left, makeLength(pan.x));
  this->world->SetProperty(Rml::PropertyId::Top, makeLength(pan.y));
}

void EditorView::setBlockPosition(Rml::Element* block, ModulePosition position) {
  block->SetProperty(Rml::PropertyId::Left, makeLength(position.x));
  block->SetProperty(Rml::PropertyId::Top, makeLength(position.y));
}

/**
 * Записывает авторасстановку в модель: блоки, которые не двигали, не должны прыгать, когда схема изменится
 */
void EditorView::freezeLayout() {
  const SynthModel& model = this->backend.getModel();
  std::vector<SetModulePosition> commands;

  for (const auto& [id, position] : computeLayout(model, this->graph)) {
    if (!model.findModule(id)->position) {
      commands.push_back({ id, position });
    }
  }

  for (const SetModulePosition& command : commands) {
    this->report(this->backend.apply(command));
  }
}

ModulePosition EditorView::findFreePosition(ModulePosition start) const {
  const SynthModel& model = this->backend.getModel();
  ModulePosition position = start;

  const auto isTaken = [&model, &position, this] {
    return std::ranges::any_of(model.getModules(), [&position, this](const auto& entry) {
      const ModuleInstance& module = entry.second;

      return module.owner == this->graph
        && module.position
        && std::fabs(module.position->x - position.x) < 8.0f
        && std::fabs(module.position->y - position.y) < 8.0f;
    });
  };

  while (isTaken()) {
    position.x += FREE_POSITION_STEP;
    position.y += FREE_POSITION_STEP;
  }

  return position;
}

// /////////////
// // События //
// /////////////

void EditorView::ProcessEvent(Rml::Event& event) {
  switch (event.GetId()) {
    case Rml::EventId::Mousedown:
      this->handleMouseDown(event);
      break;
    case Rml::EventId::Click:
      this->handleClick(event);
      break;
    case Rml::EventId::Dblclick:
      this->handleDoubleClick(event);
      break;
    case Rml::EventId::Dragstart:
      this->handleDragStart(event);
      break;
    case Rml::EventId::Drag:
      this->handleDrag(event);
      break;
    case Rml::EventId::Dragdrop:
      this->handleDrop(event);
      break;
    case Rml::EventId::Dragend:
      this->handleDragEnd();
      break;
    case Rml::EventId::Mousescroll:
      this->handleScroll(event);
      break;
    case Rml::EventId::Keydown:
      this->handleKey(event);
      break;
    default:
      break;
  }

  this->isViewChanged = true;
}

void EditorView::handleMouseDown(Rml::Event& event) {
  Rml::Element* target = event.GetTargetElement();
  const int button = event.GetParameter("button", 0);
  const Rml::Vector2f mouse = this->readMouse(event);

  if (!this->popup->IsClassSet("hidden") && !this->popup->Contains(target)) {
    this->hideMenu();
  }

  const bool isCanvas = target == this->canvas || target == this->world;

  if (button == 0 && isCanvas) {
    const auto wire = this->wires->findWire(mouse, WIRE_TOLERANCE * this->getScale());

    this->selectWire(wire ? std::optional(this->wireRefs[*wire]) : std::nullopt);
    return;
  }

  if (button != 1) {
    return;
  }

  if (isCanvas) {
    const Rml::Vector2f offset = (mouse - this->canvas->GetAbsoluteOffset(Rml::BoxArea::Border)) / this->getScale() - this->pans[this->graph];

    this->showModuleMenu(mouse, { offset.x, offset.y });
    return;
  }

  // Параметр можно ввести числом: мышью не всегда попасть в нужное значение
  if (Rml::Element* cell = findClosest(target, "data-parameter")) {
    for (const KnobView& view : this->knobs) {
      if (view.cell == cell) {
        this->promptKnobValue(view);
        return;
      }
    }
  }
}

void EditorView::handleClick(Rml::Event& event) {
  if (Rml::Element* element = findClosest(event.GetTargetElement(), "data-action")) {
    this->runAction(readText(element, "data-action"), element);
  }
}

void EditorView::handleDoubleClick(Rml::Event& event) {
  Rml::Element* target = event.GetTargetElement();

  if (KnobView* view = this->findKnob(target)) {
    this->backend.beginGesture(view->module, view->parameter->key);
    this->report(this->backend.apply(SetParameter{ view->module, view->parameter->key, this->backend.getDefaultValue(view->module, *view->parameter) }));
    this->backend.endGesture(view->module, view->parameter->key);
    return;
  }

  if (findClosest(target, "data-action")) {
    return;
  }

  // Вход в генератор (D-063)
  if (Rml::Element* block = findClosest(target, "data-drag"); block && block->IsClassSet("block")) {
    const ModuleInstance* module = this->backend.getModel().findModule(readModule(block));

    if (module && module->descriptor->role == MODULE_ROLE::GENERATOR) {
      this->openGraph(module->id);
    }
  }
}

void EditorView::runAction(const std::string& action, Rml::Element* element) {
  const ModuleId module = readModule(element);
  const size_t route = (size_t)element->GetAttribute<int>("data-route", 0);
  const Rml::Vector2f below = element->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(0.0f, element->GetBox().GetSize(Rml::BoxArea::Border).y + 2.0f);

  if (action == "toggle") {
    const ModuleInstance* instance = this->backend.getModel().findModule(module);

    if (instance) {
      this->report(this->backend.apply(SetModuleEnabled{ module, !instance->enabled }));
    }
  } else if (action == "remove") {
    this->report(this->backend.apply(RemoveModule{ module }));
  } else if (action == "open") {
    this->openGraph(module);
  } else if (action == "close-graph") {
    this->openGraph(std::nullopt);
  } else if (action == "remove-wire") {
    this->removeSelectedWire();
  } else if (action == "remove-route") {
    const auto& list = this->backend.getModel().getRoutes();

    if (route < list.size()) {
      this->report(this->backend.apply(RemoveRoute{ list[route].source, list[route].target }));
    }
  } else if (action == "menu") {
    const size_t index = (size_t)element->GetAttribute<int>("data-item", 0);

    // Действие может открыть следующее меню, поэтому список забирается заранее
    std::function<void()> run = this->menuActions.at(index);

    this->hideMenu();
    run();
  } else if (action == "presets") {
    this->showPresetMenu();
  } else if (action == "save") {
    this->savePreset();
  } else if (action == "save-as") {
    this->savePresetAs();
  } else if (action == "add-module") {
    const Rml::Vector2f size = this->canvas->GetBox().GetSize(Rml::BoxArea::Border) / this->getScale();
    const Rml::Vector2f center = size * 0.5f - this->pans[this->graph];

    this->showModuleMenu(below, { center.x - 126.0f, center.y - 60.0f });
  } else if (action == "add-route") {
    this->showRouteSourceMenu(below);
  } else if (action == "prompt-accept") {
    this->acceptPrompt();
  } else if (action == "prompt-cancel") {
    this->hidePrompt();
  }
}

EditorView::KnobView* EditorView::findKnob(const Rml::Element* element) {
  const auto view = std::ranges::find_if(this->knobs, [element](const KnobView& candidate) {
    return candidate.knob == element;
  });

  return view != this->knobs.end() ? &*view : nullptr;
}

void EditorView::handleDragStart(Rml::Event& event) {
  Rml::Element* element = event.GetTargetElement();
  const std::string kind = readText(element, "data-drag");
  const Rml::Vector2f mouse = this->readMouse(event);

  this->drag = {};
  this->drag.element = element;
  this->drag.lastMouse = mouse;
  this->selectWire(std::nullopt);

  if (kind == "canvas") {
    this->drag.kind = DRAG_KIND::CANVAS;
  } else if (kind == "block") {
    this->freezeLayout();
    this->drag.kind = DRAG_KIND::BLOCK;
    this->drag.grabOffset = mouse - element->GetAbsoluteOffset(Rml::BoxArea::Border);
  } else if (kind == "knob") {
    if (KnobView* view = this->findKnob(element)) {
      this->drag.kind = DRAG_KIND::KNOB;
      this->drag.index = (size_t)(view - this->knobs.data());
      this->drag.value = view->knob->getValue();
      this->backend.beginGesture(view->module, view->parameter->key);
    }
  } else if (kind == "wire") {
    this->startWireDrag(element);
  } else if (kind == "depth") {
    this->drag.kind = DRAG_KIND::DEPTH;
    this->drag.index = (size_t)element->GetAttribute<int>("data-route", 0);
    this->setDepthFromMouse(mouse.x);
  }
}

void EditorView::handleDrag(Rml::Event& event) {
  const Rml::Vector2f mouse = this->readMouse(event);
  const Rml::Vector2f delta = mouse - this->drag.lastMouse;
  const float scale = this->getScale();

  this->drag.lastMouse = mouse;

  switch (this->drag.kind) {
    case DRAG_KIND::CANVAS:
      this->pans[this->graph] = this->pans[this->graph] + delta / scale;
      this->setWorldOffset();
      break;

    case DRAG_KIND::BLOCK: {
      const Rml::Vector2f offset = (mouse - this->drag.grabOffset - this->world->GetAbsoluteOffset(Rml::BoxArea::Border)) / scale;

      this->setBlockPosition(this->drag.element, { offset.x, offset.y });
      break;
    }

    case DRAG_KIND::KNOB:
      this->changeKnob(this->knobs[this->drag.index], -delta.y / scale / KNOB_DRAG_RANGE, event.GetParameter("shift_key", 0) != 0);
      break;

    case DRAG_KIND::WIRE:
      this->draft->moveDraft(mouse);
      break;

    case DRAG_KIND::DEPTH:
      this->setDepthFromMouse(mouse.x);
      break;

    case DRAG_KIND::NONE:
      break;
  }
}

void EditorView::handleDrop(Rml::Event& event) {
  if (this->drag.kind == DRAG_KIND::WIRE) {
    this->finishWireDrag(event.GetTargetElement());
  }
}

void EditorView::handleDragEnd() {
  switch (this->drag.kind) {
    case DRAG_KIND::BLOCK: {
      const Rml::Vector2f offset = (this->drag.element->GetAbsoluteOffset(Rml::BoxArea::Border) - this->world->GetAbsoluteOffset(Rml::BoxArea::Border)) / this->getScale();

      this->report(this->backend.apply(SetModulePosition{ readModule(this->drag.element), { offset.x, offset.y } }));
      break;
    }

    case DRAG_KIND::KNOB: {
      const KnobView& view = this->knobs[this->drag.index];

      this->backend.endGesture(view.module, view.parameter->key);
      break;
    }

    case DRAG_KIND::WIRE:
      this->clearLegalTargets();
      this->draft->clearDraft();
      break;

    default:
      break;
  }

  this->drag = {};
}

void EditorView::handleScroll(Rml::Event& event) {
  Rml::Element* target = event.GetTargetElement();
  const float wheel = event.GetParameter("wheel_delta_y", 0.0f);

  if (KnobView* view = this->findKnob(target)) {
    this->backend.beginGesture(view->module, view->parameter->key);

    // Колесо у ступенчатого параметра переключает ровно на одну ступень
    const float step = view->parameter->isStepped ? 1.0f / (view->parameter->maxValue - view->parameter->minValue) : WHEEL_STEP;
    const float position = view->knob->getValue() + (wheel < 0.0f ? step : -step);

    this->setKnobValue(*view, position);
    this->backend.endGesture(view->module, view->parameter->key);
    event.StopPropagation();
    return;
  }

  if (this->canvas->Contains(target)) {
    const float wheelX = event.GetParameter("wheel_delta_x", 0.0f);
    const bool isSideways = event.GetParameter("shift_key", 0) != 0;
    const Rml::Vector2f delta = isSideways ? Rml::Vector2f(wheel, wheelX) : Rml::Vector2f(wheelX, wheel);

    this->pans[this->graph] = this->pans[this->graph] - delta * PAN_WHEEL_STEP;
    this->setWorldOffset();
    this->selectWire(std::nullopt);
  }
}

void EditorView::handleKey(Rml::Event& event) {
  const int key = event.GetParameter("key_identifier", 0);
  const bool isPromptOpen = !this->prompt->IsClassSet("hidden");

  if (key == Rml::Input::KI_ESCAPE) {
    this->hideMenu();
    this->hidePrompt();
  } else if (key == Rml::Input::KI_RETURN && isPromptOpen) {
    this->acceptPrompt();
  } else if ((key == Rml::Input::KI_DELETE || key == Rml::Input::KI_BACK) && !isPromptOpen) {
    this->removeSelectedWire();
  }
}

// /////////////
// // Провода //
// /////////////

void EditorView::startWireDrag(Rml::Element* element) {
  this->drag.kind = DRAG_KIND::WIRE;
  this->drag.wire = {
    .module = readModule(element),
    .port = readText(element, "data-port"),
    .isModulation = element->IsClassSet("modulation"),
    .isInput = element->IsClassSet("input"),
  };
  this->draft->setDraft(element, this->drag.wire.isModulation ? WIRE_KIND::MODULATION : WIRE_KIND::AUDIO);
  this->markLegalTargets();
}

/**
 * Подсвечивает, куда провод можно отпустить: правила те же, что у команды, поэтому вид не расходится с моделью
 */
void EditorView::markLegalTargets() {
  const SynthModel& model = this->backend.getModel();
  const WireDrag& wire = this->drag.wire;

  if (wire.isModulation) {
    for (const KnobView& view : this->knobs) {
      view.cell->SetClass("legal", model.checkRoute({ wire.module, wire.port }, { view.module, view.parameter->key }).has_value());
    }

    return;
  }

  for (Rml::Element* port : this->ports) {
    if (port->IsClassSet("modulation") || port->IsClassSet("input") == wire.isInput) {
      continue;
    }

    const Connection connection = wire.isInput
      ? Connection{ readModule(port), readText(port, "data-port"), wire.module, wire.port }
      : Connection{ wire.module, wire.port, readModule(port), readText(port, "data-port") };

    port->SetClass("legal", model.checkConnection(connection).has_value());
  }
}

void EditorView::clearLegalTargets() {
  for (const KnobView& view : this->knobs) {
    view.cell->SetClass("legal", false);
  }

  for (Rml::Element* port : this->ports) {
    port->SetClass("legal", false);
  }
}

/**
 * Провод отпущен. Подсказка не обязательна: отпустить можно и на блок, если подходящий порт у него один
 */
void EditorView::finishWireDrag(Rml::Element* target) {
  const WireDrag wire = this->drag.wire;

  if (wire.isModulation) {
    Rml::Element* cell = findClosest(target, "data-parameter");

    if (cell && cell->IsClassSet("legal")) {
      this->report(this->backend.apply(AddRoute{ { { wire.module, wire.port }, { readModule(cell), readText(cell, "data-parameter") }, NEW_ROUTE_DEPTH } }));
    }

    return;
  }

  Rml::Element* port = findClosest(target, "data-port");

  if (!port || !port->IsClassSet("legal")) {
    port = nullptr;

    if (Rml::Element* block = findClosest(target, "data-drag"); block) {
      const ModuleId module = readModule(block);
      std::vector<Rml::Element*> candidates;

      for (Rml::Element* candidate : this->ports) {
        if (candidate->IsClassSet("legal") && readModule(candidate) == module) {
          candidates.push_back(candidate);
        }
      }

      if (candidates.size() == 1) {
        port = candidates.front();
      }
    }
  }

  if (!port) {
    return;
  }

  const Connection connection = wire.isInput
    ? Connection{ readModule(port), readText(port, "data-port"), wire.module, wire.port }
    : Connection{ wire.module, wire.port, readModule(port), readText(port, "data-port") };

  this->report(this->backend.apply(Connect{ connection }));
}

void EditorView::selectWire(std::optional<WireRef> wire) {
  this->selectedWire = std::move(wire);

  const auto found = this->selectedWire ? std::ranges::find(this->wireRefs, *this->selectedWire) : this->wireRefs.end();

  if (found == this->wireRefs.end()) {
    this->selectedWire.reset();
    this->wires->selectWire(std::nullopt);
    this->wireRemove->SetClass("hidden", true);
    return;
  }

  const size_t index = (size_t)(found - this->wireRefs.begin());
  const float scale = this->getScale();
  const Rml::Vector2f middle = (this->wires->findMiddle(index) - this->canvas->GetAbsoluteOffset(Rml::BoxArea::Border)) / scale;

  this->wires->selectWire(index);
  this->wireRemove->SetProperty(Rml::PropertyId::Left, makeLength(middle.x - 10.0f));
  this->wireRemove->SetProperty(Rml::PropertyId::Top, makeLength(middle.y - 10.0f));
  this->wireRemove->SetClass("hidden", false);
}

void EditorView::removeSelectedWire() {
  if (!this->selectedWire) {
    return;
  }

  if (const auto* connection = std::get_if<Connection>(&*this->selectedWire)) {
    this->report(this->backend.apply(Disconnect{ *connection }));
  } else {
    const RouteKey& route = std::get<RouteKey>(*this->selectedWire);

    this->report(this->backend.apply(RemoveRoute{ route.first, route.second }));
  }

  this->selectWire(std::nullopt);
}

// ///////////////
// // Параметры //
// ///////////////

void EditorView::setKnobValue(const KnobView& view, float position) {
  this->report(this->backend.apply(SetParameter{ view.module, view.parameter->key, toReal(*view.parameter, position) }));
}

void EditorView::changeKnob(KnobView& view, float delta, bool isFine) {
  this->drag.value = std::clamp(this->drag.value + delta * (isFine ? FINE_FACTOR : 1.0f), 0.0f, 1.0f);
  this->setKnobValue(view, this->drag.value);
}

void EditorView::promptKnobValue(const KnobView& view) {
  const ModuleId module = view.module;
  const ParameterDescriptor* parameter = view.parameter;

  this->showPrompt(parameter->name, view.shownText, [this, module, parameter](const std::string& text) {
    const auto value = parseParameter(*parameter, text);

    if (!value) {
      this->showMessage("Не удалось прочитать значение: " + text);
      return;
    }

    this->backend.beginGesture(module, parameter->key);
    this->report(this->backend.apply(SetParameter{ module, parameter->key, *value }));
    this->backend.endGesture(module, parameter->key);
  });
}

void EditorView::setDepthFromMouse(float mouseX) {
  const auto& list = this->backend.getModel().getRoutes();

  if (this->drag.index >= list.size()) {
    return;
  }

  const float left = this->drag.element->GetAbsoluteOffset(Rml::BoxArea::Border).x;
  const float width = this->drag.element->GetBox().GetSize(Rml::BoxArea::Border).x;
  const float depth = std::clamp((mouseX - left) / width * 2.0f - 1.0f, -1.0f, 1.0f);
  const ModRoute& route = list[this->drag.index];

  this->report(this->backend.apply(SetRouteDepth{ route.source, route.target, depth }));
}

// //////////
// // Меню //
// //////////

void EditorView::showMenu(Rml::Vector2f position, std::vector<std::pair<std::string, std::function<void()>>> items) {
  this->clearChildren(this->popup);
  this->menuActions.clear();

  for (auto& [label, action] : items) {
    if (!action) {
      this->addElement(this->popup, "div", "menu-heading", label);
      continue;
    }

    Rml::Element* item = this->addElement(this->popup, "div", "menu-item", label);

    item->SetAttribute("data-action", "menu");
    item->SetAttribute("data-item", (int)this->menuActions.size());
    this->menuActions.push_back(std::move(action));
  }

  // Меню не должно уходить за край окна: в нём может быть длинный список параметров
  const float scale = this->getScale();
  const Rml::Vector2f window = this->document->GetBox().GetSize(Rml::BoxArea::Border) / scale;
  const Rml::Vector2f place = position / scale;

  this->popup->SetProperty(Rml::PropertyId::Left, makeLength(std::min(place.x, window.x - 220.0f)));
  this->popup->SetProperty(Rml::PropertyId::Top, makeLength(std::min(place.y, std::max(window.y - 330.0f, 0.0f))));
  this->popup->SetClass("hidden", false);
}

void EditorView::hideMenu() {
  this->popup->SetClass("hidden", true);
}

void EditorView::showModuleMenu(Rml::Vector2f position, ModulePosition place) {
  std::vector<std::pair<std::string, std::function<void()>>> items;

  for (const ModuleDescriptor* descriptor : this->backend.getRegistry().getAll()) {
    const bool isUserModule = descriptor->role == MODULE_ROLE::REGULAR || descriptor->role == MODULE_ROLE::GENERATOR;

    // Вложенные генераторы запрещены (D-004), а для остальных правила описаны в самом модуле
    if (!isUserModule || (this->graph && !descriptor->isAllowedInPoly)) {
      continue;
    }

    items.emplace_back(descriptor->name, [this, type = descriptor->type, place] {
      this->addModule(type, place);
    });
  }

  this->showMenu(position, std::move(items));
}

void EditorView::addModule(const std::string& type, ModulePosition place) {
  this->freezeLayout();

  const CommandResult result = this->backend.apply(AddModule{ type, this->graph });

  if (!result) {
    this->report(result);
    return;
  }

  this->report(this->backend.apply(SetModulePosition{ **result, this->findFreePosition(place) }));
}

void EditorView::showPresetMenu() {
  std::vector<std::pair<std::string, std::function<void()>>> items;

  items.emplace_back("Init", [this] {
    this->report(this->backend.loadDefaultPreset());
  });

  for (const std::string& name : this->backend.listPresets()) {
    items.emplace_back(name, [this, name] {
      this->report(this->backend.loadPreset(name));
    });
  }

  const Rml::Vector2f offset = this->presetButton->GetAbsoluteOffset(Rml::BoxArea::Border);

  this->showMenu(offset + Rml::Vector2f(0.0f, this->presetButton->GetBox().GetSize(Rml::BoxArea::Border).y + 2.0f), std::move(items));
}

void EditorView::showRouteSourceMenu(Rml::Vector2f position) {
  const SynthModel& model = this->backend.getModel();
  std::vector<std::pair<std::string, std::function<void()>>> items;

  items.emplace_back("Source", nullptr);

  for (const auto& [id, module] : model.getModules()) {
    if (!isModulator(module) || module.isStub()) {
      continue;
    }

    for (const ModOutputDescriptor& output : module.descriptor->modOutputs) {
      const ModSource source = { id, output.key };

      items.emplace_back(this->makeSourceLabel(source), [this, source, position] {
        this->showRouteTargetMenu(position, source);
      });
    }
  }

  this->showMenu(position, std::move(items));
}

void EditorView::showRouteTargetMenu(Rml::Vector2f position, ModSource source) {
  const SynthModel& model = this->backend.getModel();
  std::vector<std::pair<std::string, std::function<void()>>> items;

  items.emplace_back("Target of " + this->makeSourceLabel(source), nullptr);

  for (const auto& [id, module] : model.getModules()) {
    if (module.isStub()) {
      continue;
    }

    for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
      const ModTarget target = { id, parameter.key };

      if (model.checkRoute(source, target)) {
        items.emplace_back(this->makeTargetLabel(target), [this, source, target] {
          this->report(this->backend.apply(AddRoute{ { source, target, NEW_ROUTE_DEPTH } }));
        });
      }
    }
  }

  this->showMenu(position, std::move(items));
}

void EditorView::showPrompt(const std::string& label, const std::string& value, std::function<void(const std::string&)> action) {
  this->promptAction = std::move(action);
  this->promptLabel->SetInnerRML(Rml::StringUtilities::EncodeRml(label));
  this->promptInput->SetAttribute("value", value);
  this->prompt->SetClass("hidden", false);
  this->promptInput->Focus();

  // Текст печатается в плагин, поэтому только сейчас клавиатуру нужно забрать у DAW
  this->requestKeyboard();
}

void EditorView::acceptPrompt() {
  const std::string value = readText(this->promptInput, "value");
  std::function<void(const std::string&)> action = std::exchange(this->promptAction, nullptr);

  this->hidePrompt();

  if (action) {
    action(value);
  }
}

void EditorView::hidePrompt() {
  this->promptInput->Blur();
  this->prompt->SetClass("hidden", true);
}

void EditorView::savePreset() {
  const std::string name = this->backend.getPresetName();

  if (name.empty()) {
    this->savePresetAs();
    return;
  }

  this->report(this->backend.savePreset(name, PRESET_WRITE::OVERWRITE));
}

void EditorView::savePresetAs() {
  this->showPrompt("Preset name", this->backend.getPresetName(), [this](const std::string& name) {
    this->report(this->backend.savePreset(name, PRESET_WRITE::CREATE));
  });
}

void EditorView::report(const std::expected<void, std::string>& result) {
  if (!result) {
    this->showMessage(result.error());
  }
}

void EditorView::report(const CommandResult& result) {
  if (!result) {
    this->showMessage(result.error());
  }
}

void EditorView::showMessage(const std::string& text) {
  this->toast->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
  this->toast->SetClass("hidden", false);
  this->toastDeadline = std::chrono::steady_clock::now() + TOAST_TIME;
  this->isViewChanged = true;
}
