#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "../engine/amp/ampProcessor.h"
#include "../engine/builtinModules/builtinModules.h"
#include "../engine/engine/engine.h"
#include "../engine/filter/filterProcessor.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/oscillator/oscillatorProcessor.h"
#include "../engine/preset/preset.h"
#include "../engine/synthModel/synthModel.h"
#include "./allocationCounter.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr uint32_t TEST_FRAMES = 4800;

  using Samples = std::vector<float>;

  /**
   * Записывает размеры и порядок кусков: сам звука не создаёт
   */
  std::vector<uint32_t> s_chunkSizes;

  class RecordingProcessor : public ModuleProcessor {
    public:
      void prepare(float) override {}

      void reset() override {}

      void process(const ProcessChunk& chunk) override {
        s_chunkSizes.push_back(chunk.frames);
      }
  };

  /**
   * Источник с заданной частотой: настоящий осциллятор берёт высоту из ноты,
   * а тестам главного графа нужен сигнал без генератора и голосов
   */
  class ToneProcessor : public ModuleProcessor {
    public:
      void prepare(float sampleRate) override {
        this->oscillator.setSampleRate(sampleRate);
      }

      void reset() override {
        this->oscillator.stopWave();
      }

      void process(const ProcessChunk& chunk) override {
        AudioBuffer& output = *chunk.outputs[0];

        this->oscillator.setWave((WAVE_TYPE)chunk.parameters[0].end);

        for (uint32_t frame = 0; frame < chunk.frames; frame++) {
          this->oscillator.setFreq(chunk.parameters[1].at(frame, chunk.frames));
          this->oscillator.setVolume(chunk.parameters[2].at(frame, chunk.frames));
          this->oscillator.doSample();

          output.left[frame] = this->oscillator.currentSample;
          output.right[frame] = this->oscillator.currentSample;
        }
      }

    private:
      Oscillator oscillator;
  };

  std::unique_ptr<ModuleProcessor> createToneProcessor() {
    return std::make_unique<ToneProcessor>();
  }

  const ModuleDescriptor TEST_TONE = {
    .type = "test.tone",
    .name = "Test tone",
    .outputs = { { "out", "Out" } },
    .parameters = {
      { .key = "wave", .name = "Wave", .minValue = 1.0f, .maxValue = 4.0f, .defaultValue = 3.0f, .isStepped = true },
      { .key = "frequency", .name = "Frequency", .minValue = 20.0f, .maxValue = 20000.0f, .defaultValue = 440.0f },
      { .key = "level", .name = "Level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 0.1f },
    },
    .createProcessor = createToneProcessor,
  };

  std::unique_ptr<ModuleProcessor> createRecordingProcessor() {
    return std::make_unique<RecordingProcessor>();
  }

  const ModuleDescriptor TEST_RECORDER = {
    .type = "test.recorder",
    .name = "Test recorder",
    .outputs = { { "out", "Out" } },
    .createProcessor = createRecordingProcessor,
  };

  /**
   * Модель и движок с настоящими модулями
   */
  struct EngineFixture {
    ModuleRegistry registry;
    std::unique_ptr<SynthModel> model;
    Engine engine;

    EngineFixture() {
      REQUIRE(addAllModules(this->registry).has_value());
      REQUIRE(this->registry.add(&TEST_RECORDER).has_value());
      REQUIRE(this->registry.add(&TEST_TONE).has_value());

      this->model = std::make_unique<SynthModel>(&this->registry);
      this->engine.prepare(SAMPLE_RATE);
    }

    ModuleId add(const std::string& type) {
      const CommandResult result = this->model->apply(AddModule{ type });

      REQUIRE(result.has_value());

      return **result;
    }

    ModuleId addOscillator(float frequency = 440.0f, float level = 0.1f) {
      const ModuleId id = this->add("test.tone");

      this->set(id, "frequency", frequency);
      this->set(id, "level", level);

      return id;
    }

    void set(ModuleId id, const std::string& parameter, float value) {
      REQUIRE(this->model->apply(SetParameter{ id, parameter, value }).has_value());
    }

    void connect(ModuleId from, ModuleId to) {
      REQUIRE(this->model->apply(Connect{ { from, "out", to, "in" } }).has_value());
    }

    void connectToOutput(ModuleId from) {
      this->connect(from, this->model->getOutputId());
    }

    Samples render(uint32_t frames = TEST_FRAMES, uint32_t blockSize = 256) {
      this->engine.applyModel(*this->model);

      return this->renderBlocks(frames, blockSize);
    }

    /**
     * Обрабатывает звук без пересборки плана
     */
    Samples renderBlocks(uint32_t frames, uint32_t blockSize = 256) {
      Samples left(frames);
      Samples right(frames);

      for (uint32_t done = 0; done < frames; done += blockSize) {
        const uint32_t count = std::min(blockSize, frames - done);

        this->engine.process(left.data() + done, right.data() + done, count);
      }

      CHECK(left == right);

      return left;
    }
  };

  float peak(const Samples& samples) {
    float result = 0.0f;

    for (const float sample : samples) {
      result = std::fmax(result, std::fabs(sample));
    }

    return result;
  }

  int countRisingCrossings(const Samples& samples) {
    int crossings = 0;

    for (size_t i = 1; i < samples.size(); i++) {
      if (samples[i - 1] < 0.0f && samples[i] >= 0.0f) {
        crossings++;
      }
    }

    return crossings;
  }

  void checkClose(const Samples& actual, const Samples& expected) {
    REQUIRE(actual.size() == expected.size());

    for (size_t i = 0; i < actual.size(); i++) {
      if (std::fabs(actual[i] - expected[i]) > 1e-5f) {
        CAPTURE(i);
        CHECK(actual[i] == doctest::Approx(expected[i]));
        return;
      }
    }
  }
}

