#include <doctest/doctest.h>

#include <cstdint>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

#include "../engine/preset/preset.h"
#include "./modelFixture.h"

namespace {
  using json = nlohmann::json;

  /**
   * Модель с генератором, подграфом, эффектом, модуляцией и изменёнными параметрами
   */
  struct PresetFixture : Fixture {
    ModuleId generator = 0;
    ModuleId osc = 0;
    ModuleId localFilter = 0;
    ModuleId localLfo = 0;
    ModuleId mainFilter = 0;
    ModuleId mainLfo = 0;

    PresetFixture() {
      this->generator = this->add("generator");
      this->osc = this->add("test.osc", this->generator);
      this->localFilter = this->add("test.filter", this->generator);
      this->localLfo = this->add("test.lfo", this->generator);
      this->mainFilter = this->add("test.filter");
      this->mainLfo = this->add("test.lfo");

      for (const Command& command : std::initializer_list<Command>{
        wire(this->osc, this->localFilter),
        wire(this->localFilter, this->getVoiceOutput(this->generator)),
        wire(this->generator, this->mainFilter),
        wire(this->mainFilter, this->model->getOutputId()),
        SetParameter{ this->mainFilter, "cutoff", 1200.0f },
        SetParameter{ this->mainFilter, "drive", 0.1f },
        SetParameter{ this->osc, "wave", 2.0f },
        AddRoute{ route(this->localLfo, this->localFilter, "cutoff", 0.25f) },
        AddRoute{ route(this->mainLfo, this->mainFilter, "cutoff", -0.7f) },
        AddRoute{ route(this->mainLfo, this->mainLfo, "rate", 0.1f) },
      }) {
        REQUIRE(this->model->apply(command).has_value());
      }
    }

    std::expected<SynthModel, std::string> reload(const std::string& text) const {
      return loadModel(&this->registry, text);
    }

    /**
     * Портит сохранённый файл и проверяет, что загрузка его отклоняет
     */
    void checkRejected(const std::function<void(json&)>& damage) const {
      json file = json::parse(saveModel(*this->model));

      damage(file);

      CHECK_FALSE(this->reload(file.dump()).has_value());
    }

    ModuleId findIn(const json& file, const std::string& type) const {
      for (const json& module : file.at("modules")) {
        if (module.at("type") == type) {
          return module.at("id").get<ModuleId>();
        }
      }

      FAIL("В файле нет модуля нужного типа");
      return 0;
    }
  };

  /**
   * Файл с модулем неизвестного типа в цепочке osc -> unknown -> output
   */
  struct StubFixture : Fixture {
    ModuleId osc = 0;
    ModuleId lfo = 0;
    ModuleId stubId = 0;

    StubFixture() {
      this->osc = this->add("test.osc");
      this->lfo = this->add("test.lfo");
      this->stubId = this->add("test.filter");

      for (const Command& command : std::initializer_list<Command>{
        wire(this->osc, this->stubId),
        wire(this->stubId, this->model->getOutputId()),
      }) {
        REQUIRE(this->model->apply(command).has_value());
      }
    }

    /**
     * Сохранённый файл, где фильтр заменён модулем типа из будущей версии
     */
    json buildFile() const {
      json file = json::parse(saveModel(*this->model));

      for (json& module : file.at("modules")) {
        if (module.at("id") == this->stubId) {
          module = {
            { "id", this->stubId },
            { "type", "future.chorus" },
            { "typeVersion", 3 },
            { "parameters", { { "rate", 0.5 } } },
            { "customData", { 1, 2, 3 } },
          };
        }
      }

      return file;
    }
  };
}

TEST_CASE_FIXTURE(PresetFixture, "Сохранение и загрузка не теряют данных") {
  const std::string saved = saveModel(*this->model);
  const auto loaded = this->reload(saved);

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(*loaded == *this->model);
  CHECK(saveModel(*loaded) == saved);
}

