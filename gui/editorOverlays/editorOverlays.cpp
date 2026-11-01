#include "./editorOverlays.h"

#include <algorithm>

#include "../elementTools/elementTools.h"

namespace {
  constexpr auto TOAST_TIME = std::chrono::seconds(5);
}

EditorOverlays::EditorOverlays(Rml::ElementDocument* document, std::function<void()> requestKeyboard)
  : document(document), requestKeyboard(std::move(requestKeyboard)) {
  this->popup = document->GetElementById("popup");
  this->prompt = document->GetElementById("prompt");
  this->promptLabel = document->GetElementById("prompt-label");
  this->promptInput = document->GetElementById("prompt-input");
  this->toast = document->GetElementById("toast");
}

void EditorOverlays::showMenu(Rml::Vector2f position, MenuItems items) {
  clearChildren(this->popup);
  this->menuActions.clear();

  for (auto& [label, action] : items) {
    if (!action) {
      addElement(this->popup, "div", "menu-heading", label);
      continue;
    }

    Rml::Element* item = addElement(this->popup, "div", "menu-item", label);

    item->SetAttribute("data-action", "menu");
    item->SetAttribute("data-item", (int)this->menuActions.size());
    this->menuActions.push_back(std::move(action));
  }

  // Меню не должно уходить за край окна: в нём может быть длинный список параметров
  const float scale = this->document->GetContext()->GetDensityIndependentPixelRatio();
  const Rml::Vector2f window = this->document->GetBox().GetSize(Rml::BoxArea::Border) / scale;
  const Rml::Vector2f place = position / scale;

  this->popup->SetProperty(Rml::PropertyId::Left, makeLength(std::min(place.x, window.x - 220.0f)));
  this->popup->SetProperty(Rml::PropertyId::Top, makeLength(std::min(place.y, std::max(window.y - 330.0f, 0.0f))));
  this->popup->SetClass("hidden", false);
}

void EditorOverlays::hideMenu() {
  this->popup->SetClass("hidden", true);
}

void EditorOverlays::hideMenuOutside(Rml::Element* target) {
  if (!this->popup->IsClassSet("hidden") && !this->popup->Contains(target)) {
    this->hideMenu();
  }
}

void EditorOverlays::runMenuItem(size_t index) {
  // Действие может открыть следующее меню, поэтому список забирается заранее
  std::function<void()> run = this->menuActions.at(index);

  this->hideMenu();
  run();
}

void EditorOverlays::showPrompt(const std::string& label, const std::string& value, std::function<void(const std::string&)> action) {
  this->promptAction = std::move(action);
  this->promptLabel->SetInnerRML(Rml::StringUtilities::EncodeRml(label));
  this->promptInput->SetAttribute("value", value);
  this->prompt->SetClass("hidden", false);
  this->promptInput->Focus();

  // Текст печатается в плагин, поэтому только сейчас клавиатуру нужно забрать у DAW
  this->requestKeyboard();
}

void EditorOverlays::acceptPrompt() {
  const std::string value = readText(this->promptInput, "value");
  std::function<void(const std::string&)> action = std::exchange(this->promptAction, nullptr);

  this->hidePrompt();

  if (action) {
    action(value);
  }
}

void EditorOverlays::hidePrompt() {
  this->promptInput->Blur();
  this->prompt->SetClass("hidden", true);
}

bool EditorOverlays::isPromptOpen() const {
  return !this->prompt->IsClassSet("hidden");
}

void EditorOverlays::showMessage(const std::string& text) {
  this->toast->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
  this->toast->SetClass("hidden", false);
  this->toastDeadline = std::chrono::steady_clock::now() + TOAST_TIME;
}

std::string EditorOverlays::getMessage() const {
  return this->toastDeadline ? std::string(this->toast->GetInnerRML()) : "";
}

bool EditorOverlays::expireMessage() {
  if (!this->toastDeadline || std::chrono::steady_clock::now() < *this->toastDeadline) {
    return false;
  }

  this->toastDeadline.reset();
  this->toast->SetClass("hidden", true);

  return true;
}