TEST_CASE_FIXTURE(EngineFixture, "Движок без модели молчит") {
  CHECK(peak(this->renderBlocks(TEST_FRAMES)) == 0.0f);
}

TEST_CASE_FIXTURE(EngineFixture, "Пустой граф молчит") {
  CHECK(peak(this->render()) == 0.0f);
}

TEST_CASE_FIXTURE(EngineFixture, "Осциллятор, подключённый к выходу, звучит на заданной частоте") {
  this->connectToOutput(this->addOscillator(1000.0f, 0.5f));

  const Samples samples = this->render();

  // 0.1 секунды при 1000 Гц
  CHECK(std::abs(countRisingCrossings(samples) - 100) <= 1);
  CHECK(peak(samples) == doctest::Approx(0.5f).epsilon(0.05));
}

TEST_CASE_FIXTURE(EngineFixture, "Модуль без соединения с выходом не слышен и не исполняется") {
  this->add("test.recorder");
  this->addOscillator();
  s_chunkSizes.clear();

  CHECK(peak(this->render()) == 0.0f);
  CHECK(s_chunkSizes.empty());
}

TEST_CASE_FIXTURE(EngineFixture, "Сигнал проходит через фильтр по заданной схеме") {
  const ModuleId osc = this->addOscillator(5000.0f, 1.0f);
  const ModuleId filter = this->add("filter");

  this->connect(osc, filter);
  this->connectToOutput(filter);

  SUBCASE("Низкая частота среза подавляет сигнал") {
    this->set(filter, "cutoff", 100.0f);
    CHECK(peak(this->render()) < 0.1f);
  }

  SUBCASE("Высокая частота среза пропускает сигнал") {
    this->set(filter, "cutoff", 20000.0f);
    CHECK(peak(this->render()) > 0.5f);
  }
}

TEST_CASE_FIXTURE(EngineFixture, "Порядок исполнения не зависит от порядка создания модулей") {
  // Фильтр создан раньше своего источника: без сортировки он прочитал бы ещё пустой буфер
  const ModuleId filter2 = this->add("filter");
  const ModuleId filter1 = this->add("filter");
  const ModuleId osc = this->addOscillator(1000.0f, 1.0f);

  this->connect(osc, filter1);
  this->connect(filter1, filter2);
  this->connectToOutput(filter2);

  const Samples reversed = this->render();

  EngineFixture other;
  const ModuleId otherOsc = other.addOscillator(1000.0f, 1.0f);
  const ModuleId otherFilter1 = other.add("filter");
  const ModuleId otherFilter2 = other.add("filter");

  other.connect(otherOsc, otherFilter1);
  other.connect(otherFilter1, otherFilter2);
  other.connectToOutput(otherFilter2);

  CHECK(reversed == other.render());
}

TEST_CASE_FIXTURE(EngineFixture, "Несколько проводов на одном входе суммируются (D-035)") {
  this->connectToOutput(this->addOscillator(300.0f, 0.2f));
  this->connectToOutput(this->addOscillator(700.0f, 0.3f));

  const Samples mixed = this->render();

  EngineFixture first;
  first.connectToOutput(first.addOscillator(300.0f, 0.2f));

  EngineFixture second;
  second.connectToOutput(second.addOscillator(700.0f, 0.3f));

  const Samples firstSamples = first.render();
  const Samples secondSamples = second.render();

  Samples expected(firstSamples.size());

  for (size_t i = 0; i < expected.size(); i++) {
    expected[i] = firstSamples[i] + secondSamples[i];
  }

  checkClose(mixed, expected);
}

