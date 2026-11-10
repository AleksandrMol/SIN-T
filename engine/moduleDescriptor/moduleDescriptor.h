#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "../parameterDescriptor/parameterDescriptor.h"

class ModuleProcessor;

// Создаёт DSP-часть модуля. Модель её не вызывает и от DSP не зависит (D-033)
using ProcessorFactory = std::unique_ptr<ModuleProcessor> (*)();

// Параметры модуля из файла в реальных единицах: ключ -> значение (D-025)
using ParameterValues = std::map<std::string, float>;

// Переводит параметры на одну версию типа вперёд (D-086). Значения может и не быть: файл его не обязан хранить
using ModuleMigration = void (*)(ParameterValues& values);

enum class MOD_POLARITY {
  UNIPOLAR, // 0..1, например ENV
  BIPOLAR, // -1..1, например LFO
};

enum class MODULE_ROLE {
  REGULAR,
  GENERATOR, // Контейнер с полифоническим подграфом (D-004)
  OUTPUT, // Выход графа: создаётся автоматически, вручную не создаётся и не удаляется (D-034)
  MACROS, // Макросы: один модуль на модель, живёт всегда (D-055)
};

/**
 * Аудиопорт. Всегда стерео (D-026), поэтому числа каналов нет
 */
struct PortDescriptor {
  std::string key;
  std::string name;
};

/**
 * Выход источника модуляции
 */
struct ModOutputDescriptor {
  std::string key;
  std::string name;
  MOD_POLARITY polarity;
};

/**
 * Описание типа модуля (D-033): общее для всех экземпляров, лежит рядом с DSP-кодом модуля.
 * Модель знает модули только через него и не зависит от их реализации.
 * Живёт всё время работы плагина: реестр хранит указатель
 */
struct ModuleDescriptor {
  std::string type; // Постоянная строка: по ней тип ищется в пресетах
  std::string name;
  uint32_t typeVersion = 1;
  std::vector<ModuleMigration> migrations{}; // migrations[n] переводит версию n + 1 в версию n + 2
  std::vector<PortDescriptor> inputs{};
  std::vector<PortDescriptor> outputs{};
  std::vector<ModOutputDescriptor> modOutputs{};
  std::vector<ParameterDescriptor> parameters{};
  bool isAllowedInPoly = false;
  bool isVoiceModulator = false; // Живёт нотой: в главном графе это общий модулятор (D-005, D-048)
  MODULE_ROLE role = MODULE_ROLE::REGULAR;
  ProcessorFactory createProcessor = nullptr; // Без неё модуль ведёт себя как заглушка D-021
};

/**
 * Проверяет корректность описания. В случае ошибки возвращает её текст
 */
std::expected<void, std::string> validateModule(const ModuleDescriptor& module);

/**
 * Порт, выход модуляции или параметр описания по постоянному ключу. Пусто, если такого нет
 */
template <typename T>
const T* findByKey(const std::vector<T>& items, std::string_view key) {
  const auto item = std::ranges::find_if(items, [key](const T& candidate) {
    return candidate.key == key;
  });

  return item != items.end() ? &*item : nullptr;
}
