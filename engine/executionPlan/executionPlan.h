#pragma once

#include <map>
#include <memory>
#include <vector>

#include "../modelTypes/modelTypes.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "../stubProcessor/stubProcessor.h"
#include "../synthModel/synthModel.h"

/**
 * Состояние модуля в движке. Живёт отдельно от плана и переживает его смену (D-023)
 */
struct ModuleSlot {
  std::unique_ptr<ModuleProcessor> processor;
  std::vector<ParameterRamp> parameters;
};

using ModuleSlots = std::map<ModuleId, ModuleSlot>;

/**
 * Сумма нескольких проводов на одном входе (D-035)
 */
struct PlanMix {
  AudioBuffer* target;
  std::vector<const AudioBuffer*> sources;
};

/**
 * Один шаг плана. У выхода графа processor пуст: от него нужны только смешанные входы
 */
struct PlanStep {
  ModuleProcessor* processor;
  const ParameterRamp* parameters;
  std::vector<PlanMix> mixes;
  std::vector<const AudioBuffer*> inputs;
  std::vector<AudioBuffer*> outputs;
};

/**
 * План исполнения: порядок модулей и буферы соединений. Неизменяемый, собирается не в аудиопотоке (D-023)
 */
struct ExecutionPlan {
  std::vector<AudioBuffer> buffers; // Память всех буферов, на неё указывают шаги
  std::vector<PlanStep> steps;
  const AudioBuffer* output;
  StubProcessor bypass; // Заменяет отключённые модули. Состояния нет, поэтому один на все шаги (D-040)
};

/**
 * Приводит состояния модулей в соответствие с моделью: создаёт новые, удаляет исчезнувшие,
 * обновляет значения параметров. Состояния существующих модулей не трогает
 */
void syncModuleSlots(const SynthModel& model, ModuleSlots& slots, float sampleRate);

/**
 * Собирает план главного графа. Исполняются только модули, от которых сигнал доходит до выхода.
 * Отключённый модуль не исполняется и не меняет своё состояние, вместо него работает заглушка.
 * Состояния должны быть приведены в соответствие с моделью
 */
std::unique_ptr<ExecutionPlan> compilePlan(const SynthModel& model, ModuleSlots& slots);
