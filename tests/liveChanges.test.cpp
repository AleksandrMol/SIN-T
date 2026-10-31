#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "../engine/defaultPreset/defaultPreset.h"
#include "../engine/engine/engine.h"
#include "../engine/modulationCompiler/modulationCompiler.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/planExchange/planExchange.h"
#include "../engine/preset/preset.h"
#include "../engine/synthModel/synthModel.h"
#include "./allocationCounter.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr uint32_t CHUNK_FRAMES = 32;
  constexpr uint32_t FADE_FRAMES = 480; // 15 шагов сетки по 32 сэмпла: ~10 мс
  constexpr uint32_t SETTLE_FRAMES = 2048;
  constexpr float DC_LEVEL = 0.5f;

  // Больше всего сигнал меняется, когда уровень проходит весь диапазон за один переход
  constexpr float FADE_SLOPE = 1.0f / (float)FADE_FRAMES;

  // Самый быстрый допустимый шаг. Щелчок — это скачок на всю величину сигнала, он намного больше
  constexpr float MAX_STEP = FADE_SLOPE * 1.5f;

  using Samples = std::vector<float>;

  std::atomic<int> s_trackedAlive = 0;
  std::atomic<uint64_t> s_trackedFrames = 0;
  std::thread::id s_trackedDestroyedOn;

  /**
   * Постоянный сигнал, равный параметру. На нём любой щелчок виден как скачок между соседними сэмплами
   */
  class DcProcessor : public ModuleProcessor {
    public:
      void prepare(float) override {}
      void reset() override {}

      void process(const ProcessChunk& chunk) override {
        for (uint32_t frame = 0; frame < chunk.frames; frame++) {
          const float value = chunk.at(chunk.parameters[0], frame);

          chunk.outputs[0]->left[frame] = value;
          chunk.outputs[0]->right[frame] = value;
        }
      }
  };

  /**
   * Сигнал растёт от сэмпла к сэмплу: сброс состояния виден сразу
   */
  class CounterProcessor : public ModuleProcessor {
    public:
      void prepare(float) override {}

      void reset() override {
        this->value = 0.0f;
      }

      void process(const ProcessChunk& chunk) override {
        for (uint32_t frame = 0; frame < chunk.frames; frame++) {
          this->value += 0.00001f;
          chunk.outputs[0]->left[frame] = this->value;
          chunk.outputs[0]->right[frame] = this->value;
        }
      }

    private:
      float value = 0.0f;
  };

  /**
   * Меняет знак: разница между обработанным и отключённым сигналом максимальна
   */
  class InvertProcessor : public ModuleProcessor {
    public:
      void prepare(float) override {}
      void reset() override {}

      void process(const ProcessChunk& chunk) override {
        for (uint32_t frame = 0; frame < chunk.frames; frame++) {
          chunk.outputs[0]->left[frame] = -chunk.inputs[0]->left[frame];
          chunk.outputs[0]->right[frame] = -chunk.inputs[0]->right[frame];
        }
      }
  };

  /**
   * Пропускает сигнал и считает свои экземпляры и исполненные сэмплы
   */
  class TrackedProcessor : public ModuleProcessor {
    public:
      TrackedProcessor() {
        s_trackedAlive++;
      }

      ~TrackedProcessor() override {
        s_trackedAlive--;
        s_trackedDestroyedOn = std::this_thread::get_id();
      }

      void prepare(float) override {}
      void reset() override {}

      void process(const ProcessChunk& chunk) override {
        s_trackedFrames += chunk.frames;

        for (uint32_t frame = 0; frame < chunk.frames; frame++) {
          chunk.outputs[0]->left[frame] = chunk.inputs[0]->left[frame];
          chunk.outputs[0]->right[frame] = chunk.inputs[0]->right[frame];
        }
      }
  };

  /**
   * Модулятор с постоянным выходом, равным параметру
   */
  class ConstantModulator : public ModuleProcessor {
    public:
      void prepare(float) override {}
      void reset() override {}
      void process(const ProcessChunk&) override {}

      void modulate(const ModulationChunk& chunk) override {
        chunk.outputs[0] = chunk.parameters[0].end;
      }
  };

  template <typename T>
  std::unique_ptr<ModuleProcessor> createProcessor() {
    return std::make_unique<T>();
  }

  const ModuleDescriptor LIVE_DC = {
    .type = "live.dc",
    .name = "Live DC",
    .outputs = { { "out", "Out" } },
    .parameters = {
      { .key = "level", .name = "Level", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = DC_LEVEL, .isModulatable = true },
    },
    .isAllowedInPoly = true,
    .createProcessor = createProcessor<DcProcessor>,
  };

  const ModuleDescriptor LIVE_COUNTER = {
    .type = "live.counter",
    .name = "Live counter",
    .outputs = { { "out", "Out" } },
    .isAllowedInPoly = true,
    .createProcessor = createProcessor<CounterProcessor>,
  };

  const ModuleDescriptor LIVE_INVERT = {
    .type = "live.invert",
    .name = "Live invert",
    .inputs = { { "in", "In" } },
    .outputs = { { "out", "Out" } },
    .isAllowedInPoly = true,
    .createProcessor = createProcessor<InvertProcessor>,
  };

  const ModuleDescriptor LIVE_TRACKED = {
    .type = "live.tracked",
    .name = "Live tracked",
    .inputs = { { "in", "In" } },
    .outputs = { { "out", "Out" } },
    .isAllowedInPoly = true,
    .createProcessor = createProcessor<TrackedProcessor>,
  };

  const ModuleDescriptor LIVE_MODULATOR = {
    .type = "live.modulator",
    .name = "Live modulator",
    .modOutputs = { { "out", "Out", MOD_POLARITY::UNIPOLAR } },
    .parameters = {
      { .key = "value", .name = "Value", .minValue = 0.0f, .maxValue = 1.0f, .defaultValue = 1.0f, .isModulatable = true },
    },
    .isAllowedInPoly = true,
    .createProcessor = createProcessor<ConstantModulator>,
  };

  NoteAddress note(int16_t key) {
    return { -1, 0, 0, key };
  }

  float findMaxStep(const Samples& samples) {
    float largest = 0.0f;

    for (size_t index = 1; index < samples.size(); index++) {
      largest = std::max(largest, std::fabs(samples[index] - samples[index - 1]));
    }

    return largest;
  }

  struct LiveFixture {
    ModuleRegistry registry;
    std::unique_ptr<SynthModel> model;
    Engine engine;
    float lastSample = 0.0f;

    LiveFixture() {
      REQUIRE(addAllModules(this->registry).has_value());

      for (const ModuleDescriptor* descriptor : { &LIVE_DC, &LIVE_COUNTER, &LIVE_INVERT, &LIVE_TRACKED, &LIVE_MODULATOR }) {
        REQUIRE(this->registry.add(descriptor).has_value());
      }

      this->model = std::make_unique<SynthModel>(&this->registry);
      this->engine.prepare(SAMPLE_RATE);
    }

    ~LiveFixture() {
      this->engine.setLive(false);
    }

    ModuleId add(const std::string& type, std::optional<ModuleId> owner = std::nullopt) {
      const CommandResult result = this->model->apply(AddModule{ type, owner });

      REQUIRE(result.has_value());

      return **result;
    }

    void command(const Command& command) {
      REQUIRE(this->model->apply(command).has_value());
    }

    void set(ModuleId id, const std::string& parameter, float value) {
      this->command(SetParameter{ id, parameter, value });
    }

    void connect(ModuleId from, ModuleId to) {
      this->command(Connect{ { from, "out", to, "in" } });
    }

    void disconnect(ModuleId from, ModuleId to) {
      this->command(Disconnect{ { from, "out", to, "in" } });
    }

    ModuleId output() const {
      return this->model->getOutputId();
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

    ModuleId addDcGenerator(float polyphony = 16.0f, float level = 0.1f) {
      const ModuleId generator = this->add("generator");
      const ModuleId dc = this->add("live.dc", generator);

      this->set(generator, "polyphony", polyphony);
      this->set(dc, "level", level);
      this->connect(dc, this->getVoiceOutput(generator));
      this->connect(generator, this->output());

      return generator;
    }

    /**
     * Запускает модель и включает живой режим: дальше движок работает как в DAW
     */
    void start() {
      this->engine.applyModel(*this->model);
      this->engine.setLive(true);
    }

    void edit() {
      this->engine.applyModel(*this->model);
    }

    Samples run(uint32_t frames, uint32_t blockSize = 64) {
      Samples left(frames);
      Samples right(frames);

      for (uint32_t done = 0; done < frames; done += blockSize) {
        const uint32_t count = std::min(blockSize, frames - done);

        this->engine.process(left.data() + done, right.data() + done, count);
      }

      CHECK(left == right);

      this->lastSample = left.back();

      return left;
    }

    /**
     * Дожидается конца всех переходов: так хост вызывает on_main_thread после запроса из process.
     * Результат начинается с последнего сэмпла перед вызовом: иначе скачок прямо на границе правки не виден
     */
    Samples runAndUpdate(uint32_t frames = SETTLE_FRAMES, uint32_t blockSize = 64) {
      Samples result = { this->lastSample };

      for (uint32_t done = 0; done < frames; done += blockSize) {
        const Samples part = this->run(blockSize, blockSize);

        result.insert(result.end(), part.begin(), part.end());

        if (this->engine.takeUpdateRequest()) {
          this->engine.update();
        }
      }

      return result;
    }

    /**
     * Проверяет, что значение плавно дошло от начального до конечного: правка должна быть слышна, но без щелчка
     */
    void checkSmooth(const Samples& samples, float from, float to) {
      CHECK(samples.front() == doctest::Approx(from).epsilon(0.02));
      CHECK(samples.back() == doctest::Approx(to).epsilon(0.001));
      CHECK(findMaxStep(samples) < MAX_STEP);
    }
  };

  /**
   * Источник постоянного сигнала на выходе главного графа
   */
  struct DcFixture : LiveFixture {
    ModuleId dc = 0;

    DcFixture() {
      this->dc = this->add("live.dc");
      this->connect(this->dc, this->output());
      this->start();
      this->run(CHUNK_FRAMES * 4);
    }
  };
}

