#include "./editorView.h"

#include <algorithm>
#include <cmath>

#include "../elementTools/elementTools.h"
#include "../graphLayout/graphLayout.h"
#include "../gridDecorator/gridDecorator.h"
#include "../knob/knob.h"
#include "../wireLayer/wireLayer.h"

namespace {
  constexpr float KNOB_DRAG_RANGE = 220.0f; // Столько точек мышью проходит вся шкала
  constexpr float FINE_FACTOR = 0.1f;
  constexpr float WHEEL_STEP = 0.02f;
  constexpr float PAN_WHEEL_STEP = 40.0f;
  constexpr float WIRE_TOLERANCE = 6.0f;
  constexpr float NEW_ROUTE_DEPTH = 0.5f;
  constexpr float FREE_POSITION_STEP = 28.0f;

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

  bool isModulator(const ModuleInstance& module) {
    return !module.descriptor->modOutputs.empty();
  }
}

EditorView::EditorView(Rml::ElementDocument* document, EditorBackend& backend, std::function<void()> requestKeyboard)
  : document(document), backend(backend), scene(document, backend), overlays(document, std::move(requestKeyboard)) {
  this->canvas = document->GetElementById("canvas");
  this->grid = document->GetElementById("grid");
  this->world = document->GetElementById("world");
  this->presetButton = document->GetElementById("preset-button");
  this->wireRemove = document->GetElementById("wire-remove");
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
  return this->overlays.getMessage();
}