TEST_CASE_FIXTURE(EngineFixture, "Один выход питает параллельные цепочки") {
  const ModuleId osc = this->addOscillator(1000.0f, 0.2f);
  const ModuleId filter1 = this->add("filter");
  const ModuleId filter2 = this->add("filter");

  this->connect(osc, filter1);
  this->connect(osc, filter2);
  this->connectToOutput(filter1);
  this->connectToOutput(filter2);

  const Samples parallel = this->render();

  EngineFixture single;
  const ModuleId singleOsc = single.addOscillator(1000.0f, 0.2f);
  const ModuleId singleFilter = single.add("filter");

  single.connect(singleOsc, singleFilter);
  single.connectToOutput(singleFilter);

  Samples expected = single.render();

  for (float& sample : expected) {
    sample *= 2.0f;
  }

  checkClose(parallel, expected);
}

TEST_CASE_FIXTURE(EngineFixture, "Заглушка пропускает сумму входов дальше по цепочке (D-021)") {
  const ModuleId osc1 = this->addOscillator(300.0f, 0.2f);
  const ModuleId osc2 = this->addOscillator(700.0f, 0.3f);
  const ModuleId placeholder = this->add("filter");
  const ModuleId silentPlaceholder = this->add("filter");

  this->connect(osc1, placeholder);
  this->connect(osc2, placeholder);
  this->connectToOutput(placeholder);
  this->connectToOutput(silentPlaceholder);

  // Оба фильтра превращаются в модули неизвестного типа
  auto file = nlohmann::json::parse(saveModel(*this->model));

  for (auto& module : file.at("modules")) {
    if (module.at("id") == placeholder || module.at("id") == silentPlaceholder) {
      module["type"] = "future.chorus";
      module.erase("parameters");
    }
  }

  // Второй провод идёт в другой порт: заглушка сама суммирует разные входы
  for (auto& connection : file.at("connections")) {
    if (connection.at("fromModule") == osc2) {
      connection["toPort"] = "second";
    }
  }

  auto loaded = loadModel(&this->registry, file.dump());
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());
  REQUIRE(loaded->findModule(placeholder)->isStub());
  REQUIRE(loaded->findModule(placeholder)->descriptor->inputs.size() == 2);

  this->engine.applyModel(*loaded);

  const Samples throughStub = this->renderBlocks(TEST_FRAMES);

  EngineFixture direct;
  direct.connectToOutput(direct.addOscillator(300.0f, 0.2f));
  direct.connectToOutput(direct.addOscillator(700.0f, 0.3f));

  checkClose(throughStub, direct.render());
}

TEST_CASE_FIXTURE(EngineFixture, "Отключённый модуль пропускает сумму входов дальше по цепочке (D-040)") {
  const ModuleId osc1 = this->addOscillator(300.0f, 0.2f);
  const ModuleId osc2 = this->addOscillator(700.0f, 0.3f);
  const ModuleId filter = this->add("filter");

  this->set(filter, "cutoff", 20.0f);
  this->connect(osc1, filter);
  this->connect(osc2, filter);
  this->connectToOutput(filter);

  const Samples filtered = this->render();

  REQUIRE(this->model->apply(SetModuleEnabled{ filter, false }).has_value());

  const Samples bypassed = this->render();

  EngineFixture direct;
  direct.connectToOutput(direct.addOscillator(300.0f, 0.2f));
  direct.connectToOutput(direct.addOscillator(700.0f, 0.3f));

  // Первый проход догоняет фазу осцилляторов: граф выше уже отыграл столько же
  direct.render();

  const Samples expected = direct.render();

  checkClose(bypassed, expected);
  CHECK(peak(filtered) < peak(expected) * 0.5f);
}

TEST_CASE_FIXTURE(EngineFixture, "Отключённый источник молчит, остальной граф продолжает звучать (D-040)") {
  const ModuleId muted = this->addOscillator(300.0f, 0.5f);

  this->connectToOutput(muted);

  REQUIRE(this->model->apply(SetModuleEnabled{ muted, false }).has_value());

  CHECK(peak(this->render()) == 0.0f);

  this->connectToOutput(this->addOscillator(1000.0f, 0.5f));

  const Samples audio = this->render();

  CHECK(peak(audio) > 0.1f);
  CHECK(std::abs(countRisingCrossings(audio) - 100) <= 1);
}

