#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../engine/engine/engine.h"
#include "../engine/generator/generatorProcessor.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/preset/preset.h"
#include "../engine/synthModel/synthModel.h"
#include "./allocationCounter.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr uint32_t CHUNK_FRAMES = 32;
  constexpr uint32_t FADE_FRAMES = 480; // 10 мс
  constexpr uint32_t SETTLE_FRAMES = FADE_FRAMES + 2 * CHUNK_FRAMES;
  constexpr float LFO_SQUARE = 3.0f;
  constexpr float LFO_SLOW = 0.01f; // Половина периода — 50 секунд: меандр держит +1 весь тест
  constexpr int16_t KEY_A4 = 69;
  constexpr int16_t KEY_C5 = 72;

  using Samples = std::vector<float>;

  NoteAddress note(int16_t key) {
    return { -1, 0, 0, key };
  }

  /**
   * Источник постоянного сигнала: на выходе видно значение параметра, поэтому правила
   * модуляции проверяются числами. Какой параметр виден, задаёт view
   */
  class DcProcessor : public ModuleProcessor {
    public:
      void prepare(float) override {}

      void reset() override {}

      void process(const ProcessChunk& chunk) override {
        const ParameterRamp& parameter = chunk.parameters[(uint32_t)chunk.parameters[3].end];

        for (uint32_t frame = 0; frame < chunk.frames; frame++) {
          const float value = chunk.at(parameter, frame);

          chunk.outputs[0]->left[frame] = value;
          chunk.outputs[0]->right[frame] = value;
        }
      }
  };

  std::unique_ptr<ModuleProcessor> createDcProcessor() {
    return std::make_unique<DcProcessor>();
  }

  const ModuleDescriptor TEST_DC = {
    .type = "test.dc",
    .name = "Test DC",
    .outputs = { { "out", "Out" } },
    .parameters = {
      { .key = "level", .name = "Level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .isModulatable = true },
      {
        .key = "curved",
        .name = "Curved",
        .minValue = 0.01f,
        .maxValue = 1.0f,
        .defaultValue = 0.1f,
        .curve = PARAM_CURVE::LOG,
        .isModulatable = true,
      },
      { .key = "stepped", .name = "Stepped", .minValue = 0.0f, .maxValue = 4.0f, .defaultValue = 0.0f, .isStepped = true, .isModulatable = true },
      { .key = "view", .name = "View", .minValue = 0.0f, .maxValue = 2.0f, .defaultValue = 0.0f, .isStepped = true },
    },
    .isAllowedInPoly = true,
    .createProcessor = createDcProcessor,
  };

  /**
   * Модулятор без своего поведения: выход равен параметру. Цепочки и циклы считаются вручную
   */
  class PassProcessor : public ModuleProcessor {
    public:
      void prepare(float) override {}

      void reset() override {}

      void process(const ProcessChunk&) override {}

      void modulate(const ModulationChunk& chunk) override {
        chunk.outputs[0] = chunk.parameters[0].end;
      }
  };

  std::unique_ptr<ModuleProcessor> createPassProcessor() {
    return std::make_unique<PassProcessor>();
  }

  const ModuleDescriptor TEST_PASS = {
    .type = "test.pass",
    .name = "Test pass",
    .modOutputs = { { "out", "Out", MOD_POLARITY::UNIPOLAR } },
    .parameters = {
      { .key = "value", .name = "Value", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.0f, .isModulatable = true },
    },
    .isAllowedInPoly = true,
    .createProcessor = createPassProcessor,
  };

  // Модули, которых нет в реестре при загрузке: там они становятся заглушками (D-021)
  const ModuleDescriptor TEST_UNKNOWN_DC = [] {
    ModuleDescriptor descriptor = TEST_DC;

    descriptor.type = "test.unknownDc";

    return descriptor;
  }();

  const ModuleDescriptor TEST_UNKNOWN_PASS = [] {
    ModuleDescriptor descriptor = TEST_PASS;

    descriptor.type = "test.unknownPass";

    return descriptor;
  }();

  /**
   * Движок и модель для проверки маршрутов
   */
  struct ModulationFixture {
    ModuleRegistry registry;
    std::unique_ptr<SynthModel> model;
    Engine engine;

    ModulationFixture() {
      REQUIRE(addAllModules(this->registry).has_value());
      REQUIRE(this->registry.add(&TEST_DC).has_value());
      REQUIRE(this->registry.add(&TEST_PASS).has_value());

      this->model = std::make_unique<SynthModel>(&this->registry);
      this->engine.prepare(SAMPLE_RATE);
    }

    ModuleId add(const std::string& type, std::optional<ModuleId> owner = std::nullopt) {
      const CommandResult result = this->model->apply(AddModule{ type, owner });

      REQUIRE(result.has_value());

      return **result;
    }

    void set(ModuleId id, const std::string& parameter, float value) {
      REQUIRE(this->model->apply(SetParameter{ id, parameter, value }).has_value());
    }

    void connect(ModuleId from, ModuleId to) {
      REQUIRE(this->model->apply(Connect{ { from, "out", to, "in" } }).has_value());
    }

    void route(ModuleId source, ModuleId target, const std::string& parameter, float depth = 1.0f, const std::string& output = "out") {
      REQUIRE(this->model->apply(AddRoute{ { { source, output }, { target, parameter }, depth } }).has_value());
    }

    void setEnabled(ModuleId id, bool enabled) {
      REQUIRE(this->model->apply(SetModuleEnabled{ id, enabled }).has_value());
    }

    ModuleId getVoiceOutput(ModuleId generator) const {
      for (const auto& [id, module] : this->model->getModules()) {
        if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
          return id;
        }
      }

      FAIL("У генератора нет выхода голоса");
      return 0;
    }

    /**
     * Источник постоянного сигнала в главном графе, подключённый к выходу
     */
    ModuleId addMainDc(float level = 0.0f) {
      const ModuleId dc = this->add("test.dc");

      this->set(dc, "level", level);
      this->connect(dc, this->model->getOutputId());

      return dc;
    }

    struct Voice {
      ModuleId generator;
      ModuleId dc;
    };

    /**
     * Генератор с источником постоянного сигнала: на выходе сумма значений его голосов
     */
    Voice addDcGenerator() {
      const ModuleId generator = this->add("generator");
      const ModuleId dc = this->add("test.dc", generator);

      this->connect(dc, this->getVoiceOutput(generator));
      this->connect(generator, this->model->getOutputId());

      return { generator, dc };
    }

    /**
     * Меандр на самой низкой частоте: выдаёт +1 всё время теста
     */
    ModuleId addConstantLfo(std::optional<ModuleId> owner = std::nullopt) {
      const ModuleId lfo = this->add("lfo", owner);

      this->set(lfo, "wave", LFO_SQUARE);
      this->set(lfo, "rate", LFO_SLOW);

      return lfo;
    }

    ModuleId addEnv(std::optional<ModuleId> owner, float attack, float decay, float sustain, float release) {
      const ModuleId env = this->add("env", owner);

      this->set(env, "attack", attack);
      this->set(env, "decay", decay);
      this->set(env, "sustain", sustain);
      this->set(env, "release", release);

      return env;
    }

    void apply() {
      this->engine.applyModel(*this->model);
    }

    Samples run(uint32_t frames, uint32_t blockSize = 256) {
      Samples left(frames);
      Samples right(frames);

      for (uint32_t done = 0; done < frames; done += blockSize) {
        const uint32_t count = std::min(blockSize, frames - done);

        this->engine.process(left.data() + done, right.data() + done, count);
      }

      CHECK(left == right);

      return left;
    }

    /**
     * Значение в конце рендера. На сэмпле после конца шага сетки выход равен значению шага
     */
    float settle(uint32_t chunks = 4) {
      return this->run(chunks * CHUNK_FRAMES + 1).back();
    }

    /**
     * Две ноты с разрезами не по сетке. Начинается с чистого состояния
     */
    Samples renderPhrase(uint32_t blockSize) {
      this->engine.reset();

      Samples result;

      const auto play = [&](uint32_t frames) {
        const Samples part = this->run(frames, blockSize);

        result.insert(result.end(), part.begin(), part.end());
      };

      play(107);
      this->engine.noteOn(note(KEY_A4), 0.8f);
      play(1403);
      this->engine.noteOn(note(KEY_C5), 0.3f);
      play(2011);
      this->engine.noteOff(note(KEY_A4));
      play(5003);
      this->engine.noteOff(note(KEY_C5));
      play(9000);

      return result;
    }
  };

  float maxJump(const Samples& samples) {
    float result = 0.0f;

    for (size_t i = 1; i < samples.size(); i++) {
      result = std::fmax(result, std::fabs(samples[i] - samples[i - 1]));
    }

    return result;
  }

  float peak(const Samples& samples) {
    float result = 0.0f;

    for (const float sample : samples) {
      result = std::fmax(result, std::fabs(sample));
    }

    return result;
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Итог параметра: база + глубина × источник (D-025)") {
  const ModuleId dc = this->addMainDc(0.25f);
  const ModuleId lfo = this->addConstantLfo();

  SUBCASE("Положительная глубина") {
    this->route(lfo, dc, "level", 0.5f);
    this->apply();

    CHECK(this->settle() == doctest::Approx(0.75f));
  }

  SUBCASE("Отрицательная глубина") {
    this->route(lfo, dc, "level", -0.125f);
    this->apply();

    CHECK(this->settle() == doctest::Approx(0.125f));
  }

  SUBCASE("Без маршрута значение не меняется") {
    this->apply();

    CHECK(this->settle() == 0.25f);
  }

  SUBCASE("Несколько источников складываются") {
    this->route(lfo, dc, "level", 0.25f);
    this->route(this->addConstantLfo(), dc, "level", 0.125f);
    this->apply();

    CHECK(this->settle() == doctest::Approx(0.625f));
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Ограничение одно, после суммы всех вкладов (D-025)") {
  const ModuleId dc = this->addMainDc(0.875f);

  SUBCASE("Промежуточный выход за диапазон не обрезается") {
    this->route(this->addConstantLfo(), dc, "level", 1.0f);
    this->route(this->addConstantLfo(), dc, "level", -1.0f);
    this->apply();

    CHECK(this->settle() == doctest::Approx(0.875f));
  }

  SUBCASE("Итог ограничен сверху") {
    this->route(this->addConstantLfo(), dc, "level", 1.0f);
    this->apply();

    CHECK(this->settle() == 1.0f);
  }

  SUBCASE("Итог ограничен снизу") {
    this->route(this->addConstantLfo(), dc, "level", -1.0f);
    this->apply();

    CHECK(this->settle() == 0.0f);
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Вклады складываются в нормированном виде, кривая применяется к итогу (D-025)") {
  const ModuleId dc = this->addMainDc();

  this->set(dc, "view", 1.0f);
  this->set(dc, "curved", 0.1f); // Середина логарифмической шкалы 0,01..1
  this->route(this->addConstantLfo(), dc, "curved", 0.25f);
  this->apply();

  CHECK(this->settle() == doctest::Approx(0.01f * std::pow(100.0f, 0.75f)));
}

TEST_CASE_FIXTURE(ModulationFixture, "Ступенчатый параметр округляется и меняется только на границе шага (D-025)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.01f, 0.01f, 1.0f, 0.01f);

  this->set(dc, "view", 2.0f);
  this->route(env, dc, "stepped");
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  const Samples samples = this->run(960);

  // Значение кратно единице и постоянно внутри шага сетки
  for (size_t i = 0; i < samples.size(); i++) {
    CAPTURE(i);
    CHECK(samples[i] == std::round(samples[i]));

    if (i % CHUNK_FRAMES != 0) {
      CHECK(samples[i] == samples[i - 1]);
    }
  }

  CHECK(samples.back() == 4.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Внутри шага сетки значение идёт по прямой (D-008)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.01f, 10.0f, 1.0f, 10.0f);

  this->route(env, dc, "level");
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  // Attack 480 сэмплов: шаг ровно 1/480 на сэмпл, ступенек на границах шагов нет
  CHECK(maxJump(this->run(400)) == doctest::Approx(1.0f / 480.0f).epsilon(0.05));
}

TEST_CASE_FIXTURE(ModulationFixture, "Значения модуляции не зависят от размера блока: сравнение до бита (D-008)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.02f, 0.05f, 0.5f, 0.1f);
  const ModuleId lfo = this->add("lfo", voice.generator);
  const ModuleId mainLfo = this->add("lfo");

  this->set(lfo, "rate", 7.0f);
  this->set(mainLfo, "rate", 11.0f);
  this->route(env, voice.dc, "level", 0.5f);
  this->route(lfo, voice.dc, "level", 0.25f);
  this->route(mainLfo, voice.dc, "level", 0.125f);
  this->route(voice.generator, voice.dc, "level", 0.125f, "velocity");
  this->apply();

  const Samples reference = this->renderPhrase(256);

  REQUIRE(peak(reference) > 0.1f);

  for (const uint32_t blockSize : { 1u, 7u, 32u, 33u, 64u, 1000u, 8192u }) {
    CAPTURE(blockSize);
    CHECK((this->renderPhrase(blockSize) == reference));
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Настоящие модули с модуляцией не зависят от размера блока: сравнение до бита (D-008)") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->add("oscillator", generator);
  const ModuleId filter = this->add("filter", generator);
  const ModuleId amp = this->add("amp", generator);
  const ModuleId env = this->addEnv(generator, 0.02f, 0.05f, 0.5f, 0.1f);
  const ModuleId lfo = this->add("lfo", generator);

  this->set(lfo, "rate", 17.0f);
  this->set(amp, "gain", 0.25f);
  this->set(amp, "velocity", 0.5f);
  this->set(osc, "level", 0.5f);
  this->connect(osc, filter);
  this->connect(filter, amp);
  this->connect(amp, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->route(env, amp, "gain", 0.75f);
  this->route(lfo, amp, "velocity", 0.5f);
  this->route(lfo, osc, "level", 0.25f);
  this->route(lfo, osc, "fine", 0.5f);
  this->route(env, filter, "cutoff", 0.5f);
  this->apply();

  const Samples reference = this->renderPhrase(256);

  REQUIRE(peak(reference) > 0.01f);

  for (const uint32_t blockSize : { 1u, 7u, 33u, 1000u }) {
    CAPTURE(blockSize);
    CHECK((this->renderPhrase(blockSize) == reference));
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Голосовой модулятор идёт на каждый голос своим значением (D-004)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.01f, 10.0f, 1.0f, 10.0f);

  this->route(env, voice.dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(240);
  this->engine.noteOn(note(KEY_C5), 1.0f);

  // Первый голос прошёл 480 сэмплов, второй 240
  CHECK(this->run(240).back() == doctest::Approx(1.5f).epsilon(0.01));
}

TEST_CASE_FIXTURE(ModulationFixture, "Velocity — источник модуляции своего голоса (D-027)") {
  const Voice voice = this->addDcGenerator();

  this->route(voice.generator, voice.dc, "level", 1.0f, "velocity");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 0.25f);
  CHECK(this->settle() == doctest::Approx(0.25f));

  this->engine.noteOn(note(KEY_C5), 0.5f);
  CHECK(this->settle() == doctest::Approx(0.75f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Голосовой источник на глобальном параметре даёт значение последней ноты (D-004)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId mainDc = this->add("test.dc");
  const ModuleId mixer = this->add("amp");

  this->set(mixer, "velocity", 0.0f);
  this->connect(mainDc, mixer);
  this->connect(mixer, this->model->getOutputId());
  REQUIRE(this->model->apply(Disconnect{ { voice.generator, "out", this->model->getOutputId(), "in" } }).has_value());

  // Без соединения с выходом генератор не получил бы нот. Его голос не слышен: уровень 0
  this->connect(voice.generator, mixer);
  this->route(voice.generator, mainDc, "level", 1.0f, "velocity");
  this->apply();

  CHECK(this->settle() == 0.0f);

  this->engine.noteOn(note(KEY_A4), 0.75f);
  CHECK(this->settle() == doctest::Approx(0.75f));

  this->engine.noteOn(note(KEY_C5), 0.25f);
  CHECK(this->settle() == doctest::Approx(0.25f));

  // Последняя нота ушла: значение берёт следующий по времени голос
  this->engine.noteOff(note(KEY_C5));
  this->run(SETTLE_FRAMES);
  CHECK(this->settle() == doctest::Approx(0.75f));

  // Голосов нет: значение равно 0
  this->engine.noteOff(note(KEY_A4));
  this->run(SETTLE_FRAMES);
  CHECK(this->settle() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Локальный модулятор на глобальном параметре следует за последней нотой (D-004)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 0.5f, 0.01f);
  const ModuleId mainDc = this->add("test.dc");
  const ModuleId mixer = this->add("amp");

  this->set(mixer, "velocity", 0.0f);
  this->connect(mainDc, mixer);
  this->connect(voice.generator, mixer);
  this->connect(mixer, this->model->getOutputId());
  REQUIRE(this->model->apply(Disconnect{ { voice.generator, "out", this->model->getOutputId(), "in" } }).has_value());
  this->route(env, mainDc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle(30) == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Глобальный модулятор даёт всем голосам одно значение (D-004)") {
  const Voice voice = this->addDcGenerator();

  this->route(this->addConstantLfo(), voice.dc, "level", 0.25f);
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(100);
  this->engine.noteOn(note(KEY_C5), 1.0f);

  CHECK(this->settle() == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Голос видит глобальный модулятор в том же шаге (D-009)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId lfo = this->add("lfo");

  this->set(voice.dc, "level", 0.5f);
  this->set(lfo, "rate", 50.0f);
  this->route(lfo, voice.dc, "level", 0.5f);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  const float phase = 2.0f * std::numbers::pi_v<float> * 50.0f * 320.0f / SAMPLE_RATE;

  CHECK(this->run(321).back() == doctest::Approx(0.5f + 0.5f * std::sin(phase)).epsilon(0.001));
}

TEST_CASE_FIXTURE(ModulationFixture, "LFO главного графа не перезапускается нотой, LFO голоса перезапускается (D-048)") {
  SUBCASE("Глобальный") {
    const ModuleId dc = this->addMainDc(0.5f);
    const ModuleId lfo = this->add("lfo");

    this->set(lfo, "rate", 5.0f);
    this->route(lfo, dc, "level", 0.5f);
    this->apply();

    // Четверть периода: вершина синуса
    const float before = this->run(2401).back();

    this->engine.noteOn(note(KEY_A4), 1.0f);

    CHECK(before == doctest::Approx(1.0f).epsilon(0.01));
    CHECK(this->run(33).back() == doctest::Approx(1.0f).epsilon(0.01));
  }

  SUBCASE("В голосе") {
    const Voice voice = this->addDcGenerator();
    const ModuleId lfo = this->add("lfo", voice.generator);

    this->set(voice.dc, "level", 0.5f);
    this->set(lfo, "rate", 5.0f);
    this->route(lfo, voice.dc, "level", 0.5f);
    this->apply();

    this->engine.noteOn(note(KEY_A4), 1.0f);
    this->run(2400); // Вершина синуса

    this->engine.noteOn(note(KEY_C5), 1.0f);

    // Второй голос начинает с фазы 0, первый продолжает: вместе не 2, а около 1,5
    CHECK(this->run(1).back() == doctest::Approx(1.5f).epsilon(0.01));
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Общий ENV: настройки одни, состояние у каждого голоса своё (D-005)") {
  const Voice first = this->addDcGenerator();
  const Voice second = this->addDcGenerator();
  const ModuleId env = this->addEnv(std::nullopt, 0.01f, 10.0f, 1.0f, 10.0f);

  this->route(env, first.dc, "level");
  this->route(env, second.dc, "level", 0.5f);
  this->apply();

  // Оба генератора получают ноту и считают свой экземпляр
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(240);

  // Вторая нота: у каждого генератора новый голос со своей огибающей
  this->engine.noteOn(note(KEY_C5), 1.0f);

  CHECK(this->run(240).back() == doctest::Approx(1.5f + 0.75f).epsilon(0.01));
}

TEST_CASE_FIXTURE(ModulationFixture, "Глобальный экземпляр общего ENV перезапускается каждой нотой и живёт последней (D-005)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.01f, 10.0f, 1.0f, 0.01f);

  this->route(env, dc, "level");
  this->apply();

  CHECK(this->settle() == 0.0f);

  // Глобальный экземпляр начинает с границы сетки: это меньше одного шага
  this->engine.noteOn(note(KEY_A4), 1.0f);
  CHECK(this->run(481 + CHUNK_FRAMES).back() == doctest::Approx(1.0f));

  this->engine.noteOn(note(KEY_C5), 1.0f);
  CHECK(this->run(241).back() == doctest::Approx(0.5f).epsilon(0.05));

  this->run(480);

  // Отпускание старой ноты последнюю не трогает
  this->engine.noteOff(note(KEY_A4));
  CHECK(this->run(480).back() == doctest::Approx(1.0f));

  this->engine.noteOff(note(KEY_C5));
  CHECK(this->run(600).back() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Педаль держит глобальный экземпляр общего ENV (D-005, D-058)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.001f, 10.0f, 1.0f, 0.01f);

  this->route(env, dc, "level");
  this->apply();

  this->engine.setSustain(0, true);
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(200);
  this->engine.noteOff(note(KEY_A4));

  CHECK(this->run(600).back() == doctest::Approx(1.0f));

  // Педаль другого канала к этой ноте отношения не имеет
  this->engine.setSustain(1, false);
  CHECK(this->run(600).back() == doctest::Approx(1.0f));

  this->engine.setSustain(0, false);
  CHECK(this->run(600).back() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Отпускание педали не трогает глобальный экземпляр нажатой последней ноты (D-058)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.001f, 10.0f, 1.0f, 0.01f);

  this->route(env, dc, "level");
  this->apply();
  this->engine.setSustain(0, true);

  SUBCASE("Педаль держала прошлую ноту, а последняя пришла позже") {
    this->engine.noteOn(note(KEY_A4), 1.0f);
    this->run(200);
    this->engine.noteOff(note(KEY_A4));
    this->engine.noteOn(note(KEY_C5), 1.0f);
  }

  SUBCASE("Под педалью отпустили старую ноту, а не последнюю") {
    this->engine.noteOn(note(KEY_A4), 1.0f);
    this->engine.noteOn(note(KEY_C5), 1.0f);
    this->run(200);
    this->engine.noteOff(note(KEY_A4));
  }

  this->run(200);
  this->engine.setSustain(0, false);

  CHECK(this->run(600).back() == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Педаль без отпущенной клавиши глобальный экземпляр не отпускает (D-058)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.001f, 10.0f, 1.0f, 0.01f);

  this->route(env, dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(200);
  this->engine.setSustain(0, true);
  this->engine.setSustain(0, false);

  CHECK(this->run(600).back() == doctest::Approx(1.0f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Макрос работает как глобальный источник: значение идёт в параметр голоса (D-055)") {
  const Voice voice = this->addDcGenerator();

  this->engine.setSlotTargets({ { MACROS_MODULE_ID, 2 } });
  REQUIRE(this->model->apply(AddRoute{ { { MACROS_MODULE_ID, "macro3" }, { voice.dc, "level" }, 1.0f } }).has_value());
  REQUIRE(this->model->apply(SetParameter{ MACROS_MODULE_ID, "macro3", 0.25f }).has_value());
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  CHECK(this->settle(30) == doctest::Approx(0.25f));

  this->engine.setSlot(0, 0.75f);
  CHECK(this->settle(30) == doctest::Approx(0.75f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Choke отпускает глобальный экземпляр общего ENV") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.001f, 10.0f, 1.0f, 0.01f);

  this->route(env, dc, "level");
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(200);
  this->engine.choke(note(KEY_A4));

  CHECK(this->run(600).back() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Цепочка общих модуляторов получает состояние на голос (D-005)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId source = this->addEnv(std::nullopt, 0.001f, 0.001f, 0.25f, 0.01f);
  const ModuleId env = this->addEnv(std::nullopt, 0.001f, 0.001f, 0.0f, 0.01f);

  this->route(source, env, "sustain");
  this->route(env, voice.dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle(30) == doctest::Approx(0.25f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Состояние общего модулятора есть только у генераторов-потребителей (D-005)") {
  const Voice consumer = this->addDcGenerator();
  const Voice other = this->addDcGenerator();
  const ModuleId env = this->add("env");
  const ModuleId lfo = this->add("lfo");
  ModuleSlots slots;

  this->route(env, consumer.dc, "level");
  this->route(lfo, consumer.dc, "level");

  syncModuleSlots(*this->model, slots, SAMPLE_RATE);

  CHECK(slots.at(consumer.generator).generator->getVoiceSlots()[0].contains(env));
  CHECK_FALSE(slots.at(other.generator).generator->getVoiceSlots()[0].contains(env));

  // Глобальный LFO не копируется в голоса (D-048)
  CHECK_FALSE(slots.at(consumer.generator).generator->getVoiceSlots()[0].contains(lfo));

  SUBCASE("Без маршрута состояние исчезает") {
    REQUIRE(this->model->apply(RemoveRoute{ { env, "out" }, { consumer.dc, "level" } }).has_value());
    syncModuleSlots(*this->model, slots, SAMPLE_RATE);

    CHECK_FALSE(slots.at(consumer.generator).generator->getVoiceSlots()[0].contains(env));
  }

  SUBCASE("Вместе с модулем состояние исчезает") {
    REQUIRE(this->model->apply(RemoveModule{ env }).has_value());
    syncModuleSlots(*this->model, slots, SAMPLE_RATE);

    CHECK_FALSE(slots.at(consumer.generator).generator->getVoiceSlots()[0].contains(env));
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "ENV держит голос, пока идёт release (D-051)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 0.1f);

  this->route(env, voice.dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(480);
  this->engine.noteOff(note(KEY_A4));

  const Samples tail = this->run(4800 + SETTLE_FRAMES);

  // Спад линейный: половина через половину времени
  CHECK(tail[2400] == doctest::Approx(0.5f).epsilon(0.01));
  CHECK(tail[4000] > 0.0f);
  CHECK(tail.back() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Какие ENV держат голос после отпускания (D-051)") {
  const Voice voice = this->addDcGenerator();

  this->set(voice.dc, "level", 1.0f);

  ModuleId env = 0;

  const auto play = [this] {
    this->apply();
    this->engine.noteOn(note(KEY_A4), 1.0f);
    this->run(480);
    this->engine.noteOff(note(KEY_A4));
    this->run(SETTLE_FRAMES);

    return this->run(100).back();
  };

  SUBCASE("ENV без маршрутов не держит: затухание за 10 мс") {
    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);

    CHECK(play() == 0.0f);
  }

  SUBCASE("ENV на чужом параметре, не влияющем на звук голоса, не держит") {
    const ModuleId silent = this->add("test.dc", voice.generator);

    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, silent, "level");

    CHECK(play() == 0.0f);
  }

  SUBCASE("ENV на глобальном параметре голос не держит") {
    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);

    const ModuleId mainDc = this->add("test.dc");

    this->connect(mainDc, this->model->getOutputId());
    this->route(env, mainDc, "stepped");

    CHECK(play() == 0.0f);
  }

  SUBCASE("ENV через другой модулятор держит") {
    const ModuleId pass = this->add("test.pass", voice.generator);

    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, pass, "value", 0.0f);
    this->route(pass, voice.dc, "curved", 0.0f);

    CHECK(play() == 1.0f);
  }

  SUBCASE("ENV на отключённом модуле не держит (D-040)") {
    const ModuleId silent = this->add("test.dc", voice.generator);

    this->connect(silent, this->getVoiceOutput(voice.generator));
    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, silent, "level");
    this->setEnabled(silent, false);

    CHECK(play() == 0.0f);
  }

  SUBCASE("ENV другого генератора через глобальный модулятор не держит") {
    const Voice other = this->addDcGenerator();
    const ModuleId pass = this->add("test.pass");

    env = this->addEnv(other.generator, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, pass, "value", 0.0f);
    this->route(pass, voice.dc, "curved", 0.0f);

    CHECK(play() == 0.0f);
  }

  SUBCASE("Отключённый ENV не держит (D-040)") {
    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, voice.dc, "curved", 0.0f);
    this->setEnabled(env, false);

    CHECK(play() == 0.0f);
  }

  SUBCASE("Общий ENV держит") {
    env = this->addEnv(std::nullopt, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, voice.dc, "curved", 0.0f);

    CHECK(play() == 1.0f);
  }

  SUBCASE("Локальный ENV через глобальный модулятор держит") {
    const ModuleId pass = this->add("test.pass");

    env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);
    this->route(env, pass, "value", 0.0f);
    this->route(pass, voice.dc, "curved", 0.0f);

    CHECK(play() == 1.0f);
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Голос заканчивается затуханием, а не обрывом, даже если ENV не на громкости (D-051)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 0.02f);

  this->set(voice.dc, "level", 1.0f);
  this->route(env, voice.dc, "curved", 0.0f);
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(100);
  this->engine.noteOff(note(KEY_A4));

  const Samples tail = this->run(960 + SETTLE_FRAMES);

  CHECK(tail[900] == 1.0f); // ENV ещё держит
  CHECK(tail.back() == 0.0f);
  CHECK(maxJump(tail) == doctest::Approx(1.0f / 480.0f).epsilon(0.05));
}

TEST_CASE_FIXTURE(ModulationFixture, "Страховка освобождает тихий отпущенный голос через 0,5 с, а удерживаемый не трогает (D-051)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 10.0f);
  const ModuleId mainDc = this->add("test.dc");

  // Значение последнего голоса видно на глобальном параметре, пока голос жив.
  // ENV в маршруте с нулевой глубиной держит голос, сигнал голоса остаётся тихим
  this->connect(mainDc, this->model->getOutputId());
  this->route(env, voice.dc, "level", 0.0f);
  this->route(voice.generator, mainDc, "level", 1.0f, "velocity");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 0.5f);
  this->run(24000);

  // Удерживаемый голос через 1 с тишины жив
  CHECK(this->run(24000).back() == doctest::Approx(0.5f));

  this->engine.noteOff(note(KEY_A4));

  CHECK(this->run(23000).back() == doctest::Approx(0.5f));

  // Порог пройден на границе сетки, затем затухание 10 мс
  this->run(1000 + SETTLE_FRAMES);
  CHECK(this->settle() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Счётчик тишины не переходит на следующую ноту (D-051)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 10.0f);

  this->set(voice.generator, "polyphony", 1.0f);
  this->route(voice.generator, voice.dc, "level", 1.0f, "velocity");
  this->route(env, voice.dc, "curved", 0.0f);
  this->apply();

  // Тихая нота: страховка освобождает голос
  this->engine.noteOn(note(KEY_A4), 1e-6f);
  this->run(480);
  this->engine.noteOff(note(KEY_A4));
  this->run(24000 + SETTLE_FRAMES);

  // Тот же слот с громкой нотой: после отпускания она продолжает звучать
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(480);
  this->engine.noteOff(note(KEY_A4));

  CHECK(this->run(4800).back() == 1.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Страховка не трогает слышный отпущенный голос (D-051)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 10.0f);

  this->route(env, voice.dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(480);
  this->engine.noteOff(note(KEY_A4));

  CHECK(this->run(48000).back() == doctest::Approx(0.9f).epsilon(0.01));
}

TEST_CASE_FIXTURE(ModulationFixture, "Удерживаемый аккорд не рвётся из-за хвостов: забирается отпущенный голос (D-042)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);

  this->set(voice.generator, "polyphony", 2.0f);
  this->route(env, voice.dc, "level");
  this->route(voice.generator, voice.dc, "curved", 0.0f, "velocity");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(100);
  this->engine.noteOn(note(KEY_C5), 1.0f);
  this->run(100);
  this->engine.noteOff(note(KEY_C5)); // Младший голос отпущен, старший удерживается
  this->run(100);
  this->engine.noteOn(note(60), 1.0f);

  // Удерживаемый звучит как раньше, новый набрал уровень, отпущенный затих
  CHECK(this->run(100 + SETTLE_FRAMES).back() == doctest::Approx(2.0f).epsilon(0.01));
}

TEST_CASE_FIXTURE(ModulationFixture, "Модуляторы считаются в порядке зависимостей, а не по ID (D-010)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId target = this->add("test.pass"); // ID меньше, чем у источника
  const ModuleId source = this->add("test.pass");

  this->set(source, "value", 0.5f);
  this->route(source, target, "value");
  this->route(target, dc, "level");
  this->apply();

  // Значение дошло за один шаг, без задержки на кусок
  CHECK(this->run(CHUNK_FRAMES + 1).back() == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Цикл разрывается на последнем созданном маршруте (D-010)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId first = this->add("test.pass");
  const ModuleId second = this->add("test.pass");

  this->set(first, "value", 0.25f);
  this->set(second, "value", 0.125f);
  this->route(second, dc, "level");

  Samples expected;

  SUBCASE("Замыкает маршрут second → first") {
    this->route(first, second, "value");
    this->route(second, first, "value", 0.5f);

    // Начало: first = 0,25 + 0,5 × 0 = 0,25, second = 0,125 + 0,25 = 0,375
    // Шаг 1: first = 0,25 + 0,5 × 0,375 = 0,4375, second = 0,5625
    // Шаг 2: first = 0,25 + 0,5 × 0,5625 = 0,53125, second = 0,65625
    expected = { 0.375f, 0.5625f, 0.65625f };
  }

  SUBCASE("Замыкает маршрут first → second") {
    this->route(second, first, "value", 0.5f);
    this->route(first, second, "value");

    // Начало: second = 0,125 + 0 = 0,125, first = 0,25 + 0,0625 = 0,3125
    // Шаг 1: second = 0,125 + 0,3125 = 0,4375, first = 0,46875
    // Шаг 2: second = 0,125 + 0,46875 = 0,59375
    expected = { 0.125f, 0.4375f, 0.59375f };
  }

  this->apply();

  const Samples samples = this->run(2 * CHUNK_FRAMES + 1);

  CHECK(samples[0] == doctest::Approx(expected[0]));
  CHECK(samples[CHUNK_FRAMES] == doctest::Approx(expected[1]));
  CHECK(samples[2 * CHUNK_FRAMES] == doctest::Approx(expected[2]));
}

TEST_CASE_FIXTURE(ModulationFixture, "Модулятор может модулировать сам себя значением прошлого шага (D-010)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId pass = this->add("test.pass");

  this->set(pass, "value", 0.25f);
  this->route(pass, pass, "value", 0.5f);
  this->route(pass, dc, "level");
  this->apply();

  const Samples samples = this->run(2 * CHUNK_FRAMES + 1);

  CHECK(samples[0] == doctest::Approx(0.25f));
  CHECK(samples[CHUNK_FRAMES] == doctest::Approx(0.375f));
  CHECK(samples[2 * CHUNK_FRAMES] == doctest::Approx(0.4375f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Цикл в голосе разрывается так же и начинается с нуля на каждой ноте (D-010)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId pass = this->add("test.pass", voice.generator);

  this->set(pass, "value", 0.25f);
  this->route(pass, pass, "value", 0.5f);
  this->route(pass, voice.dc, "level");
  this->apply();

  const auto play = [this] {
    this->engine.noteOn(note(KEY_A4), 1.0f);

    const Samples samples = this->run(2 * CHUNK_FRAMES + 1);

    this->engine.choke(note(KEY_A4));
    this->run(SETTLE_FRAMES + (CHUNK_FRAMES - 1));

    return samples;
  };

  const Samples first = play();

  // Вклад прошлой ноты не попал в начальное значение
  CHECK(first[0] == doctest::Approx(0.25f));
  CHECK(first[CHUNK_FRAMES] == doctest::Approx(0.375f));
  CHECK(play() == first);
}

TEST_CASE_FIXTURE(ModulationFixture, "Порядок разрыва цикла переживает сохранение и загрузку (D-010)") {
  const ModuleId dc = this->addMainDc();
  const ModuleId first = this->add("test.pass");
  const ModuleId second = this->add("test.pass");

  this->set(first, "value", 0.25f);
  this->set(second, "value", 0.125f);
  this->route(second, first, "value", 0.5f);
  this->route(first, second, "value");
  this->route(second, dc, "level");
  this->apply();

  const Samples before = this->run(320);

  auto loaded = loadModel(&this->registry, saveModel(*this->model));

  REQUIRE(loaded.has_value());

  Engine other;

  other.prepare(SAMPLE_RATE);
  other.applyModel(*loaded);

  Samples left(320);
  Samples right(320);

  other.process(left.data(), right.data(), 320);

  CHECK(left == before);
}

TEST_CASE_FIXTURE(ModulationFixture, "Глобальный модулятор видит голос с задержкой на шаг, а голос глобальный — сразу") {
  const Voice voice = this->addDcGenerator();
  const ModuleId global = this->add("test.pass");

  this->route(voice.generator, global, "value", 1.0f, "velocity");
  this->route(global, voice.dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 0.5f);

  const Samples samples = this->run(2 * CHUNK_FRAMES + 1);

  CHECK(samples[CHUNK_FRAMES] == 0.0f);
  CHECK(samples[2 * CHUNK_FRAMES] == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Отключённый модулятор даёт 0 и замирает (D-040)") {
  const ModuleId dc = this->addMainDc(0.25f);
  const ModuleId lfo = this->addConstantLfo();

  this->route(lfo, dc, "level", 0.5f);
  this->setEnabled(lfo, false);
  this->apply();

  CHECK(this->settle() == 0.25f);

  this->setEnabled(lfo, true);
  this->apply();

  CHECK(this->settle() == doctest::Approx(0.75f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Маршрут на параметр отключённого модуля не действует (D-040)") {
  const Voice voice = this->addDcGenerator();

  this->set(voice.dc, "level", 0.25f);
  this->route(this->addConstantLfo(), voice.dc, "level", 0.5f);
  this->setEnabled(voice.dc, false);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Источник в отключённом генераторе даёт 0") {
  const Voice voice = this->addDcGenerator();
  const ModuleId dc = this->addMainDc(0.25f);

  this->route(voice.generator, dc, "level", 0.5f, "velocity");
  this->setEnabled(voice.generator, false);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle() == 0.25f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Источник в генераторе без пути к выходу даёт 0") {
  const ModuleId generator = this->add("generator");
  const ModuleId dc = this->addMainDc(0.25f);

  this->route(this->addConstantLfo(generator), dc, "level", 0.5f);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle() == 0.25f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Неизвестный источник даёт 0, маршрут на неизвестный модуль не действует (D-021)") {
  REQUIRE(this->registry.add(&TEST_UNKNOWN_DC).has_value());
  REQUIRE(this->registry.add(&TEST_UNKNOWN_PASS).has_value());

  const Voice voice = this->addDcGenerator();
  const ModuleId source = this->add("test.unknownPass");
  const ModuleId target = this->add("test.unknownDc", voice.generator);
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 1.0f);

  this->set(voice.dc, "level", 0.25f);
  this->set(source, "value", 1.0f);
  this->connect(target, this->getVoiceOutput(voice.generator));
  this->route(source, voice.dc, "level", 0.5f);
  this->route(env, target, "level");

  ModuleRegistry withoutUnknown;

  REQUIRE(addAllModules(withoutUnknown).has_value());
  REQUIRE(withoutUnknown.add(&TEST_DC).has_value());
  REQUIRE(withoutUnknown.add(&TEST_PASS).has_value());

  const auto loaded = loadModel(&withoutUnknown, saveModel(*this->model));

  REQUIRE(loaded.has_value());

  this->engine.applyModel(*loaded);
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle() == 0.25f);

  // ENV стоит только на заглушке, поэтому голос не держит (D-051)
  this->engine.noteOff(note(KEY_A4));
  this->run(SETTLE_FRAMES);

  CHECK(this->settle() == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Модулятор без маршрутов до исполняемых модулей не считается") {
  const Voice voice = this->addDcGenerator();
  const ModuleId dc = this->add("test.dc"); // Сигнал никуда не идёт
  const ModuleId env = this->add("env");
  const ModuleId lfo = this->add("lfo");

  this->route(lfo, dc, "level");
  this->route(env, dc, "level");
  this->apply();

  // Без маршрутов к voice.dc глобальный LFO ни на что не влияет и не должен мешать звуку
  this->set(voice.dc, "level", 0.25f);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->settle() == 0.25f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Новая нота начинается с начальных значений, а не со значений прошлой ноты") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.01f, 10.0f, 1.0f, 0.001f);

  this->set(voice.generator, "polyphony", 1.0f);
  this->route(env, voice.dc, "level");
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(1000);
  this->engine.choke(note(KEY_A4));
  this->run(SETTLE_FRAMES);
  this->engine.noteOn(note(KEY_A4), 1.0f);

  const Samples samples = this->run(64);

  CHECK(samples[0] == 0.0f);
  CHECK(samples[1] == doctest::Approx(1.0f / 480.0f).epsilon(0.05));
  CHECK(maxJump(samples) < 1.1f / 480.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Нота между границами сетки начинается в свой сэмпл и догоняет сетку (D-008)") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.01f, 10.0f, 1.0f, 1.0f);

  this->route(env, voice.dc, "level");
  this->apply();

  this->run(CHUNK_FRAMES + 11);
  this->engine.noteOn(note(KEY_A4), 1.0f);

  const Samples samples = this->run(200);

  CHECK(samples[0] == 0.0f);

  for (size_t i = 1; i < samples.size(); i++) {
    CAPTURE(i);
    CHECK(samples[i] == doctest::Approx((float)i / 480.0f).epsilon(0.001));
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Отпускание в середине шага применяется на границе сетки") {
  const Voice voice = this->addDcGenerator();
  const ModuleId env = this->addEnv(voice.generator, 0.001f, 0.001f, 1.0f, 0.1f);

  this->route(env, voice.dc, "level");
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(CHUNK_FRAMES * 8 + 5);
  this->engine.noteOff(note(KEY_A4));

  const Samples samples = this->run(CHUNK_FRAMES * 2);

  CHECK(samples[CHUNK_FRAMES - 5 - 1] == 1.0f);
  CHECK(samples[CHUNK_FRAMES * 2 - 1] < 1.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Изменение базы сглаживается ~10 мс (D-025)") {
  const ModuleId dc = this->addMainDc(0.25f);

  this->apply();
  this->settle();
  this->engine.setParameter(dc, 0, 0.75f);

  const Samples samples = this->run(960);

  // Уже идёт к цели, щелчка нет, за 10 мс и шаг сетки дошло
  CHECK(samples[0] == doctest::Approx(0.25f).epsilon(0.05));
  CHECK(samples[240] == doctest::Approx(0.5f).epsilon(0.05));
  CHECK(samples[FADE_FRAMES + CHUNK_FRAMES] == 0.75f);
  CHECK(samples.back() == 0.75f);
  CHECK(maxJump(samples) == doctest::Approx(0.5f / 480.0f).epsilon(0.05));
}

TEST_CASE_FIXTURE(ModulationFixture, "База сглаживается и в голосах, и в логарифмических параметрах") {
  const Voice voice = this->addDcGenerator();

  this->set(voice.dc, "level", 0.25f);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->engine.noteOn(note(KEY_C5), 1.0f);
  this->settle();
  this->engine.setParameter(voice.dc, 0, 0.5f);

  CHECK(maxJump(this->run(960)) == doctest::Approx(0.5f / 480.0f).epsilon(0.05));
  CHECK(this->settle() == 1.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Нота во время сглаживания начинается с текущей базы, а не с устаревшей") {
  const Voice voice = this->addDcGenerator();

  this->set(voice.dc, "level", 0.25f);
  this->apply();
  this->engine.setParameter(voice.dc, 0, 0.75f);
  this->run(FADE_FRAMES + 2 * CHUNK_FRAMES);
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->run(1).back() == 0.75f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Ступенчатая база меняется сразу, без промежуточных ступеней (D-025)") {
  const ModuleId dc = this->addMainDc();

  this->set(dc, "view", 2.0f);
  this->apply();
  this->run(CHUNK_FRAMES);
  this->engine.setParameter(dc, 2, 4.0f);

  const Samples samples = this->run(CHUNK_FRAMES * 3);

  for (const float sample : samples) {
    CHECK(sample == 4.0f);
  }
}

TEST_CASE_FIXTURE(ModulationFixture, "Смена базы складывается с модуляцией (D-025)") {
  const ModuleId dc = this->addMainDc(0.25f);

  this->route(this->addConstantLfo(), dc, "level", 0.25f);
  this->apply();
  this->settle();
  this->engine.setParameter(dc, 0, 0.5f);
  this->run(FADE_FRAMES + 2 * CHUNK_FRAMES);

  CHECK(this->settle() == doctest::Approx(0.75f));
}

TEST_CASE_FIXTURE(ModulationFixture, "Изменение неизвестного параметра игнорируется") {
  const ModuleId dc = this->addMainDc(0.25f);

  this->apply();
  this->engine.setParameter(9999, 0, 1.0f);
  this->engine.setParameter(dc, 99, 1.0f);

  CHECK(this->settle() == 0.25f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Неверный темп заменяется темпом по умолчанию (D-050)") {
  const ModuleId dc = this->addMainDc(0.5f);
  const ModuleId lfo = this->add("lfo");

  this->set(lfo, "wave", 2.0f);
  this->set(lfo, "sync", 1.0f);
  this->route(lfo, dc, "level", 0.5f);
  this->apply();

  const auto render = [this](float tempo) {
    this->engine.setTempo(tempo);
    this->engine.reset();

    return this->run(4800);
  };

  const Samples reference = render(DEFAULT_TEMPO);

  CHECK(render(0.0f) == reference);
  CHECK(render(-10.0f) == reference);
  CHECK(render(240.0f) != reference);
}

TEST_CASE_FIXTURE(ModulationFixture, "Reset отпускает последнюю ноту") {
  const ModuleId dc = this->addMainDc();
  const ModuleId env = this->addEnv(std::nullopt, 0.001f, 10.0f, 1.0f, 0.01f);

  this->route(env, dc, "level");
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(200);
  this->engine.reset();

  CHECK(this->settle(10) == 0.0f);
}

TEST_CASE_FIXTURE(ModulationFixture, "Reset возвращает модуляторы к началу") {
  const ModuleId dc = this->addMainDc(0.5f);
  const ModuleId lfo = this->add("lfo");

  this->set(lfo, "rate", 5.0f);
  this->route(lfo, dc, "level", 0.5f);
  this->apply();

  const Samples first = this->run(4800);

  this->engine.reset();

  CHECK(this->run(1).back() == first[0]);
}

TEST_CASE_FIXTURE(ModulationFixture, "Модуляция не выделяет память (D-023)") {
  const Voice first = this->addDcGenerator();
  const Voice second = this->addDcGenerator();
  const ModuleId localEnv = this->addEnv(first.generator, 0.002f, 0.002f, 0.5f, 0.002f);
  const ModuleId localLfo = this->add("lfo", first.generator);
  const ModuleId sharedEnv = this->addEnv(std::nullopt, 0.002f, 0.002f, 0.5f, 0.002f);
  const ModuleId globalLfo = this->add("lfo");
  const ModuleId mainDc = this->addMainDc();

  this->set(first.generator, "polyphony", 4.0f);
  this->route(localEnv, first.dc, "level");
  this->route(localLfo, first.dc, "curved", 0.25f);
  this->route(sharedEnv, first.dc, "stepped");
  this->route(sharedEnv, second.dc, "level");
  this->route(globalLfo, second.dc, "curved", 0.25f);
  this->route(first.generator, mainDc, "level", 0.5f, "velocity");
  this->route(sharedEnv, mainDc, "curved");
  this->apply();

  Samples left(256);
  Samples right(256);

  const size_t before = getAllocationCount();

  for (int block = 0; block < 200; block++) {
    const int16_t key = (int16_t)(40 + block % 40);

    this->engine.noteOn(note(key), 1.0f);
    this->engine.process(left.data(), right.data(), 17);
    this->engine.noteOn(note((int16_t)(key + 1)), 0.5f);
    this->engine.setParameter(first.dc, 0, block % 2 == 0 ? 0.5f : 0.25f);
    this->engine.process(left.data(), right.data(), 256);
    this->engine.noteOff(note(key));
    this->engine.process(left.data(), right.data(), 13);
    this->engine.choke(note((int16_t)(key + 1)));
  }

  this->engine.reset();

  CHECK(getAllocationCount() == before);
}