TEST_CASE_FIXTURE(DcFixture, "Значение от хоста, уже записанное в модель, не возвращается вдогонку новому (D-056)") {
  const float first = DC_LEVEL * 0.5f;
  const float second = DC_LEVEL * 0.25f;

  this->engine.setParameter(this->dc, 0, first);
  this->run(CHUNK_FRAMES * 30);

  // Главный поток дошёл до первого значения, а хост уже прислал второе
  this->set(this->dc, "level", first);
  this->engine.acknowledgeParameter({ this->dc, "level", first });
  this->engine.setParameter(this->dc, 0, second);

  // Обычная правка рядом со значением хоста посылает изменившиеся параметры модели
  this->engine.applyModel(*this->model);
  this->run(CHUNK_FRAMES * 30);

  CHECK(this->run(CHUNK_FRAMES).back() == doctest::Approx(second));
}

TEST_CASE("Живой режим: первая модель приходит, когда звук уже идёт") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");

  fixture.connect(dc, fixture.output());
  fixture.engine.setLive(true);

  CHECK(fixture.run(CHUNK_FRAMES).back() == 0.0f);

  fixture.edit();

  CHECK(fixture.run(CHUNK_FRAMES)[0] == DC_LEVEL);
}

TEST_CASE("Живой режим: новый провод нарастает за ~10 мс") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");

  fixture.start();
  CHECK(fixture.run(256).back() == 0.0f);

  fixture.connect(dc, fixture.output());
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  fixture.checkSmooth(samples, 0.0f, DC_LEVEL);

  // Значение меняется только в начале: остальное время провод звучит ровно
  const auto reached = std::ranges::find(samples, DC_LEVEL);

  REQUIRE(reached != samples.end());
  CHECK(reached - samples.begin() == doctest::Approx(FADE_FRAMES).epsilon(0.15));
}

TEST_CASE("Живой режим: удалённый провод затухает, а не обрывается (D-024)") {
  DcFixture fixture;

  fixture.disconnect(fixture.dc, fixture.output());
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  fixture.checkSmooth(samples, DC_LEVEL, 0.0f);
  CHECK(samples.back() == 0.0f);
}

