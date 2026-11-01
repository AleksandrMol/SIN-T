#pragma once

#include <cstddef>
#include <string>

#include "../../plugin/plugin.h"

/**
 * Что получилось: всё лежит в подграфе одного генератора, то есть на одном холсте
 */
struct LargePatch {
  ModuleId generator = 0;
  ModuleId lastBlock = 0; // Его включение и выключение меняет структуру
  ModuleId valueModule = 0; // Его параметр меняется без перестройки
  std::string valueParameter;
  size_t blockCount = 0;
  size_t connectionCount = 0;
  size_t routeCount = 0;
};

/**
 * Дописывает к пресету по умолчанию цепочки блоков и маршруты, пока их не станет blocks и routes.
 * Схема всегда одна и та же: замеры до и после правки сравниваются честно
 */
LargePatch buildLargePatch(MyPluginInstance* instance, size_t blocks, size_t routes);