TEST_CASE_FIXTURE(PresetFixture, "Загруженная модель работает как обычная") {
  auto loaded = this->reload(saveModel(*this->model));
  REQUIRE(loaded.has_value());

  // Подграф и соединения восстановлены: те же правила действуют для них
  CHECK_FALSE(loaded->apply(wire(this->localFilter, this->osc)).has_value());
  CHECK_FALSE(loaded->apply(RemoveModule{ loaded->getOutputId() }).has_value());

  const CommandResult created = loaded->apply(AddModule{ "test.filter", this->generator });
  REQUIRE(created.has_value());
  CHECK(**created == this->model->getNextId());

  REQUIRE(loaded->apply(RemoveModule{ this->generator }).has_value());
  CHECK(loaded->findModule(this->osc) == nullptr);
}

TEST_CASE_FIXTURE(PresetFixture, "Числа записаны коротко и в реальных единицах (D-025)") {
  const std::string saved = saveModel(*this->model);

  CHECK(saved.find("\"cutoff\": 1200") != std::string::npos);
  CHECK(saved.find("\"cutoff\": 1200.0") == std::string::npos);
  CHECK(saved.find("\"drive\": 0.1") != std::string::npos);
  CHECK(saved.find("0.10000000") == std::string::npos);
}

TEST_CASE_FIXTURE(PresetFixture, "Порядок маршрутов сохраняется (D-010)") {
  const auto loaded = this->reload(saveModel(*this->model));

  REQUIRE(loaded.has_value());
  CHECK(loaded->getRoutes() == this->model->getRoutes());
}

TEST_CASE_FIXTURE(PresetFixture, "Счётчик ID сохраняется: ID удалённых модулей не повторяются") {
  const ModuleId last = this->add("test.lfo");

  REQUIRE(this->model->apply(RemoveModule{ last }).has_value());

  auto loaded = this->reload(saveModel(*this->model));
  REQUIRE(loaded.has_value());

  const CommandResult created = loaded->apply(AddModule{ "test.lfo" });
  REQUIRE(created.has_value());
  CHECK(**created > last);
}

TEST_CASE_FIXTURE(PresetFixture, "Счётчик ID не может оказаться меньше существующих ID") {
  json file = json::parse(saveModel(*this->model));
  file["nextModuleId"] = 1;

  auto loaded = this->reload(file.dump());
  REQUIRE(loaded.has_value());

  const CommandResult created = loaded->apply(AddModule{ "test.lfo" });
  REQUIRE(created.has_value());
  CHECK(**created == this->model->getNextId());
}