TEST_CASE("Живой режим: план не меняется посреди шага сетки") {
  DcFixture fixture;

  fixture.run(CHUNK_FRAMES / 2, 16);
  fixture.disconnect(fixture.dc, fixture.output());
  fixture.edit();

  // До конца шага звучит прежний план
  const Samples rest = fixture.run(CHUNK_FRAMES / 2, 16);

  CHECK(rest.back() == DC_LEVEL);
  CHECK(fixture.run(CHUNK_FRAMES * 2, 16).back() < DC_LEVEL);
}

TEST_CASE("Живой режим: удаление эффекта плавно ведёт сигнал по новой схеме (ТЗ §4.4)") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");
  const ModuleId invert = fixture.add("live.invert");

  fixture.connect(dc, invert);
  fixture.connect(invert, fixture.output());
  fixture.start();
  CHECK(fixture.run(64).back() == -DC_LEVEL);

  fixture.command(RemoveModule{ invert });
  fixture.connect(dc, fixture.output());
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == DC_LEVEL);
  CHECK(findMaxStep(samples) < MAX_STEP);
}

TEST_CASE("Живой режим: добавление эффекта включает его в маршрут (ТЗ §4.4)") {
  DcFixture fixture;
  const ModuleId invert = fixture.add("live.invert");

  fixture.disconnect(fixture.dc, fixture.output());
  fixture.connect(fixture.dc, invert);
  fixture.connect(invert, fixture.output());
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == -DC_LEVEL);
  CHECK(findMaxStep(samples) < MAX_STEP);
}

TEST_CASE("Живой режим: подмена не сбрасывает состояние модулей (D-023)") {
  const auto render = [](bool withEdits) {
    LiveFixture fixture;
    const ModuleId counter = fixture.add("live.counter");
    const ModuleId dc = fixture.add("live.dc");

    fixture.connect(counter, fixture.output());
    fixture.start();

    Samples result = fixture.runAndUpdate(1024);

    if (withEdits) {
      fixture.connect(dc, fixture.add("live.invert"));
      fixture.edit();
    }

    const Samples rest = fixture.runAndUpdate(4096);

    result.insert(result.end(), rest.begin(), rest.end());

    return result;
  };

  CHECK(render(true) == render(false));
}

TEST_CASE("Живой режим: звук перехода не зависит от размера блока (D-008)") {
  const auto render = [](uint32_t blockSize) {
    DcFixture fixture;

    fixture.run(CHUNK_FRAMES * 4 + 1024, 64);
    fixture.disconnect(fixture.dc, fixture.output());
    fixture.edit();

    return fixture.run(2048, blockSize);
  };

  CHECK(render(32) == render(256));
  CHECK(render(32) == render(1024));

  // Разрез блока посреди шага сетки: значение перехода должно зависеть только от положения сэмпла
  CHECK(render(32) == render(7));
  CHECK(render(32) == render(50));
}

TEST_CASE("Живой режим: звук перехода внутри голоса не зависит от размера блока (D-008)") {
  const auto render = [](uint32_t blockSize) {
    LiveFixture fixture;
    const ModuleId generator = fixture.addDcGenerator(16.0f, 0.1f);

    fixture.start();
    fixture.engine.noteOn(note(60), 1.0f);
    fixture.run(1024, 64);

    const ModuleId dc = fixture.add("live.dc", generator);

    fixture.connect(dc, fixture.getVoiceOutput(generator));
    fixture.edit();

    return fixture.run(2048, blockSize);
  };

  CHECK(render(32) == render(7));
  CHECK(render(32) == render(50));
}

TEST_CASE("Живой режим: отключение модуля плавное в обе стороны и замораживает его состояние (D-040, D-041)") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");
  const ModuleId invert = fixture.add("live.invert");
  const ModuleId tracked = fixture.add("live.tracked");

  fixture.connect(dc, invert);
  fixture.connect(invert, tracked);
  fixture.connect(tracked, fixture.output());
  fixture.start();
  CHECK(fixture.run(64).back() == -DC_LEVEL);

  fixture.command(SetModuleEnabled{ invert, false });
  fixture.edit();
  fixture.checkSmooth(fixture.runAndUpdate(), -DC_LEVEL, DC_LEVEL);

  SUBCASE("Обратно включается так же") {
    fixture.command(SetModuleEnabled{ invert, true });
    fixture.edit();
    fixture.checkSmooth(fixture.runAndUpdate(), DC_LEVEL, -DC_LEVEL);
  }

  SUBCASE("Отключённый после перехода не исполняется") {
    fixture.command(SetModuleEnabled{ tracked, false });
    fixture.edit();
    fixture.runAndUpdate();

    const uint64_t before = s_trackedFrames;

    fixture.runAndUpdate();

    CHECK(s_trackedFrames == before);
  }
}

TEST_CASE("Живой режим: маршрут появляется, меняется и исчезает плавно (D-023)") {
  DcFixture fixture;
  const ModuleId modulator = fixture.add("live.modulator");
  const ModRoute route = { { modulator, "out" }, { fixture.dc, "level" }, 0.5f };

  fixture.set(fixture.dc, "level", 0.25f);
  fixture.edit();
  fixture.runAndUpdate();

  fixture.command(AddRoute{ route });
  fixture.edit();
  fixture.checkSmooth(fixture.runAndUpdate(), 0.25f, 0.75f);

  SUBCASE("Глубина") {
    fixture.command(SetRouteDepth{ route.source, route.target, 0.25f });
    fixture.edit();
    fixture.checkSmooth(fixture.runAndUpdate(), 0.75f, 0.5f);
  }

  SUBCASE("Удаление") {
    fixture.command(RemoveRoute{ route.source, route.target });
    fixture.edit();
    fixture.checkSmooth(fixture.runAndUpdate(), 0.75f, 0.25f);
  }

  SUBCASE("Отключение и включение источника") {
    fixture.command(SetModuleEnabled{ modulator, false });
    fixture.edit();
    fixture.checkSmooth(fixture.runAndUpdate(), 0.75f, 0.25f);

    fixture.command(SetModuleEnabled{ modulator, true });
    fixture.edit();
    fixture.checkSmooth(fixture.runAndUpdate(), 0.25f, 0.75f);
  }
}

