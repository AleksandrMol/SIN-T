#pragma once

#include <doctest/doctest.h>

#include <limits>
#include <optional>
#include <string>

#include "../engine/builtinModules/builtinModules.h"
#include "../engine/synthModel/synthModel.h"

// Придуманные модули: тесты модели не зависят от настоящего DSP (D-033)
inline const ModuleDescriptor TEST_OSC = {
  .type = "test.osc",
  .name = "Test oscillator",
  .outputs = { { "out", "Out" } },
  .parameters = {
    {
      .key = "wave",
      .name = "Wave",
      .minValue = 1.0f,
      .maxValue = 4.0f,
      .defaultValue = 3.0f,
      .isStepped = true,
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
};

inline const ModuleDescriptor TEST_FILTER = {
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
    {
      .key = "drive",
      .name = "Drive",
      .minValue = 0.0f,
      .maxValue = 1.0f,
      .defaultValue = 0.0f,
    },
  },
  .isAllowedInPoly = true,
};

inline const ModuleDescriptor TEST_LFO = {
  .type = "test.lfo",
  .name = "Test LFO",
  .modOutputs = { { "out", "Out", MOD_POLARITY::BIPOLAR } },
  .parameters = {
    {
      .key = "rate",
      .name = "Rate",
      .minValue = 0.1f,
      .maxValue = 20.0f,
      .defaultValue = 1.0f,
      .isModulatable = true,
    },
  },
  .isAllowedInPoly = true,
};

inline const ModuleDescriptor TEST_REVERB = {
  .type = "test.reverb",
  .name = "Test reverb",
  .inputs = { { "in", "In" } },
  .outputs = { { "out", "Out" } },
  .isAllowedInPoly = false,
};

inline const float NOT_A_NUMBER = std::numeric_limits<float>::quiet_NaN();

/**
 * Модель с реестром из встроенных и тестовых модулей
 */
struct Fixture {
  ModuleRegistry registry;
  std::optional<SynthModel> storage;
  SynthModel* model = nullptr;

  Fixture() {
    REQUIRE(addBuiltinModules(this->registry).has_value());

    for (const ModuleDescriptor* descriptor : { &TEST_OSC, &TEST_FILTER, &TEST_LFO, &TEST_REVERB }) {
      REQUIRE(this->registry.add(descriptor).has_value());
    }

    this->storage.emplace(&this->registry);
    this->model = &*this->storage;
  }

  ModuleId add(const std::string& type, std::optional<ModuleId> owner = std::nullopt) {
    const CommandResult result = this->model->apply(AddModule{ type, owner });

    REQUIRE(result.has_value());
    REQUIRE(result->has_value());

    return **result;
  }

  /**
   * ID выхода голоса в подграфе генератора
   */
  ModuleId getVoiceOutput(ModuleId generator) const {
    for (const auto& [id, module] : this->model->getModules()) {
      if (module.owner == generator && module.descriptor == &VOICE_OUTPUT_DESCRIPTOR) {
        return id;
      }
    }

    FAIL("У генератора нет выхода голоса");
    return 0;
  }

  /**
   * Команда должна быть отклонена и оставить модель без изменений
   */
  void checkRejected(const Command& command) {
    const SynthModel before = *this->model;

    CHECK_FALSE(this->model->apply(command).has_value());
    CHECK(*this->model == before);
  }
};

inline Connect wire(ModuleId from, ModuleId to) {
  return { { from, "out", to, "in" } };
}

inline ModRoute route(ModuleId source, ModuleId target, const std::string& parameter, float depth = 0.5f) {
  return { { source, "out" }, { target, parameter }, depth };
}
