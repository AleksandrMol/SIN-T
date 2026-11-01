#pragma once

#include <RmlUi/Core.h>

#include <string>

#include "../../engine/modelTypes/modelTypes.h"

/**
 * Ближайший предок с атрибутом, начиная с самого элемента. Как closest() в DOM
 */
Rml::Element* findClosest(Rml::Element* element, const char* attribute);

std::string readText(Rml::Element* element, const char* attribute);
ModuleId readModule(Rml::Element* element);

/**
 * Длина числом: не нужно собирать текст, который RmlUi тут же разбирал бы обратно
 */
Rml::Property makeLength(float value);

Rml::Element* addElement(Rml::Element* parent, const std::string& tag, const std::string& className, const std::string& text = "");
void clearChildren(Rml::Element* element);
