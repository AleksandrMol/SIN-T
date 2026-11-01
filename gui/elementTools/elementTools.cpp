#include "./elementTools.h"

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

Rml::Property makeLength(float value) {
  return Rml::Property(value, Rml::Unit::DP);
}

Rml::Element* addElement(Rml::Element* parent, const std::string& tag, const std::string& className, const std::string& text) {
  Rml::Element* element = parent->AppendChild(parent->GetOwnerDocument()->CreateElement(tag));

  if (!className.empty()) {
    element->SetAttribute("class", className);
  }

  if (!text.empty()) {
    element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
  }

  return element;
}

void clearChildren(Rml::Element* element) {
  while (Rml::Element* child = element->GetFirstChild()) {
    element->RemoveChild(child);
  }
}
