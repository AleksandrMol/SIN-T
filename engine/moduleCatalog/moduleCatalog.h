#pragma once

#include <expected>
#include <string>

#include "../moduleRegistry/moduleRegistry.h"

/**
 * Добавляет в реестр все модули плагина. Новый модуль нужно внести только сюда (ТЗ §3.2)
 */
std::expected<void, std::string> addAllModules(ModuleRegistry& registry);
