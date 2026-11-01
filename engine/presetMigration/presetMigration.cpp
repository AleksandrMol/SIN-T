#include "./presetMigration.h"

#include <array>
#include <string>

#include <nlohmann/json.hpp>

namespace {
  using json = nlohmann::json;

  // Размер задан явно: поднять версию, не дописав шаг, не даст static_assert
  constexpr std::array<FileMigration, 0> FILE_MIGRATIONS = {};

  static_assert(FILE_MIGRATIONS.size() == CURRENT_PRESET_VERSION - 1);

  void migrateModule(json& entry, const ModuleDescriptor& descriptor, uint32_t typeVersion) {
    ParameterValues values;

    if (entry.contains("parameters")) {
      values = entry.at("parameters").get<ParameterValues>();
    }

    for (uint32_t step = typeVersion; step < descriptor.typeVersion; step++) {
      descriptor.migrations[step - 1](values);
    }

    entry["parameters"] = values;
  }
}

void runFileMigrations(json& file, uint64_t currentVersion, std::span<const FileMigration> chain) {
  const json& versionField = file.at("version");

  if (!versionField.is_number_unsigned() || versionField.get<uint64_t>() == 0) {
    throw PresetError("неверная версия файла");
  }

  const uint64_t version = versionField.get<uint64_t>();

  if (version > currentVersion) {
    throw PresetError("файл создан более новой версией плагина");
  }

  if (chain.size() != currentVersion - 1) {
    throw PresetError("цепочка миграций не совпадает с версией формата");
  }

  for (uint64_t step = version; step < currentVersion; step++) {
    chain[step - 1](file);
  }
}

void migrateFile(json& file) {
  runFileMigrations(file, CURRENT_PRESET_VERSION, FILE_MIGRATIONS);
}

void migrateModules(json& file, const ModuleRegistry* registry) {
  for (json& entry : file.at("modules")) {
    const ModuleDescriptor* descriptor = registry->find(entry.at("type").get<std::string>());

    if (!descriptor) {
      continue;
    }

    const uint32_t typeVersion = entry.at("typeVersion").get<uint32_t>();

    if (typeVersion == 0) {
      throw PresetError("неверная версия модуля \"" + descriptor->type + "\"");
    }

    if (typeVersion < descriptor->typeVersion) {
      migrateModule(entry, *descriptor, typeVersion);
    }
  }
}
