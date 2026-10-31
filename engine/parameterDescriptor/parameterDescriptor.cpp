#include "./parameterDescriptor.h"

#include <algorithm>
#include <cmath>

namespace {
  bool isInteger(float value) {
    return std::trunc(value) == value;
  }
}

std::expected<void, std::string> validateParameter(const ParameterDescriptor& parameter) {
  const std::string label = "параметр \"" + parameter.key + "\": ";

  if (parameter.key.empty()) {
    return std::unexpected("у параметра пустой ключ");
  }

  if (parameter.name.empty()) {
    return std::unexpected(label + "пустое имя");
  }

  if (!std::isfinite(parameter.minValue) || !std::isfinite(parameter.maxValue) || parameter.minValue >= parameter.maxValue) {
    return std::unexpected(label + "неверный диапазон");
  }

  if (!(parameter.defaultValue >= parameter.minValue && parameter.defaultValue <= parameter.maxValue)) {
    return std::unexpected(label + "значение по умолчанию вне диапазона");
  }

  if (parameter.curve == PARAM_CURVE::LOG && parameter.minValue <= 0.0f) {
    return std::unexpected(label + "логарифмической кривой нужен положительный минимум");
  }

  if (parameter.isStepped && !(isInteger(parameter.minValue) && isInteger(parameter.maxValue) && isInteger(parameter.defaultValue))) {
    return std::unexpected(label + "значения ступенчатого параметра должны быть целыми");
  }

  return {};
}

float toReal(const ParameterDescriptor& parameter, float normalized) {
  const float position = std::clamp(normalized, 0.0f, 1.0f);

  const float real = parameter.curve == PARAM_CURVE::LOG
    ? parameter.minValue * std::pow(parameter.maxValue / parameter.minValue, position)
    : parameter.minValue + position * (parameter.maxValue - parameter.minValue);

  // Из-за округления float результат может чуть выйти за границу диапазона
  return limitReal(parameter, real);
}

float toNormalized(const ParameterDescriptor& parameter, float real) {
  const float limited = std::clamp(real, parameter.minValue, parameter.maxValue);

  if (parameter.curve == PARAM_CURVE::LOG) {
    return std::log(limited / parameter.minValue) / std::log(parameter.maxValue / parameter.minValue);
  }

  return (limited - parameter.minValue) / (parameter.maxValue - parameter.minValue);
}

float limitReal(const ParameterDescriptor& parameter, float real) {
  const float limited = std::clamp(real, parameter.minValue, parameter.maxValue);

  return parameter.isStepped ? std::round(limited) : limited;
}
