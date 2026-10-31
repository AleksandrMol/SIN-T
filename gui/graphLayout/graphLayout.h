#pragma once

#include <map>
#include <optional>

#include "../../engine/synthModel/synthModel.h"

/**
 * Авторасстановка блоков одного графа (D-068): старые пресеты положений не содержат (D-065).
 * Звук идёт слева направо, колонка — длина самого длинного пути от источника, выход стоит последним.
 * Модуляторы без аудиопортов идут отдельным рядом снизу. Результат зависит только от модели.
 * graph — генератор, подграф которого расставляется. Пусто — главный граф
 */
std::map<ModuleId, ModulePosition> computeLayout(const SynthModel& model, std::optional<ModuleId> graph);
