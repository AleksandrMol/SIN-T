#pragma once

#include <map>
#include <memory>
#include <span>
#include <vector>

#include "../modelTypes/modelTypes.h"
#include "../modulationPlan/modulationPlan.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "../stubProcessor/stubProcessor.h"
#include "../synthModel/synthModel.h"

class GeneratorProcessor;
class PlanExchange;

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
 * Плавное изменение на время перехода между планами (D-023). Значения шага сетки считаются
 * один раз на всех, поэтому голоса одного подграфа слышат одну и ту же громкость провода
 */
struct PlanFade {
  ParameterRamp ramp;
  float from;
  float to;
};

/**
 * Откуда и куда меняются элементы во время перехода (D-053)
 */
struct FadeRange {
  float from;
  float to;
};

struct TransitionFades {
  std::map<Connection, FadeRange> wires; // Громкость провода
  std::map<ModuleId, FadeRange> modules; // Доля обработанного сигнала: 0 — модуль отключён
  std::map<RouteKey, FadeRange> routes; // Глубина маршрута

  bool isEmpty() const {
    return this->wires.empty() && this->modules.empty() && this->routes.empty();
  }
};

/**
 * Один провод входа. Громкость есть только у провода в переходе
 */
struct PlanMixSource {
  const AudioBuffer* buffer;
  const ParameterRamp* gain;
};

/**
 * Сумма нескольких проводов на одном входе (D-035)
 */
struct PlanMix {
  AudioBuffer* target;
  std::vector<PlanMixSource> sources;
};

/**
 * Один шаг плана: соединения модуля. Общие для всех голосов графа
 */
struct PlanStep {
  std::vector<PlanMix> mixes;
  std::vector<const AudioBuffer*> inputs;
  std::vector<AudioBuffer*> outputs;
  const GraphPlan* subgraph; // Подграф, если шаг исполняет генератор
  const ParameterRamp* wet; // Доля сигнала модуля, если его отключают или включают
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
  std::vector<std::vector<ModuleProcessor*>> processors; // [голос]: все состояния, для сброса
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
  uint32_t polyphony;
};

/**
 * План исполнения. Собирается не в аудиопотоке и после публикации не меняется, кроме значений
 * переходов: их ведёт аудиопоток (D-023)
 */
struct ExecutionPlan {
  GraphPlan main;
  std::vector<std::unique_ptr<GraphPlan>> subgraphs;
  std::vector<PlanGenerator> generators;
  std::vector<ModuleProcessor*> sharedModulators; // Глобальные экземпляры общих модуляторов (D-005)
  std::map<ModuleId, std::span<BaseValue>> bases; // Адреса базовых значений всех модулей

  std::map<Connection, PlanFade> wireFades;
  std::map<ModuleId, PlanFade> moduleFades;
  std::map<RouteKey, PlanFade> routeFades;
  PlanFade outputFade = { { 0.0f, 0.0f }, 0.0f, 1.0f }; // Появление плана, заменившего всё состояние
  uint32_t fadeChunks = 0; // Длительность перехода в шагах сетки
  uint32_t fadeChunksLeft = 0; // Сколько шагов осталось, 0 — переход окончен. Ведёт аудиопоток

  uint64_t generation = 0;

  // Состояние целиком новое и не пересекается с прошлым планом: так меняется пресет (D-054)
  bool isReplacement = false;
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
  uint32_t gridOffset; // Положение начала куска на сетке: по нему считаются значения переходов
};

/**
 * Как применять изменения состояний
 */
enum class SLOT_VALUES {
  ASSIGN, // Звука нет: значения принимаются сразу
  KEEP, // Звук идёт: значения существующих модулей принадлежат аудиопотоку, а новые модули начинают с чистого листа
};

/**
 * Приводит состояния модулей в соответствие с моделью: создаёт новые, убирает исчезнувшие.
 * Состояния существующих модулей не трогает. Исчезнувшее передаётся в exchange, если на него
 * могут ссылаться планы, иначе удаляется сразу. Состояния подграфа создаются на каждый
 * слот голоса генератора, туда же входят общие модуляторы, которые этот генератор использует (D-005)
 */
void syncModuleSlots(
  const SynthModel& model,
  ModuleSlots& slots,
  float sampleRate,
  PlanExchange* exchange = nullptr,
  SLOT_VALUES values = SLOT_VALUES::ASSIGN
);

/**
 * Откуда и куда должны измениться провода, отключение модулей и глубины маршрутов (D-053)
 */
TransitionFades findFades(const SynthModel& previous, const SynthModel& next);

/**
 * Собирает план главного графа и подграфов. Исполняются только модули, от которых сигнал доходит до выхода.
 * Отключённый модуль не исполняется и не меняет своё состояние, вместо него работает заглушка.
 * Состояния должны быть приведены в соответствие с моделью
 */
std::unique_ptr<ExecutionPlan> compilePlan(
  const SynthModel& model,
  ModuleSlots& slots,
  ParameterBases& bases,
  const TransitionFades& fades = {}
);

/**
 * Исполняет граф для одного голоса. Вызывается из аудиопотока
 */
void runGraph(const GraphPlan& graph, uint32_t voice, const ChunkSpan& span, const VoiceContext& context);
