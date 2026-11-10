#include "./editorScene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <set>

#include "../../engine/oscillator/oscillatorProcessor.h"
#include "../../engine/slotTable/slotTable.h"
#include "../elementTools/elementTools.h"
#include "../graphLayout/graphLayout.h"
#include "../knob/knob.h"
#include "../valueField/valueField.h"
#include "../waveScreen/waveScreen.h"
#include "../wireLayer/wireLayer.h"

namespace {
  constexpr float PORT_FIRST_TOP = 34.0f;
  constexpr float PORT_STEP = 24.0f;
  constexpr float DISABLED_OPACITY = 0.45f;

  /**
   * Откуда идёт дуга значения: у параметра с нулём внутри диапазона это ноль, так видно знак
   */
  float findOrigin(const ParameterDescriptor& parameter) {
    return parameter.minValue < 0.0f && parameter.maxValue > 0.0f ? toNormalized(parameter, 0.0f) : 0.0f;
  }

  /**
   * Не зависит от локали. std::format здесь нельзя: ему нужна libstdc++ новее той, что есть в среде Reaper (D-019)
   */
  std::string formatDepth(float depth) {
    char text[16];
    const auto written = std::to_chars(text, text + sizeof(text), std::fabs(depth), std::chars_format::fixed, 2);

    return (depth < 0.0f ? "-" : "+") + std::string(text, written.ptr);
  }

  /**
   * Лежит ли блок модуля на показанном холсте. Только тогда к нему можно провести линию (D-063)
   */
  bool isOnGraph(const ModuleInstance& module, std::optional<ModuleId> graph) {
    return module.owner == graph && module.descriptor->role != MODULE_ROLE::MACROS;
  }
}

EditorScene::EditorScene(Rml::ElementDocument* document, EditorBackend& backend)
  : backend(backend) {
  this->world = document->GetElementById("world");
  this->breadcrumbs = document->GetElementById("breadcrumbs");
  this->sources = document->GetElementById("sources");
  this->macroCells = document->GetElementById("macro-cells");
  this->matrixRows = document->GetElementById("matrix-rows");
  this->presetButton = document->GetElementById("preset-button");
  this->wires = dynamic_cast<WireLayer*>(document->GetElementById("wires"));
}

const std::vector<KnobView>& EditorScene::getKnobs() const {
  return this->knobs;
}

const std::vector<RouteView>& EditorScene::getRoutes() const {
  return this->routes;
}

const std::map<ModuleId, Rml::Element*>& EditorScene::getBlocks() const {
  return this->blocks;
}

const std::vector<Rml::Element*>& EditorScene::getPorts() const {
  return this->ports;
}

const std::vector<WireRef>& EditorScene::getWireRefs() const {
  return this->wireRefs;
}