TEST_CASE("Живой режим: параметр идёт мимо плана и сглаживается (D-023, D-025)") {
  DcFixture fixture;

  fixture.runAndUpdate();
  fixture.set(fixture.dc, "level", 0.1f);
  fixture.edit();

  const Samples samples = fixture.run(SETTLE_FRAMES);

  fixture.checkSmooth(samples, DC_LEVEL, 0.1f);

  // Новый план аудиопоток бы забрал и попросил главный поток убрать старый
  CHECK_FALSE(fixture.engine.takeUpdateRequest());
}

TEST_CASE("Живой режим: автоматизация не откатывается, когда главный поток меняет другой параметр") {
  DcFixture fixture;
  const ModuleId second = fixture.add("live.dc");

  fixture.connect(second, fixture.output());
  fixture.edit();
  fixture.runAndUpdate();

  // Значение от хоста есть только в движке, в модели остаётся прежнее
  fixture.engine.setParameter(fixture.dc, 0, 0.2f);
  fixture.runAndUpdate();

  fixture.set(second, "level", 0.1f);
  fixture.edit();

  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.3f));
}

TEST_CASE("Живой режим: параметр нового модуля и модуля, которого нет в плане") {
  DcFixture fixture;
  const ModuleId added = fixture.add("live.dc");

  fixture.set(added, "level", 0.9f);
  fixture.edit();
  fixture.runAndUpdate();

  // Значение меняется, пока модуль не исполняется: его параметры никто не считает
  fixture.set(added, "level", 0.2f);
  fixture.edit();
  fixture.runAndUpdate();

  fixture.disconnect(fixture.dc, fixture.output());
  fixture.connect(added, fixture.output());
  fixture.edit();

  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.2f));
}

TEST_CASE("Живой режим: значение, отправленное раньше, чем аудиопоток забрал план, не теряется") {
  DcFixture fixture;
  const ModuleId added = fixture.add("live.dc");

  fixture.connect(added, fixture.output());
  fixture.edit();
  fixture.set(added, "level", 0.1f);
  fixture.edit();

  CHECK(fixture.runAndUpdate().back() == doctest::Approx(DC_LEVEL + 0.1f));
}

TEST_CASE("Живой режим: правки во время перехода применяются одним следующим переходом") {
  DcFixture fixture;
  const ModuleId second = fixture.add("live.dc");
  const ModuleId third = fixture.add("live.dc");

  fixture.set(second, "level", 0.1f);
  fixture.set(third, "level", 0.2f);
  fixture.connect(second, fixture.output());
  fixture.edit();
  fixture.run(FADE_FRAMES / 2);

  fixture.disconnect(second, fixture.output());
  fixture.edit();
  fixture.connect(third, fixture.output());
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == doctest::Approx(DC_LEVEL + 0.2f));
  CHECK(findMaxStep(samples) < MAX_STEP);
}

TEST_CASE("Живой режим: провод, замыкающий цикл с затухающим, ждёт его конца (D-007)") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");
  const ModuleId first = fixture.add("live.invert");
  const ModuleId second = fixture.add("live.invert");

  fixture.connect(dc, first);
  fixture.connect(first, second);
  fixture.connect(second, fixture.output());
  fixture.start();
  CHECK(fixture.run(64).back() == DC_LEVEL);

  // Было dc → first → second → выход, стало dc → second → first → выход
  fixture.disconnect(dc, first);
  fixture.disconnect(first, second);
  fixture.disconnect(second, fixture.output());
  fixture.connect(dc, second);
  fixture.connect(second, first);
  fixture.connect(first, fixture.output());
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == DC_LEVEL);

  // Обе схемы дают DC_LEVEL, поэтому провал показывает, что звук действительно прошёл через переход
  CHECK(*std::ranges::min_element(samples) < DC_LEVEL);

  // Пять проводов меняются одновременно и складывают свои наклоны. Щелчок был бы скачком на DC_LEVEL
  CHECK(findMaxStep(samples) < MAX_STEP * 2.0f);
}

TEST_CASE("Живой режим: удалённый модуль освобождается главным потоком и только после ухода из плана (D-023)") {
  DcFixture fixture;
  const ModuleId tracked = fixture.add("live.tracked");

  fixture.disconnect(fixture.dc, fixture.output());
  fixture.connect(fixture.dc, tracked);
  fixture.connect(tracked, fixture.output());
  fixture.edit();
  fixture.runAndUpdate();

  const int alive = s_trackedAlive;

  CHECK(alive > 0);
  CHECK(fixture.run(64).back() == DC_LEVEL);

  fixture.command(RemoveModule{ tracked });
  fixture.edit();

  // Модуль затухает и всё ещё звучит
  fixture.run(CHUNK_FRAMES * 2);
  fixture.engine.update();
  CHECK(s_trackedAlive == alive);

  fixture.run(SETTLE_FRAMES);
  CHECK(s_trackedAlive == alive);

  // Переход окончен: главный поток публикует план без модуля, но его ещё надо забрать
  fixture.engine.update();
  CHECK(s_trackedAlive == alive);

  fixture.run(CHUNK_FRAMES);
  CHECK(s_trackedAlive == alive);

  fixture.engine.update();
  CHECK(s_trackedAlive == alive - 1);
  CHECK(s_trackedDestroyedOn == std::this_thread::get_id());
}

TEST_CASE("Живой режим: удаление генератора убирает его голоса (ТЗ §5.4)") {
  LiveFixture fixture;
  const ModuleId generator = fixture.addDcGenerator();
  const int aliveBefore = s_trackedAlive;

  fixture.add("live.tracked", generator);
  fixture.start();
  fixture.engine.noteOn(note(60), 1.0f);
  CHECK(fixture.run(256).back() == doctest::Approx(0.1f));
  CHECK(s_trackedAlive > aliveBefore);

  fixture.command(RemoveModule{ generator });
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == 0.0f);
  CHECK(findMaxStep(samples) < MAX_STEP);
  CHECK(s_trackedAlive == aliveBefore);
}

