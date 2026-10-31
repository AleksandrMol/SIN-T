#pragma once

#include <expected>
#include <string>

#include "../moduleRegistry/moduleRegistry.h"
#include "../synthModel/synthModel.h"

/**
 * Пресет по умолчанию (ТЗ §9.1, D-020): генератор с цепочкой осциллятор → фильтр → amp.
 * Собирается командами, поэтому всегда соответствует правилам модели
 */
std::expected<SynthModel, std::string> createDefaultModel(const ModuleRegistry* registry);
