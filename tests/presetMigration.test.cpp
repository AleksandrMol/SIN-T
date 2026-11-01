#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/preset/preset.h"
#include "../engine/presetMigration/presetMigration.h"
#include "../engine/sessionState/sessionState.h"
#include "./modelFixture.h"

namespace {
  using json = nlohmann::json;

  void addStep(json& file) {
    file["steps"].push_back("first");
  }

  void doubleLastStep(json& file) {
    file["steps"].push_back(file.at("steps").back().get<std::string>() + "+second");
  }

  constexpr std::array<FileMigration, 2> TEST_CHAIN = { addStep, doubleLastStep };

  // Умножение и сложение не перестановочны: неверный порядок шагов даст другое число
  const ModuleDescriptor TEST_VERSIONED = {
    .type = "test.versioned",
    .name = "Test versioned",
    .typeVersion = 3,
    .migrations = {
      [](ParameterValues& values) {
        if (values.contains("gain")) {
          values["gain"] *= 2.0f;
        }
      },
      [](ParameterValues& values) {
        if (values.contains("gain")) {
          values["gain"] += 1.0f;
        }

        values.erase("legacy");
      },
    },
    .inputs = { { "in", "In" } },
    .outputs = { { "out", "Out" } },
    .parameters = {
      {
        .key = "gain",
        .name = "Gain",
        .minValue = 0.0f,
        .maxValue = 100.0f,
        .defaultValue = 50.0f,
        .isModulatable = true,
      },
    },
    .isAllowedInPoly = true,
  };

  struct MigrationFixture : Fixture {
    ModuleId versioned = 0;
    ModuleId osc = 0;
    ModuleId lfo = 0;

    MigrationFixture() {
      REQUIRE(this->registry.add(&TEST_VERSIONED).has_value());

      this->versioned = this->add("test.versioned");
      this->osc = this->add("test.osc");
      this->lfo = this->add("test.lfo");

      for (const Command& command : std::initializer_list<Command>{
        wire(this->osc, this->versioned),
        wire(this->versioned, this->model->getOutputId()),
        AddRoute{ route(this->lfo, this->versioned, "gain", 0.25f) },
        SetModuleEnabled{ this->versioned, false },
        SetModulePosition{ this->versioned, { 10.0f, 20.0f } },
      }) {
        REQUIRE(this->model->apply(command).has_value());
      }
    }

    /**
     * Сохранённый файл, где модуль выглядит записанным старой версией
     */
    json buildFile(uint32_t typeVersion, const json& parameters) const {
      json file = json::parse(saveModel(*this->model));

      for (json& module : file.at("modules")) {
        if (module.at("id") == this->versioned) {
          module["typeVersion"] = typeVersion;
          module["parameters"] = parameters;
        }
      }

      return file;
    }

    float loadGain(uint32_t typeVersion, float gain) const {
      const auto loaded = loadModel(&this->registry, this->buildFile(typeVersion, { { "gain", gain } }).dump());

      REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

      return loaded->findModule(this->versioned)->parameters.at("gain");
    }
  };

  std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::stringstream text;

    REQUIRE(stream.good());
    text << stream.rdbuf();

    return text.str();
  }
}

TEST_CASE("Цепочка файла идёт от его версии до текущей, шаги по порядку") {
  json fromFirst = { { "version", 1u }, { "steps", json::array() } };
  json fromSecond = { { "version", 2u }, { "steps", { "kept" } } };
  json current = { { "version", 3u }, { "steps", json::array() } };

  runFileMigrations(fromFirst, 3, TEST_CHAIN);
  runFileMigrations(fromSecond, 3, TEST_CHAIN);
  runFileMigrations(current, 3, TEST_CHAIN);

  CHECK(fromFirst.at("steps") == json{ "first", "first+second" });
  CHECK(fromSecond.at("steps") == json{ "kept", "kept+second" });
  CHECK(current.at("steps").empty());
}

TEST_CASE("Цепочка файла отклоняет неверные версии и не выходит за свой конец") {
  json newer = { { "version", 4u } };
  json zero = { { "version", 0u } };
  json negative = { { "version", -1 } };
  json text = { { "version", "1" } };
  json old = { { "version", 1u } };

  CHECK_THROWS_AS(runFileMigrations(newer, 3, TEST_CHAIN), PresetError);
  CHECK_THROWS_AS(runFileMigrations(zero, 3, TEST_CHAIN), PresetError);
  CHECK_THROWS_AS(runFileMigrations(negative, 3, TEST_CHAIN), PresetError);
  CHECK_THROWS_AS(runFileMigrations(text, 3, TEST_CHAIN), PresetError);

  // Версию подняли, шаг забыли: файл отклоняется, а не читается за концом цепочки
  CHECK_THROWS_AS(runFileMigrations(old, 4, TEST_CHAIN), PresetError);
}