TEST_CASE("Живой режим: новый генератор получает только следующие ноты (ТЗ §5.4)") {
  DcFixture fixture;

  fixture.engine.noteOn(note(60), 1.0f);

  fixture.addDcGenerator();
  fixture.edit();
  CHECK(fixture.runAndUpdate().back() == DC_LEVEL);

  fixture.engine.noteOn(note(64), 1.0f);

  CHECK(fixture.runAndUpdate().back() == doctest::Approx(DC_LEVEL + 0.1f));
}

TEST_CASE("Живой режим: голоса генератора, вышедшего из плана, не зависают") {
  LiveFixture fixture;
  const ModuleId generator = fixture.addDcGenerator();

  fixture.start();
  fixture.engine.noteOn(note(60), 1.0f);
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.1f));

  SUBCASE("Провод до выхода убрали и вернули") {
    fixture.disconnect(generator, fixture.output());
    fixture.edit();
    CHECK(fixture.runAndUpdate().back() == 0.0f);

    fixture.connect(generator, fixture.output());
  }

  SUBCASE("Генератор отключили и включили") {
    fixture.command(SetModuleEnabled{ generator, false });
    fixture.edit();

    const Samples samples = fixture.runAndUpdate();

    CHECK(samples.back() == 0.0f);
    CHECK(findMaxStep(samples) < MAX_STEP);

    fixture.command(SetModuleEnabled{ generator, true });
  }

  // Note off старая нота не получила: без сброса она зазвучала бы снова и навсегда
  fixture.edit();
  CHECK(fixture.runAndUpdate().back() == 0.0f);

  fixture.engine.noteOn(note(62), 1.0f);
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.1f));
}

TEST_CASE("Живой режим: уменьшение полифонии забирает голоса в порядке D-042") {
  LiveFixture fixture;
  const ModuleId generator = fixture.addDcGenerator(4.0f);

  fixture.start();

  for (int16_t key = 60; key < 64; key++) {
    fixture.engine.noteOn(note(key), 1.0f);
  }

  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.4f));

  fixture.engine.noteOff(note(62));
  fixture.set(generator, "polyphony", 2.0f);
  fixture.edit();

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == doctest::Approx(0.2f));
  CHECK(findMaxStep(samples) < MAX_STEP * 2.0f);

  // Первым забирается отпущенный голос, затем самый старый из удерживаемых: остаются 61 и 63
  fixture.engine.noteOff(note(60));
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.2f));

  fixture.engine.noteOff(note(61));
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.1f));
}

TEST_CASE("Живой режим: увеличение полифонии даёт новые голоса") {
  LiveFixture fixture;
  const ModuleId generator = fixture.addDcGenerator(2.0f, 0.01f);

  fixture.start();
  fixture.runAndUpdate();

  fixture.set(generator, "polyphony", MAX_POLYPHONY);
  fixture.edit();
  fixture.runAndUpdate();

  Samples left(256);
  Samples right(256);
  const size_t before = getThreadMemoryOperations();

  for (int16_t key = 0; key < (int16_t)MAX_POLYPHONY; key++) {
    fixture.engine.noteOn(note(key), 1.0f);
  }

  fixture.engine.process(left.data(), right.data(), 256);

  CHECK(getThreadMemoryOperations() == before);
  CHECK(left.back() == doctest::Approx(0.01f * (float)MAX_POLYPHONY));
}

TEST_CASE("Живой режим: новый модуль включается в уже звучащий голос") {
  LiveFixture fixture;
  const ModuleId generator = fixture.addDcGenerator(16.0f, 0.1f);

  fixture.start();
  fixture.engine.noteOn(note(60), 1.0f);
  fixture.runAndUpdate();

  SUBCASE("Источник звука") {
    const ModuleId dc = fixture.add("live.dc", generator);

    fixture.set(dc, "level", 0.3f);
    fixture.connect(dc, fixture.getVoiceOutput(generator));
    fixture.edit();

    fixture.checkSmooth(fixture.runAndUpdate(), 0.1f, 0.4f);
  }

  SUBCASE("Огибающая начинает атаку") {
    const ModuleId env = fixture.add("env", generator);
    const ModuleId dc = fixture.add("live.dc", generator);

    fixture.set(env, "attack", 0.1f);
    fixture.set(env, "sustain", 1.0f);
    fixture.set(dc, "level", 0.0f);
    fixture.connect(dc, fixture.getVoiceOutput(generator));
    fixture.command(AddRoute{ { { env, "out" }, { dc, "level" }, 1.0f } });
    fixture.edit();

    const Samples samples = fixture.runAndUpdate((uint32_t)(0.2f * SAMPLE_RATE));

    CHECK(samples[(size_t)(0.05f * SAMPLE_RATE)] == doctest::Approx(0.6f).epsilon(0.05));
    CHECK(samples.back() == doctest::Approx(1.1f));
  }
}

TEST_CASE("Живой режим: замена состояния переводит старый план в новый без щелчка (D-054)") {
  DcFixture fixture;
  SynthModel next(&fixture.registry);
  const ModuleId dc = **next.apply(AddModule{ "live.dc" });

  REQUIRE(next.apply(SetParameter{ dc, "level", 0.9f }).has_value());
  REQUIRE(next.apply(Connect{ { dc, "out", next.getOutputId(), "in" } }).has_value());

  fixture.engine.replaceModel(next);
  fixture.model = std::make_unique<SynthModel>(next);

  fixture.checkSmooth(fixture.runAndUpdate(), DC_LEVEL, 0.9f);

  // Правки идут уже поверх новой модели
  fixture.set(dc, "level", 0.3f);
  fixture.edit();
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.3f));
}

TEST_CASE("Живой режим: звучащие голоса заканчиваются вместе со старым планом, а новая нота идёт в новый") {
  LiveFixture fixture;

  fixture.addDcGenerator(16.0f, 0.1f);
  fixture.start();
  fixture.engine.noteOn(note(60), 1.0f);
  fixture.runAndUpdate();

  const SynthModel next = *fixture.model;

  fixture.engine.replaceModel(next);
  fixture.run(CHUNK_FRAMES);
  fixture.engine.noteOn(note(62), 1.0f);

  // Новый план принял ноту посреди перехода
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.1f));

  fixture.engine.noteOff(note(62));
  fixture.engine.noteOff(note(60));
  CHECK(fixture.runAndUpdate().back() == 0.0f);
}

