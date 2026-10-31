#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "../moduleRegistry/moduleRegistry.h"
#include "../synthModel/synthModel.h"

/**
 * Записывает модель в JSON (D-020). Значения параметров — в реальных единицах
 */
std::string saveModel(const SynthModel& model);

/**
 * Собирает новую модель из JSON. Файл проверяется целиком: при любой ошибке возвращается её текст,
 * а текущая модель остаётся нетронутой, потому что эта функция её не видит.
 * Модуль неизвестного типа или более новой версии становится заглушкой (D-021).
 * Вызывается только на главном потоке
 */
std::expected<SynthModel, std::string> loadModel(const ModuleRegistry* registry, std::string_view text);
