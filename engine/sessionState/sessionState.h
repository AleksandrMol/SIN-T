#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "../moduleRegistry/moduleRegistry.h"
#include "../slotTable/slotTable.h"
#include "../synthModel/synthModel.h"

/**
 * Что восстанавливается из проекта DAW: модель и данные, не влияющие на звук (D-020)
 */
struct SessionState {
  SynthModel model;
  SlotTable slots;
  std::string presetName;
};

/**
 * Состояние проекта: тот же JSON, что и пресет, плюс раздел session. Поэтому пресет
 * можно загрузить как состояние, а состояние открывается как пресет
 */
std::string saveSession(const SynthModel& model, const SlotTable& slots, const std::string& presetName);

/**
 * Собирает состояние проекта. В файле без раздела session (пресет, проект этапов 1–5) слоты выдаются заново.
 * При любой ошибке возвращается её текст. Таблица уже приведена в соответствие с моделью.
 * Вызывается только на главном потоке
 */
std::expected<SessionState, std::string> loadSession(const ModuleRegistry* registry, std::string_view text);
