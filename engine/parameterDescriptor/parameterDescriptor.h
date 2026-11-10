#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Число знаков после точки в тексте значения подбирается по величине числа
inline constexpr int AUTO_DECIMALS = -1;
inline constexpr int MAX_DECIMALS = 6;

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
  bool isStructural = false; // Значение приходит с планом: ему может понадобиться память (D-053)
  std::vector<std::string> choices{}; // Подписи ступеней, начиная с minValue. Только у ступенчатых (D-066)
  std::string shortName{}; // Подпись под ручкой, если имя в неё не помещается
  int decimals = AUTO_DECIMALS; // Знаков после точки в тексте значения: точнее, чем нужно на слух, не показываем
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

/**
 * Значение в реальных единицах как текст для человека: «1200 Hz», «0.300 s», «Saw», «180°».
 * Градус и процент пишутся слитно. Не зависит от локали: хост мог поменять её во всём процессе
 */
std::string formatParameter(const ParameterDescriptor& parameter, float real);

/**
 * Как formatParameter, но без единиц, которые пишутся через пробел: для тесного поля,
 * где имя параметра уже говорит, что это за число (D-108). Слитные «°» и «%» остаются
 */
std::string formatParameterShort(const ParameterDescriptor& parameter, float real);

/**
 * Обратно к formatParameter. Подпись ступени читается без учёта регистра, число тоже подходит.
 * Единицы после числа не обязательны. Значение вне диапазона ограничивается.
 * Пусто, если это не подпись и текст не начинается с числа
 */
std::optional<float> parseParameter(const ParameterDescriptor& parameter, std::string_view text);