std::string EditorScene::makeStructureKey(std::optional<ModuleId> graph) const {
  const SynthModel& model = this->backend.getModel();
  std::string key = graph ? std::to_string(*graph) : "main";

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

void EditorScene::rebuild(std::optional<ModuleId> graph) {
  this->graph = graph;
  this->knobs.clear();
  this->screens.clear();
  this->routes.clear();
  this->blocks.clear();
  this->ports.clear();

  this->buildBreadcrumbs();
  this->buildSources();
  this->buildBlocks();
  this->buildMacros();
  this->buildMatrix();
  this->buildWires();
}

void EditorScene::refresh(const Rml::Element* heldBlock) {
  const SynthModel& model = this->backend.getModel();
  const std::string presetName = this->backend.getPresetName();

  for (KnobView& view : this->knobs) {
    this->refreshKnob(view);
  }

  for (RouteView& view : this->routes) {
    this->refreshRoute(view);
  }

  for (const ScreenView& view : this->screens) {
    this->refreshScreen(view);
  }

  for (const auto& [id, block] : this->blocks) {
    if (block == heldBlock) {
      continue;
    }

    const ModuleInstance* module = model.findModule(id);

    this->setBlockPosition(block, module->position.value_or(this->layout.at(id)));
  }

  if (this->presetButton->GetInnerRML().empty() || presetName != this->seenPresetName) {
    this->seenPresetName = presetName;
    this->presetButton->SetInnerRML(Rml::StringUtilities::EncodeRml(presetName.empty() ? "Init" : presetName));
  }
}

void EditorScene::refreshKnob(KnobView& view) {
  const SynthModel& model = this->backend.getModel();
  const float real = model.findModule(view.module)->parameters.at(view.parameter->key);
  const float base = toNormalized(*view.parameter, real);
  float low = base;
  float high = base;

  for (const ModRoute& route : model.getRoutes()) {
    if (route.target.module != view.module || route.target.parameter != view.parameter->key) {
      continue;
    }

    const ModOutputDescriptor* output = findByKey(model.findModule(route.source.module)->descriptor->modOutputs, route.source.output);
    const bool isBipolar = output && output->polarity == MOD_POLARITY::BIPOLAR;

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

  std::string text = view.isShort ? formatParameterShort(*view.parameter, real) : formatParameter(*view.parameter, real);

  if (text != view.shownText) {
    view.valueText->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
    view.shownText = std::move(text);
  }
}

/**
 * Экран рисует сдвиг фазы в долях периода, поэтому значение переводится в 0..1 по диапазону параметра
 */
void EditorScene::refreshScreen(const ScreenView& view) {
  const ParameterValues& values = this->backend.getModel().findModule(view.module)->parameters;

  view.screen->setShape((WAVE_TYPE)std::lround(values.at(view.wave->key)), toNormalized(*view.phase, values.at(view.phase->key)));
}

void EditorScene::refreshRoute(RouteView& view) {
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

void EditorScene::buildBreadcrumbs() {
  clearChildren(this->breadcrumbs);

  Rml::Element* main = addElement(this->breadcrumbs, "div", this->graph ? "crumb" : "crumb current", "Main");

  if (!this->graph) {
    return;
  }

  main->SetAttribute("data-action", "close-graph");

  const SynthModel& model = this->backend.getModel();

  addElement(this->breadcrumbs, "div", "crumb-separator", "›");
  addElement(this->breadcrumbs, "div", "crumb current", makeModuleLabel(model, *model.findModule(*this->graph)));
}

/**
 * Выходы модуляции самого генератора видны внутри него: velocity нужна как раз голосу (D-052)
 */
void EditorScene::buildSources() {
  clearChildren(this->sources);

  if (!this->graph) {
    return;
  }

  const ModuleInstance* generator = this->backend.getModel().findModule(*this->graph);

  for (const ModOutputDescriptor& output : generator->descriptor->modOutputs) {
    Rml::Element* source = addElement(this->sources, "div", "source", output.name);

    this->addPort(source, generator->id, output.key, "port modulation");
  }
}

void EditorScene::buildBlocks() {
  clearChildren(this->world);

  const SynthModel& model = this->backend.getModel();

  this->layout = computeLayout(model, this->graph);

  for (const auto& [id, module] : model.getModules()) {
    if (isOnGraph(module, this->graph)) {
      this->buildBlock(module, module.position.value_or(this->layout.at(id)));
    }
  }
}

void EditorScene::buildBlock(const ModuleInstance& module, ModulePosition position) {
  const SynthModel& model = this->backend.getModel();
  const ModuleDescriptor& descriptor = *module.descriptor;
  const bool isOutput = descriptor.role == MODULE_ROLE::OUTPUT;
  const bool isGenerator = descriptor.role == MODULE_ROLE::GENERATOR;
  const bool isSmall = isSmallBlock(module);
  std::string className = "block";

  className += isSmall ? " small" : "";
  className += module.enabled ? "" : " disabled";
  className += module.isStub() ? " stub" : "";

  Rml::Element* block = addElement(this->world, "div", className);

  block->SetAttribute("data-drag", "block");
  block->SetAttribute("data-module", (int)module.id);
  this->blocks[module.id] = block;
  this->setBlockPosition(block, position);

  // Дробное число в тексте стилей RmlUi читает по локали процесса, а её мог поменять хост: 0.45 стало бы нулём
  if (!module.enabled) {
    block->SetProperty(Rml::PropertyId::Opacity, Rml::Property(DISABLED_OPACITY, Rml::Unit::NUMBER));
  }

  Rml::Element* header = addElement(block, "div", "block-header");

  addElement(header, "div", "block-title", isOutput ? descriptor.name : makeModuleLabel(model, module));

  if (!isOutput) {
    if (!module.isStub()) {
      Rml::Element* power = addElement(header, "div", "block-button");

      power->SetAttribute("data-action", "toggle");
      power->SetAttribute("data-module", (int)module.id);
      addElement(power, "div", "power-dot");
    }

    Rml::Element* remove = addElement(header, "div", "block-button", "×");

    remove->SetAttribute("data-action", "remove");
    remove->SetAttribute("data-module", (int)module.id);
  }

  Rml::Element* body = addElement(block, "div", "block-body");

  if (module.isStub()) {
    addElement(block, "div", "block-note", "Unknown module");
  } else if (descriptor.type == OSCILLATOR_DESCRIPTOR.type) {
    this->buildOscillatorFace(body, module);
  } else {
    for (const ParameterDescriptor& parameter : descriptor.parameters) {
      this->buildKnob(body, module, parameter);
    }
  }

  if (isGenerator) {
    Rml::Element* open = addElement(block, "div", "button open-button", "Open voice ›");

    open->SetAttribute("data-action", "open");
    open->SetAttribute("data-module", (int)module.id);
  }

  this->buildPorts(block, module);
}

void EditorScene::buildKnob(Rml::Element* parent, const ModuleInstance& module, const ParameterDescriptor& parameter) {
  Rml::Element* cell = addElement(parent, "div", "knob-cell");

  cell->SetAttribute("data-module", (int)module.id);
  cell->SetAttribute("data-parameter", parameter.key);

  auto* knob = dynamic_cast<Knob*>(addElement(cell, "knob", ""));

  knob->SetAttribute("data-drag", "knob");
  knob->setOrigin(findOrigin(parameter));

  addElement(cell, "div", "knob-name", parameter.shortName.empty() ? parameter.name : parameter.shortName);

  Rml::Element* value = addElement(cell, "div", "knob-value");

  this->addParameterMarks(cell, module, parameter);
  this->knobs.push_back({ knob, cell, value, module.id, &parameter, "" });
}

/**
 * Поле вместо ручки (D-108): само поле и ячейка, и то, что тянут мышью. Подпись и число не ловят мышь,
 * поэтому двойной щелчок, колесо и провод модуляции попадают в поле, где бы ни был указатель
 */
void EditorScene::buildField(Rml::Element* parent, const ModuleInstance& module, const ParameterDescriptor& parameter, const std::string& icon, const std::string& label, const std::string& className) {
  auto* field = dynamic_cast<ValueField*>(addElement(parent, "value-field", className));

  field->SetAttribute("data-module", (int)module.id);
  field->SetAttribute("data-parameter", parameter.key);
  field->SetAttribute("data-drag", "knob");
  field->setOrigin(findOrigin(parameter));

  if (!icon.empty()) {
    addElement(field, "div", "field-icon", icon);
  }

  if (!label.empty()) {
    addElement(field, "div", "field-name", label);
  }

  Rml::Element* value = addElement(field, "div", "field-value");

  this->addParameterMarks(field, module, parameter);
  this->knobs.push_back({ field, field, value, module.id, &parameter, "", true });
}

void EditorScene::addParameterMarks(Rml::Element* cell, const ModuleInstance& module, const ParameterDescriptor& parameter) {
  if (this->hasOffCanvasRoute(module, parameter)) {
    addElement(cell, "div", "knob-mark");
  }

  // Слотов не хватило: параметр работает, но хост его не видит (D-011)
  if (!this->backend.isExported(module.id, parameter.key)) {
    addElement(cell, "div", "knob-mark unexported");
  }
}

/**
 * Стрелка, которая переключает ступенчатый параметр на соседнюю ступень, с последней на первую
 */
void EditorScene::addStepButton(Rml::Element* parent, ModuleId module, const std::string& parameter, int step, const std::string& text) {
  Rml::Element* button = addElement(parent, "div", "step-button", text);

  button->SetAttribute("data-action", "step-parameter");
  button->SetAttribute("data-module", (int)module);
  button->SetAttribute("data-key", parameter);
  button->SetAttribute("data-step", step);
}

/**
 * Лицевая сторона осциллятора, как на референсе (D-108): волна переключается стрелками, на экране поля высоты тона
 * (октава, полутоны, центы, плавный сдвиг) сверху и фазы снизу, под экраном ряд ручек, панорама и громкость отделены чертой.
 * Параметр, которому здесь не нашлось места, всё равно получает ручку в ряду ниже: так он не пропадёт из редактора
 */
void EditorScene::buildOscillatorFace(Rml::Element* body, const ModuleInstance& module) {
  std::set<std::string> placed;

  const auto addField = [&](Rml::Element* parent, const std::string& key, const std::string& icon, const std::string& label, const std::string& className) {
    if (const ParameterDescriptor* parameter = findByKey(module.descriptor->parameters, key)) {
      this->buildField(parent, module, *parameter, icon, label, className);
      placed.insert(key);
    }
  };

  const auto addKnob = [&](Rml::Element* parent, const std::string& key) {
    if (const ParameterDescriptor* parameter = findByKey(module.descriptor->parameters, key)) {
      this->buildKnob(parent, module, *parameter);
      placed.insert(key);
    }
  };

  body->SetClass("osc-face", true);

  Rml::Element* select = addElement(body, "div", "wave-select");

  this->addStepButton(select, module.id, "wave", -1, "‹");
  addField(select, "wave", "", "", "field wave-name");
  this->addStepButton(select, module.id, "wave", 1, "›");

  Rml::Element* screen = addElement(body, "div", "osc-screen");
  Rml::Element* pitch = addElement(screen, "div", "field-row pitch-row");

  addField(pitch, "octave", "", "OCT", "field pitch-octave");
  addField(pitch, "semi", "", "SEM", "field pitch-semi");
  addField(pitch, "fine", "", "FIN", "field pitch-fine");
  addField(pitch, "coarse", "", "CRS", "field pitch-coarse");

  const ParameterDescriptor* wave = findByKey(module.descriptor->parameters, "wave");
  const ParameterDescriptor* phaseShift = findByKey(module.descriptor->parameters, "phase");

  if (wave && phaseShift) {
    this->screens.push_back({ dynamic_cast<WaveScreen*>(addElement(screen, "wave-screen", "wave-screen")), module.id, wave, phaseShift });
  }

  // Знак фазы крупнее подписи, как на референсе
  Rml::Element* phase = addElement(screen, "div", "field-row");

  addField(phase, "phase", "φ", "", "field");
  addField(phase, "rand", "φ", "RAND", "field");

  Rml::Element* knobs = addElement(body, "div", "knob-row");

  addKnob(knobs, "unison");
  addKnob(knobs, "detune");
  addKnob(knobs, "width");
  addElement(knobs, "div", "knob-divider");
  addKnob(knobs, "pan");
  addKnob(knobs, "level");

  Rml::Element* rest = nullptr;

  for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
    if (!placed.contains(parameter.key)) {
      rest = rest ? rest : addElement(body, "div", "knob-row");
      this->buildKnob(rest, module, parameter);
    }
  }
}

/**
 * Есть ли маршрут на параметр, который нельзя нарисовать линией: его источник на другом уровне (D-063)
 */
bool EditorScene::hasOffCanvasRoute(const ModuleInstance& target, const ParameterDescriptor& parameter) const {
  const SynthModel& model = this->backend.getModel();

  return std::ranges::any_of(model.getRoutes(), [&](const ModRoute& route) {
    return route.target.module == target.id
      && route.target.parameter == parameter.key
      && !isOnGraph(*model.findModule(route.source.module), this->graph);
  });
}

Rml::Element* EditorScene::addPort(Rml::Element* parent, ModuleId module, const std::string& port, const std::string& className) {
  Rml::Element* element = addElement(parent, "div", className);

  element->SetAttribute("data-module", (int)module);
  element->SetAttribute("data-port", port);
  element->SetAttribute("data-drag", "wire");
  this->ports.push_back(element);

  return element;
}

void EditorScene::buildPorts(Rml::Element* block, const ModuleInstance& module) {
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
      placeRow(addElement(block, "div", "port-label", output.name), row);
    }
  }
}

