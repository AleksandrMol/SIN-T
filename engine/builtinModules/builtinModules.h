#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleRegistry/moduleRegistry.h"

/**
 * Модули, на которых держится сама структура графа. Описания не лежат рядом с DSP (D-033):
 * звук этих модулей реализуется в движке, а модель должна знать их и без него
 */
extern const ModuleDescriptor OUTPUT_DESCRIPTOR;
extern const ModuleDescriptor VOICE_OUTPUT_DESCRIPTOR;
extern const ModuleDescriptor GENERATOR_DESCRIPTOR;

/**
 * Добавляет встроенные модули в реестр
 */
std::expected<void, std::string> addBuiltinModules(ModuleRegistry& registry);