const EditorScene& EditorView::getScene() const {
  return this->scene;
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

bool EditorView::update() {
  bool isChanged = std::exchange(this->isViewChanged, false);

  if (this->overlays.expireMessage()) {
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

  std::string structure = this->scene.makeStructureKey(this->graph);

  if (structure != this->seenStructure) {
    this->seenStructure = std::move(structure);
    this->rebuild();
  }

  this->scene.refresh(this->drag.kind == DRAG_KIND::BLOCK ? this->drag.element : nullptr);

  return true;
}

void EditorView::rebuild() {
  // Перестройка уничтожает элементы: начатое действие продолжать не с чем
  if (this->drag.kind != DRAG_KIND::NONE && this->drag.kind != DRAG_KIND::CANVAS) {
    if (this->drag.kind == DRAG_KIND::KNOB) {
      this->backend.endGesture(this->drag.knob.module, this->drag.knob.parameter);
    }

    this->drag = {};
    this->draft->clearDraft();
  }

  this->scene.rebuild(this->graph);
  this->restoreSelectedWire();
  this->setWorldOffset();
}

void EditorView::setWorldOffset() {
  const Rml::Vector2f pan = this->pans[this->graph];

  this->world->SetProperty(Rml::PropertyId::Left, makeLength(pan.x));
  this->world->SetProperty(Rml::PropertyId::Top, makeLength(pan.y));

  const Rml::Vector2f gridOffset = getGridOffset(pan);

  this->grid->SetProperty(Rml::PropertyId::Left, makeLength(gridOffset.x));
  this->grid->SetProperty(Rml::PropertyId::Top, makeLength(gridOffset.y));
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

  this->overlays.hideMenuOutside(target);

  const bool isCanvas = target == this->canvas || target == this->world;

  if (button == 0 && isCanvas) {
    const auto wire = this->wires->findWire(mouse, WIRE_TOLERANCE * this->getScale());

    this->selectWire(wire ? std::optional(this->scene.getWireRefs()[*wire]) : std::nullopt);
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
    if (const KnobView* view = this->scene.findKnobByCell(cell)) {
      this->promptKnobValue(*view);
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

  if (const KnobView* view = this->scene.findKnob(target)) {
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
  const Rml::Vector2f below = element->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(0.0f, element->GetBox().GetSize(Rml::BoxArea::Border).y + 2.0f);

  if (action == "toggle") {
    const ModuleInstance* instance = this->backend.getModel().findModule(module);

    if (instance) {
      this->report(this->backend.apply(SetModuleEnabled{ module, !instance->enabled }));
    }
  } else if (action == "remove") {
    this->report(this->backend.apply(RemoveModule{ module }));
  } else if (action == "step-parameter") {
    this->stepParameter(module, readText(element, "data-key"), element->GetAttribute<int>("data-step", 0));
  } else if (action == "open") {
    this->openGraph(module);
  } else if (action == "close-graph") {
    this->openGraph(std::nullopt);
  } else if (action == "remove-wire") {
    this->removeSelectedWire();
  } else if (action == "remove-route") {
    if (const RouteView* view = this->scene.findRoute(element)) {
      this->report(this->backend.apply(RemoveRoute{ view->key.first, view->key.second }));
    }
  } else if (action == "menu") {
    this->overlays.runMenuItem((size_t)element->GetAttribute<int>("data-item", 0));
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
    this->overlays.acceptPrompt();
  } else if (action == "prompt-cancel") {
    this->overlays.hidePrompt();
  }
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
    if (const KnobView* view = this->scene.findKnob(element)) {
      this->drag.kind = DRAG_KIND::KNOB;
      this->drag.knob = { view->module, view->parameter->key };
      this->drag.value = view->knob->getValue();
      this->backend.beginGesture(view->module, view->parameter->key);
    }
  } else if (kind == "wire") {
    this->startWireDrag(element);
  } else if (kind == "depth") {
    if (const RouteView* view = this->scene.findRoute(element)) {
      this->drag.kind = DRAG_KIND::DEPTH;
      this->drag.route = view->key;
      this->setDepthFromMouse(mouse.x);
    }
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

      this->scene.setBlockPosition(this->drag.element, { offset.x, offset.y });
      break;
    }

    case DRAG_KIND::KNOB:
      if (const KnobView* view = this->scene.findKnob(this->drag.knob)) {
        this->changeKnob(*view, -delta.y / scale / KNOB_DRAG_RANGE, event.GetParameter("shift_key", 0) != 0);
      }

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

    case DRAG_KIND::KNOB:
      this->backend.endGesture(this->drag.knob.module, this->drag.knob.parameter);
      break;

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

  if (const KnobView* view = this->scene.findKnob(target)) {
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
  const bool isPromptOpen = this->overlays.isPromptOpen();

  if (key == Rml::Input::KI_ESCAPE) {
    this->overlays.hideMenu();
    this->overlays.hidePrompt();
  } else if (key == Rml::Input::KI_RETURN && isPromptOpen) {
    this->overlays.acceptPrompt();
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
    for (const KnobView& view : this->scene.getKnobs()) {
      view.cell->SetClass("legal", model.checkRoute({ wire.module, wire.port }, { view.module, view.parameter->key }).has_value());
    }

    return;
  }

  for (Rml::Element* port : this->scene.getPorts()) {
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
  for (const KnobView& view : this->scene.getKnobs()) {
    view.cell->SetClass("legal", false);
  }

  for (Rml::Element* port : this->scene.getPorts()) {
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

      for (Rml::Element* candidate : this->scene.getPorts()) {
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

/**
 * Перестройка заново раздала проводам номера: выделение ищется по ссылке, а если провода больше нет, снимается
 */
void EditorView::restoreSelectedWire() {
  const auto& refs = this->scene.getWireRefs();
  const auto selected = this->selectedWire ? std::ranges::find(refs, *this->selectedWire) : refs.end();

  if (selected == refs.end()) {
    this->selectWire(std::nullopt);
  } else {
    this->wires->selectWire((size_t)(selected - refs.begin()));
  }
}

void EditorView::selectWire(std::optional<WireRef> wire) {
  this->selectedWire = std::move(wire);

  const auto& refs = this->scene.getWireRefs();
  const auto found = this->selectedWire ? std::ranges::find(refs, *this->selectedWire) : refs.end();

  if (found == refs.end()) {
    this->selectedWire.reset();
    this->wires->selectWire(std::nullopt);
    this->wireRemove->SetClass("hidden", true);
    return;
  }

  const size_t index = (size_t)(found - refs.begin());
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

void EditorView::changeKnob(const KnobView& view, float delta, bool isFine) {
  this->drag.value = std::clamp(this->drag.value + delta * (isFine ? FINE_FACTOR : 1.0f), 0.0f, 1.0f);
  this->setKnobValue(view, this->drag.value);
}

void EditorView::promptKnobValue(const KnobView& view) {
  const ModuleId module = view.module;
  const ParameterDescriptor* parameter = view.parameter;

  this->overlays.showPrompt(parameter->name, view.shownText, [this, module, parameter](const std::string& text) {
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

/**
 * Стрелка переключателя (D-108): соседняя ступень, после последней снова первая
 */
void EditorView::stepParameter(ModuleId module, const std::string& parameter, int step) {
  const KnobView* view = this->scene.findKnob(ModTarget{ module, parameter });

  if (!view) {
    return;
  }

  const ParameterDescriptor& descriptor = *view->parameter;
  const float real = this->backend.getModel().findModule(module)->parameters.at(parameter);
  const int count = (int)std::lround(descriptor.maxValue - descriptor.minValue) + 1;
  const int index = (int)std::lround(real - descriptor.minValue);
  const int next = ((index + step) % count + count) % count;

  this->backend.beginGesture(module, parameter);
  this->report(this->backend.apply(SetParameter{ module, parameter, descriptor.minValue + (float)next }));
  this->backend.endGesture(module, parameter);
}

void EditorView::setDepthFromMouse(float mouseX) {
  const auto& list = this->backend.getModel().getRoutes();
  const auto [source, target] = this->drag.route;

  // Маршрут могли удалить из хоста, а вид об этом ещё не знает
  const bool isAlive = std::ranges::any_of(list, [&source, &target](const ModRoute& route) {
    return route.source == source && route.target == target;
  });

  if (!isAlive) {
    return;
  }

  const float left = this->drag.element->GetAbsoluteOffset(Rml::BoxArea::Border).x;
  const float width = this->drag.element->GetBox().GetSize(Rml::BoxArea::Border).x;
  const float depth = std::clamp((mouseX - left) / width * 2.0f - 1.0f, -1.0f, 1.0f);

  this->report(this->backend.apply(SetRouteDepth{ source, target, depth }));
}

// //////////
// // Меню //
// //////////

void EditorView::showModuleMenu(Rml::Vector2f position, ModulePosition place) {
  MenuItems items;

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

  this->overlays.showMenu(position, std::move(items));
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
  MenuItems items;

  items.emplace_back("Init", [this] {
    this->report(this->backend.loadDefaultPreset());
  });

  for (const std::string& name : this->backend.listPresets()) {
    items.emplace_back(name, [this, name] {
      this->report(this->backend.loadPreset(name));
    });
  }

  const Rml::Vector2f offset = this->presetButton->GetAbsoluteOffset(Rml::BoxArea::Border);

  this->overlays.showMenu(offset + Rml::Vector2f(0.0f, this->presetButton->GetBox().GetSize(Rml::BoxArea::Border).y + 2.0f), std::move(items));
}

void EditorView::showRouteSourceMenu(Rml::Vector2f position) {
  const SynthModel& model = this->backend.getModel();
  MenuItems items;

  items.emplace_back("Source", nullptr);

  for (const auto& [id, module] : model.getModules()) {
    if (!isModulator(module) || module.isStub()) {
      continue;
    }

    for (const ModOutputDescriptor& output : module.descriptor->modOutputs) {
      const ModSource source = { id, output.key };

      items.emplace_back(this->scene.makeSourceLabel(source), [this, source, position] {
        this->showRouteTargetMenu(position, source);
      });
    }
  }

  this->overlays.showMenu(position, std::move(items));
}

void EditorView::showRouteTargetMenu(Rml::Vector2f position, ModSource source) {
  const SynthModel& model = this->backend.getModel();
  MenuItems items;

  items.emplace_back("Target of " + this->scene.makeSourceLabel(source), nullptr);

  for (const auto& [id, module] : model.getModules()) {
    if (module.isStub()) {
      continue;
    }

    for (const ParameterDescriptor& parameter : module.descriptor->parameters) {
      const ModTarget target = { id, parameter.key };

      if (model.checkRoute(source, target)) {
        items.emplace_back(this->scene.makeTargetLabel(target), [this, source, target] {
          this->report(this->backend.apply(AddRoute{ { source, target, NEW_ROUTE_DEPTH } }));
        });
      }
    }
  }

  this->overlays.showMenu(position, std::move(items));
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
  this->overlays.showPrompt("Preset name", this->backend.getPresetName(), [this](const std::string& name) {
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
  this->overlays.showMessage(text);
  this->isViewChanged = true;
}