TEST_CASE_FIXTURE(EngineFixture, "Отключённый модуль не исполняется (D-040)") {
  const ModuleId recorder = this->add("test.recorder");

  this->connectToOutput(recorder);

  REQUIRE(this->model->apply(SetModuleEnabled{ recorder, false }).has_value());

  s_chunkSizes.clear();
  this->render(320);

  CHECK(s_chunkSizes.empty());

  REQUIRE(this->model->apply(SetModuleEnabled{ recorder, true }).has_value());

  this->render(320);

  CHECK_FALSE(s_chunkSizes.empty());
}

TEST_CASE_FIXTURE(EngineFixture, "Включённый снова модуль продолжает со своего состояния (D-040)") {
  const ModuleId osc = this->addOscillator(1000.0f, 0.5f);

  this->connectToOutput(osc);

  const Samples whole = this->render(1000);

  EngineFixture paused;
  const ModuleId pausedOsc = paused.addOscillator(1000.0f, 0.5f);

  paused.connectToOutput(pausedOsc);

  Samples parts = paused.render(320);

  REQUIRE(paused.model->apply(SetModuleEnabled{ pausedOsc, false }).has_value());
  CHECK(peak(paused.render(320)) == 0.0f);
  REQUIRE(paused.model->apply(SetModuleEnabled{ pausedOsc, true }).has_value());

  const Samples rest = paused.render(680);

  parts.insert(parts.end(), rest.begin(), rest.end());

  CHECK(parts == whole);
}

TEST_CASE_FIXTURE(EngineFixture, "Генератор без нот молчит и не мешает графу") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addOscillator(1000.0f, 0.5f);

  this->connectToOutput(generator);

  CHECK(peak(this->render()) == 0.0f);

  this->connectToOutput(osc);

  CHECK(peak(this->render()) > 0.1f);
}

TEST_CASE_FIXTURE(EngineFixture, "Куски не длиннее 32 сэмплов и идут по общей сетке (D-008)") {
  this->connectToOutput(this->add("test.recorder"));
  this->engine.applyModel(*this->model);

  s_chunkSizes.clear();

  // Блоки разной длины, в том числе короче куска
  for (const uint32_t blockSize : { 10u, 50u, 3u, 100u, 32u, 64u }) {
    this->renderBlocks(blockSize, blockSize);
  }

  uint32_t position = 0;

  for (const uint32_t size : s_chunkSizes) {
    CAPTURE(position);
    CHECK(size > 0);
    CHECK(size <= MAX_CHUNK_FRAMES);

    // Кусок не пересекает границу сетки
    CHECK((position % MAX_CHUNK_FRAMES) + size <= MAX_CHUNK_FRAMES);

    position += size;
  }

  CHECK(position == 10 + 50 + 3 + 100 + 32 + 64);
}

TEST_CASE_FIXTURE(EngineFixture, "Звук не зависит от размера блока хоста") {
  const ModuleId osc = this->addOscillator(1234.0f, 0.5f);
  const ModuleId filter = this->add("filter");

  this->connect(osc, filter);
  this->connectToOutput(filter);

  const Samples reference = this->render(TEST_FRAMES, 256);

  for (const uint32_t blockSize : { 1u, 7u, 32u, 37u, 1024u, 8192u }) {
    EngineFixture other;
    const ModuleId otherOsc = other.addOscillator(1234.0f, 0.5f);
    const ModuleId otherFilter = other.add("filter");

    other.connect(otherOsc, otherFilter);
    other.connectToOutput(otherFilter);

    CAPTURE(blockSize);
    CHECK(other.render(TEST_FRAMES, blockSize) == reference);
  }
}

TEST_CASE_FIXTURE(EngineFixture, "Смена плана не сбрасывает состояние модулей (D-023)") {
  const ModuleId osc = this->addOscillator(1000.0f, 0.5f);
  const ModuleId filter = this->add("filter");

  this->connect(osc, filter);
  this->connectToOutput(filter);

  const Samples whole = this->render(1000);

  EngineFixture split;
  const ModuleId splitOsc = split.addOscillator(1000.0f, 0.5f);
  const ModuleId splitFilter = split.add("filter");

  split.connect(splitOsc, splitFilter);
  split.connectToOutput(splitFilter);

  Samples parts = split.render(400);
  const Samples rest = split.render(600);

  parts.insert(parts.end(), rest.begin(), rest.end());

  CHECK(parts == whole);
}

