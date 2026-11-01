#include <doctest/doctest.h>

#include <string>

#include "../engine/moduleRegistry/moduleRegistry.h"

namespace {
  // Придуманные модули: тесты модели не зависят от настоящего DSP (D-033)
  const ModuleDescriptor TEST_FILTER = {
    .type = "test.filter",
    .name = "Test filter",
    .inputs = { { "in", "In" } },
    .outputs = { { "out", "Out" } },
    .parameters = {
      {
        .key = "cutoff",
        .name = "Cutoff",
        .minValue = 20.0f,
        .maxValue = 20000.0f,
        .defaultValue = 1000.0f,
        .curve = PARAM_CURVE::LOG,
        .unit = "Hz",
        .isModulatable = true,
      },
    },
    .isAllowedInPoly = true,
  };

  const ModuleDescriptor TEST_LFO = {
    .type = "test.lfo",
    .name = "Test LFO",
    .modOutputs = { { "out", "Out", MOD_POLARITY::BIPOLAR } },
  };

  template <typename Change>
  ModuleDescriptor broken(ModuleDescriptor module, Change change) {
    change(module);
    return module;
  }
}

TEST_CASE("validateModule принимает модуль без аудио и без параметров") {
  // ТЗ §12: модуль не обязан поддерживать все возможности
  CHECK(validateModule(TEST_LFO).has_value());
  CHECK(validateModule(TEST_FILTER).has_value());
}

TEST_CASE("validateModule отклоняет некорректные описания") {
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.type = ""; })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.name = ""; })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.typeVersion = 0; })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.typeVersion = 2; })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.migrations.push_back(+[](ParameterValues&) {}); })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.typeVersion = 2; m.migrations.push_back(nullptr); })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.inputs.push_back({ "", "Empty" }); })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.outputs.push_back({ "out", "Copy" }); })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.parameters.push_back(m.parameters[0]); })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_LFO, [](auto& m) { m.modOutputs.push_back(m.modOutputs[0]); })).has_value());
  CHECK_FALSE(validateModule(broken(TEST_FILTER, [](auto& m) { m.parameters[0].minValue = 0.0f; })).has_value());
}

TEST_CASE("Одинаковые ключи в разных списках разрешены") {
  const ModuleDescriptor module = broken(TEST_FILTER, [](auto& m) {
    m.inputs[0].key = "cutoff";
    m.outputs[0].key = "cutoff";
    m.modOutputs.push_back({ "cutoff", "Cutoff", MOD_POLARITY::UNIPOLAR });
  });

  CHECK(validateModule(module).has_value());
}

TEST_CASE("Текст ошибки называет модуль и причину") {
  const auto result = validateModule(broken(TEST_FILTER, [](auto& m) { m.parameters[0].minValue = 0.0f; }));

  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("test.filter") != std::string::npos);
  CHECK(result.error().find("cutoff") != std::string::npos);
}

TEST_CASE("Реестр находит добавленные типы и сохраняет порядок") {
  ModuleRegistry registry;

  CHECK(registry.find("test.filter") == nullptr);

  REQUIRE(registry.add(&TEST_FILTER).has_value());
  REQUIRE(registry.add(&TEST_LFO).has_value());

  CHECK(registry.find("test.filter") == &TEST_FILTER);
  CHECK(registry.find("test.lfo") == &TEST_LFO);
  CHECK(registry.find("test.unknown") == nullptr);

  REQUIRE(registry.getAll().size() == 2);
  CHECK(registry.getAll()[0] == &TEST_FILTER);
  CHECK(registry.getAll()[1] == &TEST_LFO);
}

TEST_CASE("Реестр отклоняет повторный и некорректный тип") {
  ModuleRegistry registry;
  const ModuleDescriptor invalid = broken(TEST_LFO, [](auto& m) { m.name = ""; });

  REQUIRE(registry.add(&TEST_LFO).has_value());

  CHECK_FALSE(registry.add(&TEST_LFO).has_value());
  CHECK_FALSE(registry.add(&invalid).has_value());
  CHECK(registry.getAll().size() == 1);
}