Rml::Element* EditorScene::findPort(ModuleId module, const std::string& port, bool isModulation, bool isInput) const {
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

const KnobView* EditorScene::findKnob(const ModTarget& target) const {
  const auto view = std::ranges::find_if(this->knobs, [&target](const KnobView& candidate) {
    return candidate.module == target.module && candidate.parameter->key == target.parameter;
  });

  return view != this->knobs.end() ? &*view : nullptr;
}

void EditorScene::buildWires() {
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
    const KnobView* to = this->findKnob(route.target);

    // Источник из панели или с другого уровня на холсте не лежит: такой маршрут виден как метка на ручке (D-063)
    if (from && to && isOnGraph(*model.findModule(route.source.module), this->graph)) {
      list.push_back({ from, to->knob, WIRE_KIND::MODULATION });
      this->wireRefs.emplace_back(RouteKey{ route.source, route.target });
    }
  }

  this->wires->setWires(std::move(list));
}

void EditorScene::buildMacros() {
  clearChildren(this->macroCells);

  const ModuleInstance& macros = *this->backend.getModel().findModule(MACROS_MODULE_ID);

  for (size_t index = 0; index < macros.descriptor->parameters.size(); index++) {
    this->buildKnob(this->macroCells, macros, macros.descriptor->parameters[index]);

    const ModOutputDescriptor& output = macros.descriptor->modOutputs[index];

    this->addPort(this->knobs.back().cell, MACROS_MODULE_ID, output.key, "port modulation");
  }
}

