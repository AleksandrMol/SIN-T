#pragma once

#include "../executionPlan/executionPlan.h"
#include "../modulationPlan/modulationPlan.h"
#include "../synthModel/synthModel.h"

/**
 * Приводит базовые значения в соответствие с моделью. Значения принимаются сразу, без сглаживания.
 * Модуль получает записи один раз и навсегда: на них указывают планы
 */
void syncParameterBases(const SynthModel& model, ParameterBases& bases);

/**
 * То же, пока идёт звук: добавляет новые модули и передаёт exchange исчезнувшие.
 * Значения существующих принадлежат аудиопотоку и не меняются
 */
void addParameterBases(const SynthModel& model, ParameterBases& bases, PlanExchange& exchange);

/**
 * Добавляет в собранный план модуляцию: порядок модуляторов и маршруты. Модулятор попадает в план,
 * только если от него цепочка маршрутов доходит до исполняемого модуля. Отключённый источник и
 * заглушка дают 0, маршруты на их параметры не действуют (D-021, D-040)
 */
void compileModulation(const SynthModel& model, ModuleSlots& slots, ParameterBases& bases, ExecutionPlan& plan);
