#pragma once

#include <expected>
#include <string>

enum class PARAM_CURVE {
  LINEAR,
  LOG,
};

/**
 * Описание параметра модуля (D-025). Одно на все экземпляры модуля, значений не хранит
 */
struct ParameterDescriptor {
  std::string key; // Постоянный ключ: по нему параметр ищется в пресетах и маршрутах
  std::string name;
  float minValue;
  float maxValue;
  float defaultValue;
  PARAM_CURVE curve = PARAM_CURVE::LINEAR;
  std::string unit{};
  bool isStepped = false;
  bool isModulatable = false;
};

/**
 * Проверяет корректность описания. В случае ошибки возвращает её текст
 */
std::expected<void, std::string> validateParameter(const ParameterDescriptor& parameter);

/**
 * Переводит значение 0..1 в реальные единицы по кривой параметра.
 * Значение вне 0..1 ограничивается, у ступенчатого параметра результат округляется
 */
float toReal(const ParameterDescriptor& parameter, float normalized);

/**
 * Переводит реальные единицы в значение 0..1. Обратно к toReal
 */
float toNormalized(const ParameterDescriptor& parameter, float real);

/**
 * Приводит значение в реальных единицах к допустимому: ограничивает диапазоном,
 * у ступенчатого параметра округляет
 */
float limitReal(const ParameterDescriptor& parameter, float real);