std::string EditorScene::makeSourceLabel(const ModSource& source) const {
  const SynthModel& model = this->backend.getModel();
  const ModuleInstance& module = *model.findModule(source.module);
  const ModOutputDescriptor* output = findByKey(module.descriptor->modOutputs, source.output);
  const std::string outputName = output ? output->name : source.output;

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

std::string EditorScene::makeTargetLabel(const ModTarget& target) const {
  const SynthModel& model = this->backend.getModel();
  const ModuleInstance& module = *model.findModule(target.module);

  const ParameterDescriptor* parameter = findByKey(module.descriptor->parameters, target.parameter);

  return parameter ? makeParameterName(model, module, *parameter) : target.parameter;
}

void EditorScene::buildMatrix() {
  clearChildren(this->matrixRows);

  const auto& list = this->backend.getModel().getRoutes();

  if (list.empty()) {
    addElement(this->matrixRows, "div", "matrix-empty", "No routes. Drag an orange output onto a knob.");
    return;
  }

  for (const ModRoute& route : list) {
    // Источник, цель с глубиной и значение в своих ячейках, как на референсе D-093. Кнопка удаления — действие, а не данные, она вне ячеек
    Rml::Element* row = addElement(this->matrixRows, "div", "route");
    Rml::Element* source = addElement(row, "div", "route-cell route-source");

    addElement(source, "div", "route-name", this->makeSourceLabel(route.source));
    addElement(source, "div", "route-arrow", "›");

    Rml::Element* target = addElement(row, "div", "route-cell route-target");

    addElement(target, "div", "route-name", this->makeTargetLabel(route.target));

    Rml::Element* slider = addElement(target, "div", "route-depth");

    slider->SetAttribute("data-drag", "depth");

    Rml::Element* fill = addElement(slider, "div", "route-fill");

    addElement(slider, "div", "route-center");

    Rml::Element* value = addElement(row, "div", "route-cell route-value");
    Rml::Element* remove = addElement(row, "div", "block-button", "×");

    remove->SetAttribute("data-action", "remove-route");

    this->routes.push_back({ row, fill, value, { route.source, route.target } });
  }
}

void EditorScene::setBlockPosition(Rml::Element* block, ModulePosition position) {
  block->SetProperty(Rml::PropertyId::Left, makeLength(position.x));
  block->SetProperty(Rml::PropertyId::Top, makeLength(position.y));
}

const KnobView* EditorScene::findKnob(const Rml::Element* element) const {
  const auto view = std::ranges::find_if(this->knobs, [element](const KnobView& candidate) {
    return candidate.knob == element;
  });

  return view != this->knobs.end() ? &*view : nullptr;
}

const KnobView* EditorScene::findKnobByCell(const Rml::Element* cell) const {
  const auto view = std::ranges::find_if(this->knobs, [cell](const KnobView& candidate) {
    return candidate.cell == cell;
  });

  return view != this->knobs.end() ? &*view : nullptr;
}

const RouteView* EditorScene::findRoute(Rml::Element* element) const {
  const auto view = std::ranges::find_if(this->routes, [element](const RouteView& candidate) {
    return candidate.row->Contains(element);
  });

  return view != this->routes.end() ? &*view : nullptr;
}