TEST_CASE_FIXTURE(PresetFixture, "Параметры при загрузке: значения по умолчанию, лишние ключи, диапазон") {
  json file = json::parse(saveModel(*this->model));

  for (json& module : file.at("modules")) {
    if (module.at("id") == this->mainFilter) {
      module["parameters"] = { { "drive", 5.0 }, { "removedInNewVersion", 1.0 } };
    }
  }

  const auto loaded = this->reload(file.dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  const ModuleInstance* filter = loaded->findModule(this->mainFilter);

  CHECK(filter->parameters.at("cutoff") == 1000.0f);
  CHECK(filter->parameters.at("drive") == 1.0f);
  CHECK_FALSE(filter->parameters.contains("removedInNewVersion"));
}

TEST_CASE_FIXTURE(PresetFixture, "Отключение модуля сохраняется, включённый модуль поля не пишет (D-040)") {
  REQUIRE(this->model->apply(SetModuleEnabled{ this->mainFilter, false }).has_value());
  REQUIRE(this->model->apply(SetModuleEnabled{ this->localFilter, false }).has_value());

  const std::string saved = saveModel(*this->model);
  const auto loaded = this->reload(saved);

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK_FALSE(loaded->findModule(this->mainFilter)->enabled);
  CHECK_FALSE(loaded->findModule(this->localFilter)->enabled);
  CHECK(loaded->findModule(this->osc)->enabled);
  CHECK(*loaded == *this->model);
  CHECK(saveModel(*loaded) == saved);

  for (const json& module : json::parse(saved).at("modules")) {
    const ModuleId id = module.at("id").get<ModuleId>();

    CAPTURE(id);
    CHECK(module.contains("enabled") == (id == this->mainFilter || id == this->localFilter));
  }
}

TEST_CASE_FIXTURE(PresetFixture, "Поле enabled при загрузке (D-040)") {
  json file = json::parse(saveModel(*this->model));

  for (json& module : file.at("modules")) {
    if (module.at("id") == this->mainFilter) {
      module["enabled"] = true;
    }
  }

  const auto loaded = this->reload(file.dump());

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(loaded->findModule(this->mainFilter)->enabled);
}

TEST_CASE_FIXTURE(PresetFixture, "Положение блока сохраняется и не обязательно (D-065)") {
  REQUIRE(this->model->apply(SetModulePosition{ this->model->getOutputId(), { 640.0f, 40.5f } }).has_value());

  const std::string text = saveModel(*this->model);
  const json file = json::parse(text);
  const auto loaded = loadModel(&this->registry, text);

  REQUIRE(loaded.has_value());
  CHECK(loaded->findModule(this->model->getOutputId())->position == ModulePosition{ 640.0f, 40.5f });
  CHECK(*loaded == *this->model);

  // Модуль, который не двигали, поля не пишет: файл остаётся таким же, как у старых версий
  size_t positions = 0;

  for (const json& module : file.at("modules")) {
    positions += module.contains("position") ? 1 : 0;
  }

  CHECK(positions == 1);
  CHECK(file.at("version") == 1);
}

TEST_CASE_FIXTURE(PresetFixture, "Файл с неверным положением отклоняется (D-065)") {
  json file = json::parse(saveModel(*this->model));

  SUBCASE("не объект") {
    file.at("modules")[0]["position"] = 5;
  }

  SUBCASE("нет координаты") {
    file.at("modules")[0]["position"] = { { "x", 5 } };
  }

  SUBCASE("координата не число") {
    file.at("modules")[0]["position"] = { { "x", 5 }, { "y", "верх" } };
  }

  CHECK_FALSE(loadModel(&this->registry, file.dump()).has_value());
}

TEST_CASE_FIXTURE(PresetFixture, "Файл с неверным полем enabled отклоняется (D-040)") {
  const ModuleId mainFilter = this->mainFilter;
  const ModuleId outputId = this->model->getOutputId();
  const ModuleId voiceOutput = this->getVoiceOutput(this->generator);

  const auto setEnabled = [](json& file, ModuleId id, const json& value) {
    for (json& module : file.at("modules")) {
      if (module.at("id") == id) {
        module["enabled"] = value;
        return;
      }
    }

    FAIL("В файле нет модуля");
  };

  for (const json& value : { json(0), json(1), json("false"), json(nullptr) }) {
    CAPTURE(value.dump());
    this->checkRejected([&](json& file) { setEnabled(file, mainFilter, value); });
  }

  this->checkRejected([&](json& file) { setEnabled(file, outputId, false); });
  this->checkRejected([&](json& file) { setEnabled(file, voiceOutput, false); });
}

TEST_CASE_FIXTURE(PresetFixture, "Файл с нарушенной структурой отклоняется") {
  const ModuleId mainFilter = this->mainFilter;
  const ModuleId generator = this->generator;

  const auto findModule = [](json& file, ModuleId id) -> json& {
    for (json& module : file.at("modules")) {
      if (module.at("id") == id) {
        return module;
      }
    }

    FAIL("В файле нет модуля");
    return file;
  };

  SUBCASE("Не JSON") {
    CHECK_FALSE(this->reload("").has_value());
    CHECK_FALSE(this->reload("{").has_value());
    CHECK_FALSE(this->reload("[1, 2, 3]").has_value());
    CHECK_FALSE(this->reload("null").has_value());
  }

  SUBCASE("Нет обязательных полей") {
    for (const char* key : { "version", "nextModuleId", "modules", "connections", "routes" }) {
      CAPTURE(key);
      this->checkRejected([key](json& file) { file.erase(key); });
    }
  }

  SUBCASE("Поля неверного типа") {
    this->checkRejected([](json& file) { file["modules"] = "text"; });
    this->checkRejected([](json& file) { file["connections"] = { { "a", 1 } }; });
    this->checkRejected([](json& file) { file["routes"] = 1; });
    this->checkRejected([&](json& file) { findModule(file, mainFilter)["parameters"] = 1; });
    this->checkRejected([&](json& file) { findModule(file, mainFilter)["parameters"]["drive"] = "text"; });
    this->checkRejected([&](json& file) { findModule(file, mainFilter).erase("type"); });

    // Модуль без проводов: файл отклоняется именно из-за ID, а не из-за оборванных соединений
    for (const json& id : { json(-1), json(500.5), json(UINT32_MAX), json("500") }) {
      CAPTURE(id.dump());
      this->checkRejected([&](json& file) {
        file["modules"].push_back({ { "id", id }, { "type", "test.lfo" }, { "typeVersion", 1 } });
      });
    }

    this->checkRejected([](json& file) { file["connections"][0]["toModule"] = "1"; });
    this->checkRejected([](json& file) { file["routes"][0].erase("depth"); });
  }

  SUBCASE("Версия файла") {
    this->checkRejected([](json& file) { file["version"] = 2; });
    this->checkRejected([](json& file) { file["version"] = 0; });
    this->checkRejected([](json& file) { file["version"] = "1"; });
  }

  SUBCASE("Выход графа") {
    this->checkRejected([](json& file) {
      file["modules"].erase(0);
      file["connections"].clear();
    });
    this->checkRejected([](json& file) { file["modules"].push_back(file["modules"][0]); });
    this->checkRejected([&](json& file) { findModule(file, 1)["id"] = 500; });
    this->checkRejected([&](json& file) {
      json second = file["modules"][0];
      second["id"] = 500;
      second["owner"] = generator;
      file["modules"].push_back(second);
    });
  }

  SUBCASE("Повторяющийся ID") {
    const ModuleId mainLfo = this->mainLfo;

    this->checkRejected([&](json& file) {
      file["modules"].push_back({ { "id", mainLfo }, { "type", "test.lfo" }, { "typeVersion", 1 } });
    });
  }

  SUBCASE("Подграфы") {
    const ModuleId voiceOutput = this->getVoiceOutput(generator);

    // Выход голоса: нет, два и вне генератора
    this->checkRejected([&](json& file) {
      auto& modules = file["modules"];
      for (size_t i = 0; i < modules.size(); i++) {
        if (modules[i]["id"] == voiceOutput) {
          modules.erase(i);
          break;
        }
      }
      file["connections"] = json::array();
    });
    this->checkRejected([&](json& file) {
      json second = findModule(file, voiceOutput);
      second["id"] = 500;
      file["modules"].push_back(second);
    });
    this->checkRejected([](json& file) {
      file["modules"].push_back({ { "id", 500 }, { "type", "voiceOutput" }, { "typeVersion", 1 } });
    });

    // Владелец не генератор и не существует
    for (const ModuleId owner : { mainFilter, (ModuleId)9999 }) {
      CAPTURE(owner);
      this->checkRejected([owner](json& file) {
        file["modules"].push_back({ { "id", 500 }, { "type", "test.lfo" }, { "typeVersion", 1 }, { "owner", owner } });
      });
    }

    // Вложенный генератор и модуль, недопустимый в полифонической зоне
    this->checkRejected([&](json& file) { findModule(file, generator)["owner"] = generator; });
    this->checkRejected([&](json& file) {
      file["modules"].push_back({ { "id", 500 }, { "type", "test.reverb" }, { "typeVersion", 1 }, { "owner", generator } });
    });
  }

  SUBCASE("Соединения и маршруты проходят те же проверки, что и команды") {
    const ModuleId osc = this->osc;
    const ModuleId localLfo = this->localLfo;
    const ModuleId localFilter = this->localFilter;

    this->checkRejected([](json& file) { file["connections"][0]["toPort"] = "missing"; });
    this->checkRejected([](json& file) { file["connections"][0]["toModule"] = 9999; });
    this->checkRejected([](json& file) { file["connections"].push_back(file["connections"][0]); });
    this->checkRejected([&](json& file) {
      file["connections"].push_back({ { "fromModule", localFilter }, { "fromPort", "out" }, { "toModule", localFilter }, { "toPort", "in" } });
    });
    this->checkRejected([&](json& file) {
      file["connections"].push_back({ { "fromModule", osc }, { "fromPort", "out" }, { "toModule", mainFilter }, { "toPort", "in" } });
    });
    this->checkRejected([](json& file) { file["routes"][0]["target"]["module"] = 9999; });
    this->checkRejected([](json& file) { file["routes"][0]["target"]["parameter"] = "drive"; });
    this->checkRejected([](json& file) { file["routes"].push_back(file["routes"][0]); });

    // Голосовой модулятор на параметр другого генератора
    this->checkRejected([&](json& file) {
      file["modules"].push_back({ { "id", 500 }, { "type", "generator" }, { "typeVersion", 1 } });
      file["modules"].push_back({ { "id", 501 }, { "type", "voiceOutput" }, { "typeVersion", 1 }, { "owner", 500 } });
      file["modules"].push_back({ { "id", 502 }, { "type", "test.filter" }, { "typeVersion", 1 }, { "owner", 500 } });
      file["routes"].push_back({
        { "source", { { "module", localLfo }, { "output", "out" } } },
        { "target", { { "module", 502 }, { "parameter", "cutoff" } } },
        { "depth", 0.5 },
      });
    });
  }
}

TEST_CASE_FIXTURE(StubFixture, "Заглушку можно передвинуть, остальные её данные не меняются (D-065)") {
  auto loaded = loadModel(&this->registry, this->buildFile().dump());

  REQUIRE(loaded.has_value());
  REQUIRE(loaded->apply(SetModulePosition{ this->stubId, { 300.0f, 200.0f } }).has_value());

  const std::string saved = saveModel(*loaded);
  const auto reloaded = loadModel(&this->registry, saved);

  REQUIRE(reloaded.has_value());
  CHECK(reloaded->findModule(this->stubId)->position == ModulePosition{ 300.0f, 200.0f });

  for (const json& module : json::parse(saved).at("modules")) {
    if (module.at("id") == this->stubId) {
      CHECK(module.at("type") == "future.chorus");
      CHECK(module.at("parameters").at("rate") == 0.5);
    }
  }
}

TEST_CASE_FIXTURE(StubFixture, "Неизвестный модуль становится заглушкой с портами из соединений (D-021)") {
  const auto loaded = loadModel(&this->registry, this->buildFile().dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  const ModuleInstance* stub = loaded->findModule(this->stubId);

  REQUIRE(stub != nullptr);
  CHECK(stub->isStub());
  CHECK(stub->descriptor->type == "future.chorus");
  REQUIRE(stub->descriptor->inputs.size() == 1);
  CHECK(stub->descriptor->inputs[0].key == "in");
  REQUIRE(stub->descriptor->outputs.size() == 1);
  CHECK(stub->descriptor->outputs[0].key == "out");
  CHECK(loaded->getConnections().size() == 2);
}

TEST_CASE_FIXTURE(StubFixture, "Данные заглушки не теряются при пересохранении (D-021)") {
  const json file = this->buildFile();
  const auto loaded = loadModel(&this->registry, file.dump());
  REQUIRE(loaded.has_value());

  const json saved = json::parse(saveModel(*loaded));

  CHECK(saved == file);

  // Копия модели держит описание заглушки живым
  const SynthModel copy = *loaded;
  CHECK(json::parse(saveModel(copy)) == file);
}

TEST_CASE_FIXTURE(StubFixture, "Заглушка восстанавливает маршруты и не принимает изменений параметров") {
  json file = this->buildFile();

  file["routes"].push_back({
    { "source", { { "module", this->lfo }, { "output", "out" } } },
    { "target", { { "module", this->stubId }, { "parameter", "rate" } } },
    { "depth", 0.5 },
  });
  file["routes"].push_back({
    { "source", { { "module", this->stubId }, { "output", "env" } } },
    { "target", { { "module", this->lfo }, { "parameter", "rate" } } },
    { "depth", 0.25 },
  });

  auto loaded = loadModel(&this->registry, file.dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  CHECK(loaded->getRoutes().size() == 2);
  CHECK(json::parse(saveModel(*loaded)) == file);
  CHECK_FALSE(loaded->apply(SetParameter{ this->stubId, "rate", 0.1f }).has_value());
}

TEST_CASE_FIXTURE(StubFixture, "Заглушку нельзя отключить, поле enabled из файла сохраняется как есть (D-021, D-040)") {
  json file = this->buildFile();

  for (json& module : file.at("modules")) {
    if (module.at("id") == this->stubId) {
      module["enabled"] = false;
    }
  }

  auto loaded = loadModel(&this->registry, file.dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  CHECK_FALSE(loaded->apply(SetModuleEnabled{ this->stubId, true }).has_value());
  CHECK_FALSE(loaded->apply(SetModuleEnabled{ this->stubId, false }).has_value());
  CHECK(json::parse(saveModel(*loaded)) == file);
}

TEST_CASE_FIXTURE(StubFixture, "Заглушку можно удалить и отключить") {
  auto loaded = loadModel(&this->registry, this->buildFile().dump());
  REQUIRE(loaded.has_value());

  REQUIRE(loaded->apply(Disconnect{ wire(this->osc, this->stubId).connection }).has_value());
  REQUIRE(loaded->apply(RemoveModule{ this->stubId }).has_value());

  CHECK(loaded->getConnections().empty());
  CHECK(json::parse(saveModel(*loaded)).at("modules").size() == 3);
}

TEST_CASE_FIXTURE(StubFixture, "Заглушка без входов и в подграфе загружается") {
  const ModuleId generator = this->add("generator");

  json file = this->buildFile();
  file["modules"].push_back({ { "id", 500 }, { "type", "future.sampler" }, { "typeVersion", 1 }, { "owner", generator } });

  const auto loaded = loadModel(&this->registry, file.dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  const ModuleInstance* stub = loaded->findModule(500);

  REQUIRE(stub != nullptr);
  CHECK(stub->isStub());
  CHECK(stub->owner == generator);
  CHECK(stub->descriptor->inputs.empty());
}

TEST_CASE_FIXTURE(StubFixture, "Модуль более новой версии становится заглушкой") {
  json file = json::parse(saveModel(*this->model));

  for (json& module : file.at("modules")) {
    if (module.at("id") == this->stubId) {
      module["typeVersion"] = 2;
      module["parameters"]["futureOption"] = 7;
    }
  }

  const auto loaded = loadModel(&this->registry, file.dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  CHECK(loaded->findModule(this->stubId)->isStub());
  CHECK(json::parse(saveModel(*loaded)) == file);
}

TEST_CASE_FIXTURE(PresetFixture, "Несдвинутые макросы в файл не пишутся, сдвинутые сохраняются (D-055)") {
  const auto hasMacros = [](const std::string& text) {
    for (const json& module : json::parse(text).at("modules")) {
      if (module.at("type") == "macros") {
        return true;
      }
    }

    return false;
  };

  CHECK_FALSE(hasMacros(saveModel(*this->model)));

  REQUIRE(this->model->apply(SetParameter{ MACROS_MODULE_ID, "macro2", 0.5f }).has_value());

  const std::string text = saveModel(*this->model);

  CHECK(hasMacros(text));

  const auto loaded = loadModel(&this->registry, text);

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(*loaded == *this->model);
}

TEST_CASE_FIXTURE(PresetFixture, "Маршрут от макроса сохраняется (D-055)") {
  const ModuleId filter = this->add("test.filter");

  REQUIRE(this->model->apply(AddRoute{ { { MACROS_MODULE_ID, "macro1" }, { filter, "cutoff" }, 0.5f } }).has_value());

  const auto loaded = loadModel(&this->registry, saveModel(*this->model));

  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  CHECK(*loaded == *this->model);
}

TEST_CASE_FIXTURE(PresetFixture, "Файл с неверным модулем макросов отклоняется (D-055)") {
  const auto addMacros = [](json& file, uint64_t id) {
    file["modules"].push_back({ { "id", id }, { "type", "macros" }, { "typeVersion", 1 } });
  };

  this->checkRejected([&addMacros](json& file) { addMacros(file, 50); });
  this->checkRejected([&addMacros](json& file) { addMacros(file, 0); addMacros(file, 0); });
  this->checkRejected([&addMacros](json& file) {
    addMacros(file, 0);
    file["modules"].back()["owner"] = 2;
  });
}