TEST_CASE_FIXTURE(EngineFixture, "Новые значения параметров применяются вместе с моделью") {
  const ModuleId osc = this->addOscillator(1000.0f, 0.5f);

  this->connectToOutput(osc);

  CHECK(std::abs(countRisingCrossings(this->render()) - 100) <= 1);

  this->set(osc, "frequency", 2000.0f);
  CHECK(std::abs(countRisingCrossings(this->render()) - 200) <= 1);

  this->set(osc, "level", 0.0f);
  CHECK(peak(this->render()) == 0.0f);
}

TEST_CASE_FIXTURE(EngineFixture, "Форма волны меняется параметром") {
  const float frequency = 1000.0f;
  const float level = 0.5f;
  const ModuleId osc = this->addOscillator(frequency, level);

  this->connectToOutput(osc);
  this->set(osc, "wave", (float)WAVE_TYPE::SIN);

  const Samples samples = this->render(100);

  for (size_t i = 0; i < samples.size(); i++) {
    const float expected = level * std::sin(2.0f * std::numbers::pi_v<float> * frequency * (float)i / SAMPLE_RATE);

    CAPTURE(i);
    CHECK(samples[i] == doctest::Approx(expected).epsilon(0.001).scale(1.0));
  }
}

TEST_CASE_FIXTURE(EngineFixture, "Удалённый модуль теряет состояние") {
  const ModuleId osc = this->addOscillator(1000.0f, 0.5f);

  this->connectToOutput(osc);
  this->render(100);

  REQUIRE(this->model->apply(RemoveModule{ osc }).has_value());
  CHECK(peak(this->render()) == 0.0f);

  const ModuleId fresh = this->addOscillator(1000.0f, 0.5f);
  this->connectToOutput(fresh);

  EngineFixture clean;
  clean.connectToOutput(clean.addOscillator(1000.0f, 0.5f));

  CHECK(this->render() == clean.render());
}

TEST_CASE_FIXTURE(EngineFixture, "Состояния удалённых модулей освобождаются, остальные не меняются") {
  ModuleSlots slots;
  const ModuleId osc = this->addOscillator();
  const ModuleId filter = this->add("filter");

  syncModuleSlots(*this->model, slots, SAMPLE_RATE);

  REQUIRE(slots.size() == 3); // С макросами

  const ModuleProcessor* kept = slots.at(filter).processor.get();

  REQUIRE(this->model->apply(RemoveModule{ osc }).has_value());
  syncModuleSlots(*this->model, slots, SAMPLE_RATE);

  CHECK(slots.size() == 2);
  CHECK(slots.at(filter).processor.get() == kept);
}

TEST_CASE_FIXTURE(EngineFixture, "Модуль другого типа с тем же ID получает своё состояние") {
  const ModuleId filter = this->add("filter");

  this->connectToOutput(filter);
  this->render(100);

  // Другая модель: под тем же ID теперь источник сигнала, а не фильтр
  SynthModel other(&this->registry);
  const ModuleId tone = **other.apply(AddModule{ "test.tone" });

  REQUIRE(tone == filter);
  REQUIRE(other.apply(SetParameter{ tone, "level", 0.5f }).has_value());
  REQUIRE(other.apply(Connect{ { tone, "out", other.getOutputId(), "in" } }).has_value());

  this->engine.applyModel(other);

  CHECK(peak(this->renderBlocks(1000)) > 0.1f);
}

TEST_CASE_FIXTURE(EngineFixture, "Обработка звука не выделяет память (D-023)") {
  const ModuleId osc1 = this->addOscillator(300.0f, 0.2f);
  const ModuleId osc2 = this->addOscillator(700.0f, 0.3f);
  const ModuleId filter = this->add("filter");

  this->connect(osc1, filter);
  this->connect(osc2, filter);
  this->connectToOutput(filter);

  this->engine.applyModel(*this->model);

  Samples left(1024);
  Samples right(1024);

  const size_t before = getAllocationCount();

  for (int block = 0; block < 100; block++) {
    this->engine.process(left.data(), right.data(), 1024);
  }

  CHECK(getAllocationCount() == before);
}

TEST_CASE("Все модули каталога корректны и создают свою DSP-часть") {
  ModuleRegistry registry;

  REQUIRE(addAllModules(registry).has_value());

  for (const ModuleDescriptor* descriptor : { &OSCILLATOR_DESCRIPTOR, &FILTER_DESCRIPTOR, &AMP_DESCRIPTOR }) {
    CAPTURE(descriptor->type);
    REQUIRE(registry.find(descriptor->type) == descriptor);
    REQUIRE(descriptor->createProcessor != nullptr);
    CHECK(descriptor->createProcessor() != nullptr);
  }
}