TEST_CASE("Живой режим: на тишине замена проходит сразу и не смягчает первую ноту") {
  LiveFixture fixture;

  fixture.add("live.dc");
  fixture.start();

  SynthModel next(&fixture.registry);
  const ModuleId generator = **next.apply(AddModule{ "generator" });
  const ModuleId dc = **next.apply(AddModule{ "live.dc", generator });

  for (const auto& [id, module] : next.getModules()) {
    if (module.owner == generator && module.descriptor->role == MODULE_ROLE::OUTPUT) {
      REQUIRE(next.apply(Connect{ { dc, "out", id, "in" } }).has_value());
    }
  }

  REQUIRE(next.apply(Connect{ { generator, "out", next.getOutputId(), "in" } }).has_value());

  fixture.engine.replaceModel(next);

  // Как в рендере DAW: план заменён до первого сэмпла, а нота стоит на самом первом
  fixture.engine.noteOn(note(60), 1.0f);

  CHECK(fixture.run(64)[0] == DC_LEVEL);
}

TEST_CASE("Живой режим: правка до того, как аудиопоток забрал замену, не теряет её признак") {
  DcFixture fixture;
  SynthModel next = *fixture.model;
  const ModuleId dc = fixture.dc;

  REQUIRE(next.apply(SetParameter{ dc, "level", 0.9f }).has_value());
  fixture.engine.replaceModel(next);

  // Новый план использует те же состояния, что и замена. Без признака старый план
  // остался бы без перехода, а звук прыгнул
  const ModuleId added = **next.apply(AddModule{ "live.dc" });

  REQUIRE(next.apply(Connect{ { added, "out", next.getOutputId(), "in" } }).has_value());
  fixture.engine.applyModel(next);

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == doctest::Approx(0.9f + DC_LEVEL));
  CHECK(findMaxStep(samples) < MAX_STEP * 2.0f);
}

TEST_CASE("Живой режим: замена посреди обычного перехода не замораживает старый план") {
  DcFixture fixture;

  fixture.disconnect(fixture.dc, fixture.output());
  fixture.edit();
  fixture.run(FADE_FRAMES / 4);

  SynthModel next(&fixture.registry);
  const ModuleId dc = **next.apply(AddModule{ "live.dc" });

  REQUIRE(next.apply(Connect{ { dc, "out", next.getOutputId(), "in" } }).has_value());

  fixture.engine.replaceModel(next);

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == DC_LEVEL);
  CHECK(findMaxStep(samples) < MAX_STEP * 2.0f);
}

TEST_CASE("Живой режим: reset посреди замены сразу убирает старый план") {
  DcFixture fixture;
  SynthModel next = *fixture.model;

  REQUIRE(next.apply(SetParameter{ fixture.dc, "level", 0.9f }).has_value());
  fixture.engine.replaceModel(next);
  fixture.run(CHUNK_FRAMES * 2);

  fixture.engine.reset();

  CHECK(fixture.run(CHUNK_FRAMES)[0] == doctest::Approx(0.9f));
  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.9f));
}

TEST_CASE("Живой режим: следующая замена ждёт конца текущей") {
  DcFixture fixture;
  SynthModel first = *fixture.model;
  SynthModel second = *fixture.model;

  REQUIRE(first.apply(SetParameter{ fixture.dc, "level", 0.9f }).has_value());
  REQUIRE(second.apply(SetParameter{ fixture.dc, "level", 0.2f }).has_value());

  fixture.engine.replaceModel(first);
  fixture.run(CHUNK_FRAMES * 2);
  fixture.engine.replaceModel(second);

  const Samples samples = fixture.runAndUpdate();

  CHECK(samples.back() == doctest::Approx(0.2f));
  CHECK(findMaxStep(samples) < MAX_STEP * 2.0f);
}

TEST_CASE("Живой режим: новый план после замены получает свой лимит голосов") {
  LiveFixture fixture;

  fixture.add("live.dc");
  fixture.start();

  SynthModel storage = *fixture.model;

  fixture.model = std::make_unique<SynthModel>(storage);
  fixture.addDcGenerator(8.0f, 0.1f);
  fixture.engine.replaceModel(*fixture.model);

  for (int16_t key = 60; key < 63; key++) {
    fixture.engine.noteOn(note(key), 1.0f);
  }

  CHECK(fixture.runAndUpdate().back() == doctest::Approx(0.3f));
}

TEST_CASE("Живой режим: старый план продолжает считать модуляцию, пока затухает") {
  DcFixture fixture;
  const ModuleId lfo = fixture.add("lfo");

  fixture.set(lfo, "rate", 50.0f);
  fixture.command(AddRoute{ { { lfo, "out" }, { fixture.dc, "level" }, 0.25f } });
  fixture.edit();
  fixture.runAndUpdate();

  const float slope = 0.25f * 2.0f * 3.1416f * 50.0f / SAMPLE_RATE;

  fixture.engine.replaceModel(*fixture.model);

  CHECK(findMaxStep(fixture.runAndUpdate()) < slope + MAX_STEP);
}

TEST_CASE("Живой режим: глобальный экземпляр общего модулятора не застревает после замены (D-005)") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");
  const ModuleId env = fixture.add("env");

  fixture.set(dc, "level", 0.0f);
  fixture.set(env, "release", 0.001f);
  fixture.connect(dc, fixture.output());
  fixture.command(AddRoute{ { { env, "out" }, { dc, "level" }, 1.0f } });
  fixture.start();
  fixture.engine.noteOn(note(60), 1.0f);
  CHECK(fixture.runAndUpdate(4800).back() > 0.0f);

  const SynthModel next = *fixture.model;

  fixture.engine.replaceModel(next);

  // Клавишу держали в старом пресете. Новый не получит её note off и не должен застрять
  CHECK(fixture.runAndUpdate(4800).back() == 0.0f);
}

