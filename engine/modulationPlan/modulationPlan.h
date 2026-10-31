#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <vector>

#include "../modelTypes/modelTypes.h"
#include "../moduleProcessor/moduleProcessor.h"

/**
 * Базовое значение параметра в нормированном виде 0..1 (D-025). Одно на модуль, даже если у
 * него много голосов: иначе сглаживание пришлось бы вести в каждом голосе отдельно
 */
struct BaseValue {
  const ParameterDescriptor* descriptor = nullptr;
  float value = 0.0f;
  float previous = 0.0f; // Значение на прошлом участке: по нему видно, что база ещё меняется
  float target = 0.0f;
  float step = 0.0f; // Изменение за участок при сглаживании

  // Новое значение от главного потока. Только последнее и нужно, поэтому очередь не требуется:
  // ни одно изменение не потеряется и не задержит поток (D-053)
  std::atomic<float> requested = 0.0f;
};

using ParameterBases = std::map<ModuleId, std::vector<BaseValue>>;

/**
 * Один вклад в параметр: глубина × значение источника (D-025)
 */
struct RouteBinding {
  const float* source;
  float depth;
  const float* fadedDepth; // Глубина меняется плавно: читается отсюда. Вне перехода пусто (D-053)
};

/**
 * Параметр в конкретном голосе: откуда брать значения и куда записать итог
 */
struct ParameterBinding {
  const BaseValue* base;
  ParameterRamp* ramp;
  std::vector<RouteBinding> routes;
};

/**
 * Модулятор в конкретном голосе. Его параметры считаются прямо перед ним: вклады
 * уже посчитанных модуляторов текущие, а вклад замыкающего цикл маршрута — с прошлого шага (D-010)
 */
struct ModulatorBinding {
  ModuleProcessor* processor;
  const ParameterRamp* ramps;
  float* outputs;
  uint32_t outputCount;
  std::vector<ParameterBinding> parameters;
};

/**
 * Значение голоса, которое копируется для глобальных получателей (D-004)
 */
struct ValueCopy {
  const float* source;
  float* target;
};

/**
 * Модуляция одного голоса графа. Указатели ведут в состояния, живущие отдельно от плана (D-023)
 */
struct VoiceModulation {
  std::vector<ModulatorBinding> modulators; // В порядке зависимостей
  std::vector<ParameterBinding> parameters; // Параметры исполняемых модулей
  std::vector<const ModuleProcessor*> holders; // Модуляторы, которые держат голос (D-051)
  std::vector<ValueCopy> lastValues; // Что этот голос отдаёт, когда он самый поздний
};

/**
 * Как считать значения параметров на этом шаге
 */
enum class PARAMETER_UPDATE {
  CHANGED, // Только те, что могли измениться
  ALL, // Все: новый план мог начать исполнять модуль, значения которого давно не считались
};

/**
 * Пересчитывает модуляторы голоса в порядке зависимостей. Значения параметров модуляторов
 * меняются вместе с ними. Вызывается из аудиопотока
 */
void runModulators(
  const VoiceModulation& modulation,
  const VoiceContext& context,
  uint32_t frames,
  float tempo,
  PARAMETER_UPDATE update = PARAMETER_UPDATE::CHANGED
);

/**
 * Пересчитывает параметры исполняемых модулей. Вызывается после runModulators и из аудиопотока
 */
void updateParameters(const VoiceModulation& modulation, PARAMETER_UPDATE update = PARAMETER_UPDATE::CHANGED);

/**
 * Готовит голос к новой ноте: модуляторы выдают начальные значения, параметры принимают их
 * сразу, без интерполяции от значений прошлой ноты. Вызывается из аудиопотока
 */
void startModulation(const VoiceModulation& modulation, const VoiceContext& context, float tempo);