TEST_CASE_FIXTURE(MigrationFixture, "Модуль проходит шаги от своей версии до текущей, по порядку") {
  CHECK(this->loadGain(1, 5.0f) == 11.0f);
  CHECK(this->loadGain(2, 5.0f) == 6.0f);
  CHECK(this->loadGain(3, 5.0f) == 5.0f);
}

TEST_CASE_FIXTURE(MigrationFixture, "Миграция модуля не трогает остальные его данные и файл") {
  const auto loaded = loadModel(&this->registry, this->buildFile(1, { { "gain", 5.0 }, { "legacy", 1.0 } }).dump());

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  const ModuleInstance* module = loaded->findModule(this->versioned);

  CHECK_FALSE(module->enabled);
  CHECK(module->position == ModulePosition{ 10.0f, 20.0f });
  CHECK_FALSE(module->isStub());
  CHECK(loaded->getConnections() == this->model->getConnections());
  CHECK(loaded->getRoutes() == this->model->getRoutes());

  // Пересохранённый файл уже новой версии: второй проход миграций значение не изменит
  const json saved = json::parse(saveModel(*loaded));
  const auto reloaded = loadModel(&this->registry, saved.dump());

  REQUIRE_MESSAGE(reloaded.has_value(), reloaded.error());
  CHECK(reloaded->findModule(this->versioned)->parameters.at("gain") == 11.0f);

  for (const json& entry : saved.at("modules")) {
    if (entry.at("id") == this->versioned) {
      CHECK(entry.at("typeVersion") == 3);
      CHECK_FALSE(entry.at("parameters").contains("legacy"));
    }
  }
}

TEST_CASE_FIXTURE(MigrationFixture, "Файл без значения: параметр получает умолчание новой версии") {
  json file = this->buildFile(1, json::object());

  for (json& module : file.at("modules")) {
    if (module.at("id") == this->versioned) {
      module.erase("parameters");
    }
  }

  const auto loaded = loadModel(&this->registry, file.dump());

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(loaded->findModule(this->versioned)->parameters.at("gain") == 50.0f);
}

TEST_CASE_FIXTURE(MigrationFixture, "Модуль в подграфе тоже мигрирует") {
  const ModuleId generator = this->add("generator");
  const ModuleId inner = this->add("test.versioned", generator);

  json file = json::parse(saveModel(*this->model));

  for (json& module : file.at("modules")) {
    if (module.at("id") == inner) {
      module["typeVersion"] = 2;
      module["parameters"] = { { "gain", 7.0 } };
    }
  }

  const auto loaded = loadModel(&this->registry, file.dump());

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(loaded->findModule(inner)->parameters.at("gain") == 8.0f);
}

TEST_CASE_FIXTURE(MigrationFixture, "Модуль более новой версии остаётся нетронутой заглушкой (D-021, D-037)") {
  const json file = this->buildFile(4, { { "gain", 5.0 }, { "futureOption", 7 } });
  const auto loaded = loadModel(&this->registry, file.dump());

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(loaded->findModule(this->versioned)->isStub());
  CHECK(json::parse(saveModel(*loaded)) == file);
}

TEST_CASE_FIXTURE(MigrationFixture, "Неверные данные модуля при миграции отклоняются") {
  CHECK_FALSE(loadModel(&this->registry, this->buildFile(0, { { "gain", 5.0 } }).dump()).has_value());
  CHECK_FALSE(loadModel(&this->registry, this->buildFile(1, 1).dump()).has_value());
  CHECK_FALSE(loadModel(&this->registry, this->buildFile(1, json::parse(R"([["gain", 5]])")).dump()).has_value());
  CHECK_FALSE(loadModel(&this->registry, this->buildFile(1, { { "gain", "text" } }).dump()).has_value());
}

TEST_CASE("Неизменные файлы версии 1 читаются без потерь (D-086)") {
  ModuleRegistry registry;
  REQUIRE(addAllModules(registry).has_value());

  // Этот файл не пересохраняется: он и есть образец старого формата
  const std::string text = readFile(std::filesystem::path(SINT_SOURCE_DIR) / "tests/golden/full-chain.sintpreset");
  const json original = json::parse(text);

  CHECK(original.at("version") == 1);

  const auto loaded = loadSession(&registry, text);

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(loaded->model.getRoutes().size() == original.at("routes").size());
  CHECK(loaded->model.getConnections().size() == original.at("connections").size());
}