TEST_CASE("Живой режим: описание заглушки живёт, пока на него ссылаются старые планы (запускать под AddressSanitizer)") {
  LiveFixture fixture;
  const ModuleId dc = fixture.add("live.dc");
  const ModuleId modulator = fixture.add("live.modulator");

  fixture.command(AddRoute{ { { modulator, "out" }, { dc, "level" }, 0.5f } });

  std::string text = saveModel(*fixture.model);

  text.replace(text.find("live.dc"), 7, "unknown");

  {
    const auto loaded = loadModel(&fixture.registry, text);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->findModule(dc)->isStub());
    REQUIRE_FALSE(loaded->findModule(dc)->descriptor->parameters.empty());

    fixture.model = std::make_unique<SynthModel>(*loaded);
  }

  fixture.start();
  fixture.run(CHUNK_FRAMES);

  // После этого описание заглушки держит только движок: хост мог уже заменить свою модель
  fixture.model = std::make_unique<SynthModel>(&fixture.registry);
  fixture.engine.replaceModel(*fixture.model);
  fixture.engine.setParameter(dc, 0, 0.5f);

  CHECK(fixture.run(CHUNK_FRAMES).back() == 0.0f);
}

TEST_CASE("Базовые значения исчезнувшего модуля убираются из таблицы") {
  LiveFixture fixture;
  PlanExchange exchange;
  ParameterBases bases;
  const ModuleId dc = fixture.add("live.dc");

  addParameterBases(*fixture.model, bases, exchange);
  REQUIRE(bases.contains(dc));

  bases.at(dc)[0].value = 0.75f;
  addParameterBases(*fixture.model, bases, exchange);
  CHECK(bases.at(dc)[0].value == 0.75f);

  fixture.command(RemoveModule{ dc });
  addParameterBases(*fixture.model, bases, exchange);

  CHECK_FALSE(bases.contains(dc));
}

TEST_CASE("Живой режим: выключение доделывает незавершённый переход") {
  DcFixture fixture;
  const ModuleId second = fixture.add("live.dc");
  float expected = 0.3f;

  fixture.set(second, "level", 0.3f);
  fixture.disconnect(fixture.dc, fixture.output());
  fixture.connect(second, fixture.output());
  fixture.edit();

  SUBCASE("План ещё не забран") {}

  SUBCASE("Посреди перехода") {
    fixture.run(CHUNK_FRAMES);
  }

  SUBCASE("Правка ждёт конца перехода") {
    fixture.run(CHUNK_FRAMES);
    fixture.connect(fixture.dc, fixture.output());
    fixture.edit();
    expected += DC_LEVEL;
  }

  SUBCASE("Замена состояния ещё не забрана") {
    fixture.run(CHUNK_FRAMES);
    fixture.engine.replaceModel(*fixture.model);
  }

  SUBCASE("Посреди замены состояния") {
    fixture.run(CHUNK_FRAMES);
    fixture.engine.replaceModel(*fixture.model);
    fixture.run(CHUNK_FRAMES);
  }

  fixture.engine.setLive(false);

  CHECK(fixture.run(CHUNK_FRAMES).back() == doctest::Approx(expected));

  // Вне живого режима подмены идут сразу
  fixture.set(second, "level", 0.1f);
  fixture.edit();

  CHECK(fixture.run(CHUNK_FRAMES).back() == doctest::Approx(expected - 0.2f));
}

TEST_CASE("Живой режим: process не выделяет и не освобождает память даже при смене плана (D-023)") {
  LiveFixture fixture;
  const ModuleId generator = fixture.addDcGenerator(8.0f);

  fixture.start();
  fixture.engine.noteOn(note(60), 1.0f);

  const auto check = [&fixture] {
    for (int step = 0; step < 40; step++) {
      Samples left(64);
      Samples right(64);
      const size_t before = getThreadMemoryOperations();

      fixture.engine.process(left.data(), right.data(), 64);
      fixture.engine.noteOn(note((int16_t)(60 + step % 4)), 1.0f);
      fixture.engine.noteOff(note((int16_t)(60 + step % 3)));

      CHECK(getThreadMemoryOperations() == before);

      if (fixture.engine.takeUpdateRequest()) {
        fixture.engine.update();
      }
    }
  };

  const ModuleId added = fixture.add("live.invert", generator);

  fixture.connect(added, fixture.getVoiceOutput(generator));
  fixture.edit();
  check();

  fixture.command(RemoveModule{ added });
  fixture.set(generator, "polyphony", 2.0f);
  fixture.edit();
  check();

  const SynthModel next = *fixture.model;

  fixture.engine.replaceModel(next);
  check();
}

namespace {
  /**
   * Случайные правки модели. Команда может быть отклонена: это нормально, модель не меняется
   */
  struct RandomEditor {
    std::mt19937 random;
    SynthModel& model;

    uint32_t pick(uint32_t count) {
      return this->random() % count;
    }

    ModuleId pickModule() {
      const auto& modules = this->model.getModules();

      return std::next(modules.begin(), this->pick((uint32_t)modules.size()))->first;
    }

    float pickUnit() {
      return (float)this->pick(1001) / 1000.0f;
    }

    std::optional<ModuleId> pickOwner() {
      const ModuleId id = this->pickModule();
      const ModuleInstance* module = this->model.findModule(id);

      return module->descriptor->role == MODULE_ROLE::GENERATOR ? std::optional(id) : module->owner;
    }

    void addModule() {
      static const std::vector<std::string> TYPES = { "oscillator", "filter", "amp", "env", "lfo", "generator", "live.invert" };

      (void)this->model.apply(AddModule{ TYPES[this->pick((uint32_t)TYPES.size())], this->pickOwner() });
    }

    void setParameter() {
      const ModuleInstance* module = this->model.findModule(this->pickModule());
      const auto& parameters = module->descriptor->parameters;

      if (!parameters.empty()) {
        const ParameterDescriptor& parameter = parameters[this->pick((uint32_t)parameters.size())];

        (void)this->model.apply(SetParameter{ module->id, parameter.key, toReal(parameter, this->pickUnit()) });
      }
    }

    void connect() {
      (void)this->model.apply(Connect{ { this->pickModule(), "out", this->pickModule(), "in" } });
    }

    void disconnect() {
      const auto& connections = this->model.getConnections();

      if (!connections.empty()) {
        (void)this->model.apply(Disconnect{ connections[this->pick((uint32_t)connections.size())] });
      }
    }

