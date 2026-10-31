#pragma once

#include "../executionPlan/executionPlan.h"
#include "../modulationPlan/modulationPlan.h"
#include "../synthModel/synthModel.h"

/**
 * Приводит базовые значения в соответствие с моделью. Значения принимаются сразу, без сглаживания
 */
void syncParameterBases(const SynthModel& model, ParameterBases& bases);

/**
 * Добавляет в собранный план модуляцию: порядок модуляторов и маршруты. Модулятор попадает в план,
 * только если от него цепочка маршрутов доходит до исполняемого модуля. Отключённый источник и
 * заглушка дают 0, маршруты на их параметры не действуют (D-021, D-040)
 */
void compileModulation(const SynthModel& model, ModuleSlots& slots, ParameterBases& bases, ExecutionPlan& plan);
