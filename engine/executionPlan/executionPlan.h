#pragma once

#include <map>
#include <memory>
#include <vector>

#include "../modelTypes/modelTypes.h"
#include "../modulationPlan/modulationPlan.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "../stubProcessor/stubProcessor.h"
#include "../synthModel/synthModel.h"

class GeneratorProcessor;

/**
 * Состояние модуля в движке. Живёт отдельно от плана и переживает его смену (D-023)
 */
struct ModuleSlot {
  const ModuleDescriptor* descriptor = nullptr; // В другом пресете тот же ID может быть у модуля другого типа
  std::unique_ptr<ModuleProcessor> processor;
  std::vector<ParameterRamp> parameters;
  std::vector<float> modOutputs; // Значения выходов модуляции
  GeneratorProcessor* generator = nullptr; // Тот же processor, если модуль — генератор
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
 * Один шаг плана: соединения модуля. Общие для всех голосов графа
 */
struct PlanStep {
  std::vector<PlanMix> mixes;
  std::vector<const AudioBuffer*> inputs;
  std::vector<AudioBuffer*> outputs;
  const GraphPlan* subgraph; // Подграф, если шаг исполняет генератор
};

/**
 * Что исполняет шаг в конкретном голосе. У выхода графа processor пуст: от него нужны только смешанные входы
 */
struct StepBinding {
  ModuleProcessor* processor;
  const ParameterRamp* parameters;
};

/**
 * План одного графа. Главный граф и подграф устроены одинаково (D-004): у главного один голос.
 * Голоса считаются по очереди, поэтому буферы у них общие, а свои только состояния модулей
 */
struct GraphPlan {
  std::vector<AudioBuffer> buffers; // Память всех буферов, на неё указывают шаги
  std::vector<PlanStep> steps;
  std::vector<ModuleId> modules; // ID модулей шагов
  std::vector<std::vector<StepBinding>> voices; // [голос][шаг]
  std::vector<VoiceModulation> modulation; // [голос]
  const AudioBuffer* output;
  StubProcessor bypass; // Заменяет отключённые модули. Состояния нет, поэтому один на все шаги (D-040)
};

/**
 * Генератор, который получает ноты: включённый и дошедший до выхода
 */
struct PlanGenerator {
  ModuleId id;
  GeneratorProcessor* processor;
  GraphPlan* subgraph;
  const ParameterRamp* parameters;
};

/**
 * План исполнения. Неизменяемый, собирается не в аудиопотоке (D-023)
 */
struct ExecutionPlan {
  GraphPlan main;
  std::vector<std::unique_ptr<GraphPlan>> subgraphs;
  std::vector<PlanGenerator> generators;
  std::vector<ModuleProcessor*> sharedModulators; // Глобальные экземпляры общих модуляторов (D-005)
};

/**
 * Общие значения для модуляции голоса
 */
struct ModulationClock {
  float tempo;
  float pitchBend;
};

/**
 * Какую часть участка значений параметров исполняет кусок
 */
struct ChunkSpan {
  uint32_t frames;
  uint32_t rampOffset;
  uint32_t rampFrames;
};

/**
 * Приводит состояния модулей в соответствие с моделью: создаёт новые, удаляет исчезнувшие,
 * обновляет значения параметров. Состояния существующих модулей не трогает.
 * Состояния подграфа создаются на каждый слот голоса генератора, туда же входят
 * общие модуляторы, которые этот генератор использует (D-005)
 */
void syncModuleSlots(const SynthModel& model, ModuleSlots& slots, float sampleRate);

/**
 * Собирает план главного графа и подграфов. Исполняются только модули, от которых сигнал доходит до выхода.
 * Отключённый модуль не исполняется и не меняет своё состояние, вместо него работает заглушка.
 * Состояния должны быть приведены в соответствие с моделью
 */
std::unique_ptr<ExecutionPlan> compilePlan(const SynthModel& model, ModuleSlots& slots, ParameterBases& bases);

/**
 * Исполняет граф для одного голоса. Вызывается из аудиопотока
 */
void runGraph(const GraphPlan& graph, uint32_t voice, const ChunkSpan& span, const VoiceContext& context);