    void addRoute() {
      const ModuleInstance* target = this->model.findModule(this->pickModule());
      const auto& parameters = target->descriptor->parameters;

      if (!parameters.empty()) {
        const std::string& key = parameters[this->pick((uint32_t)parameters.size())].key;

        (void)this->model.apply(AddRoute{ { { this->pickModule(), "out" }, { target->id, key }, this->pickUnit() * 2.0f - 1.0f } });
      }
    }

    void editRoute() {
      const auto& routes = this->model.getRoutes();

      if (routes.empty()) {
        return;
      }

      const ModRoute route = routes[this->pick((uint32_t)routes.size())];

      if (this->pick(2) == 0) {
        (void)this->model.apply(RemoveRoute{ route.source, route.target });
      } else {
        (void)this->model.apply(SetRouteDepth{ route.source, route.target, this->pickUnit() });
      }
    }

    void step() {
      switch (this->pick(10)) {
        case 0: this->addModule(); break;
        case 1: (void)this->model.apply(RemoveModule{ this->pickModule() }); break;
        case 2: this->connect(); break;
        case 3: this->disconnect(); break;
        case 4: (void)this->model.apply(SetModuleEnabled{ this->pickModule(), this->pick(2) == 0 }); break;
        case 5: this->addRoute(); break;
        case 6: this->editRoute(); break;
        default: this->setParameter(); break;
      }
    }
  };

  /**
   * Аудиопоток: ноты и звук без остановки. Проверки внутри потока собираются в результат:
   * doctest нельзя вызывать одновременно из двух потоков
   */
  struct AudioResult {
    uint64_t blocks = 0;
    uint64_t badSamples = 0;
    size_t memoryOperations = 0;
  };

  AudioResult runAudio(Engine& engine, const std::atomic<bool>& isRunning, uint32_t seed) {
    std::mt19937 random(seed);
    AudioResult result;
    Samples left(128);
    Samples right(128);
    const size_t before = getThreadMemoryOperations();

    while (isRunning) {
      const auto key = (int16_t)(48 + random() % 24);

      engine.process(left.data(), right.data(), (uint32_t)left.size());

      if (random() % 3 == 0) {
        engine.noteOn(note(key), 0.8f);
      } else {
        engine.noteOff(note(key));
      }

      result.blocks++;
      result.badSamples += (uint64_t)std::ranges::count_if(left, [](float sample) {
        return !std::isfinite(sample);
      });

      (void)engine.takeUpdateRequest();
    }

    result.memoryOperations = getThreadMemoryOperations() - before;

    return result;
  }
}

TEST_CASE("Живой режим: правки с главного потока во время звука (стресс-тест, запускать и под ThreadSanitizer)") {
  LiveFixture fixture;

  // Стартовая модель, которая звучит: случайные правки её постепенно перестраивают
  auto preset = createDefaultModel(&fixture.registry);

  REQUIRE(preset.has_value());
  fixture.model = std::make_unique<SynthModel>(*preset);
  fixture.start();

  std::atomic<bool> isRunning = true;
  AudioResult audio;

  std::thread audioThread([&fixture, &isRunning, &audio] {
    audio = runAudio(fixture.engine, isRunning, 7);
  });

  RandomEditor editor = { std::mt19937(42), *fixture.model };

  for (int step = 0; step < 4000; step++) {
    editor.step();
    fixture.engine.applyModel(*fixture.model);
    fixture.engine.update();

    if (step % 500 == 499) {
      fixture.engine.replaceModel(*fixture.model);
    }

    if (step % 4 == 0) {
      std::this_thread::yield();
    }
  }

  isRunning = false;
  audioThread.join();

  CHECK(audio.blocks > 0);
  CHECK(audio.badSamples == 0);
  CHECK(audio.memoryOperations == 0);

  // После остановки движок звучит так же, как новый, собранный из той же модели
  fixture.engine.setLive(false);
  fixture.engine.applyModel(*fixture.model);
  fixture.engine.reset();

  Engine fresh;

  fresh.prepare(SAMPLE_RATE);
  fresh.applyModel(*fixture.model);
  fresh.reset();

  const auto play = [](Engine& engine) {
    Samples left(4096);
    Samples right(4096);

    engine.noteOn(note(60), 1.0f);
    engine.process(left.data(), right.data(), (uint32_t)left.size());

    return left;
  };

  CHECK(play(fixture.engine) == play(fresh));
}

TEST_CASE("Обмен планами: новый план заменяет незабранный") {
  PlanExchange exchange;
  ExecutionPlan* first = exchange.adopt(std::make_unique<ExecutionPlan>());
  ExecutionPlan* second = exchange.adopt(std::make_unique<ExecutionPlan>());

  CHECK(exchange.take() == nullptr);

  exchange.publish(first);
  exchange.publish(second);

  CHECK(exchange.take() == second);
  CHECK(exchange.take() == nullptr);
}

TEST_CASE("Обмен планами: признак замены состояния наследует только план, заменивший незабранный") {
  PlanExchange exchange;
  ExecutionPlan* replacement = exchange.adopt(std::make_unique<ExecutionPlan>());
  ExecutionPlan* edit = exchange.adopt(std::make_unique<ExecutionPlan>());

  replacement->isReplacement = true;
  exchange.publish(replacement);

  SUBCASE("Замена не забрана") {
    exchange.publish(edit);
    CHECK(edit->isReplacement);
  }

  SUBCASE("Замену уже забрали") {
    CHECK(exchange.take() == replacement);
    exchange.publish(edit);
    CHECK_FALSE(edit->isReplacement);
  }
}

TEST_CASE("Обмен планами: память освобождается только после release") {
  PlanExchange exchange;
  auto item = std::make_shared<int>(1);
  const std::weak_ptr<int> watcher = item;

  ExecutionPlan* first = exchange.adopt(std::make_unique<ExecutionPlan>());

  exchange.bury(std::move(item));

  ExecutionPlan* second = exchange.adopt(std::make_unique<ExecutionPlan>());

  exchange.release(first->generation);
  exchange.collect();
  CHECK_FALSE(watcher.expired());

  exchange.release(second->generation);
  exchange.collect();
  CHECK(watcher.expired());
}
