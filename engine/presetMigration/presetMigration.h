#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>

#include <nlohmann/json_fwd.hpp>

#include "../moduleRegistry/moduleRegistry.h"

/**
 * Версия формата, которую пишет плагин. Новое необязательное поле её не поднимает (D-041, D-086)
 */
inline constexpr uint64_t CURRENT_PRESET_VERSION = 1;

/**
 * Ошибка содержимого файла. Наружу из загрузки не выходит: она превращается в текст
 */
class PresetError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

/**
 * Шаг цепочки: chain[n] переводит файл версии n + 1 в версию n + 2 (D-020)
 */
using FileMigration = void (*)(nlohmann::json& file);

/**
 * Проходит цепочку от версии файла до currentVersion. Файл более новой версии не читается.
 * Цепочка передаётся явно, чтобы её можно было проверить, пока настоящая пуста
 */
void runFileMigrations(nlohmann::json& file, uint64_t currentVersion, std::span<const FileMigration> chain);

/**
 * Переводит файл старой версии в текущую. Охватывает весь файл, вместе с полем session
 */
void migrateFile(nlohmann::json& file);

/**
 * Переводит параметры модулей старых typeVersion в текущие (D-086). Вызывается после migrateFile.
 * Неизвестные типы и более новые версии не трогает: они остаются как есть ради заглушки (D-021)
 */
void migrateModules(nlohmann::json& file, const ModuleRegistry* registry);
