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

#include "../engine/defaultPreset/defaultPreset.h"
#include "../engine/engine/engine.h"
#include "../engine/moduleCatalog/moduleCatalog.h"
#include "../engine/oscillator/oscillator.h"
#include "../engine/preset/preset.h"
#include "../engine/synthModel/synthModel.h"
#include "./allocationCounter.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr uint32_t FADE_FRAMES = 480; // 10 мс
  constexpr uint32_t CHUNK_FRAMES = 32;
  constexpr uint32_t SETTLE_FRAMES = FADE_FRAMES + CHUNK_FRAMES;
  constexpr int16_t KEY_A3 = 57;
  constexpr int16_t KEY_A4 = 69;
  constexpr int16_t KEY_A5 = 81;

  using Samples = std::vector<float>;

  NoteAddress note(int16_t key, int32_t noteId = -1) {
    return { noteId, 0, 0, key };
  }

  /**
   * Модель и движок с настоящими модулями: ноты идут через generator и его подграф
   */
  struct PolyFixture {
    ModuleRegistry registry;
    std::unique_ptr<SynthModel> model;
    Engine engine;

    PolyFixture() {
      REQUIRE(addAllModules(this->registry).has_value());

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

    void setEnabled(ModuleId id, bool enabled) {
      REQUIRE(this->model->apply(SetModuleEnabled{ id, enabled }).has_value());
    }

    /**
     * Меняет параметр из аудиопотока, как автоматизация хоста: со сглаживанием ~10 мс
     */
    void setLive(ModuleId id, const std::string& key, float value) {
      const auto& parameters = this->model->findModule(id)->descriptor->parameters;
      const auto parameter = std::ranges::find(parameters, key, &ParameterDescriptor::key);

      REQUIRE(parameter != parameters.end());
      this->engine.setParameter(id, (uint32_t)(parameter - parameters.begin()), value);
    }

    void connect(ModuleId from, ModuleId to) {
      REQUIRE(this->model->apply(Connect{ { from, "out", to, "in" } }).has_value());
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

    ModuleId addSine(ModuleId generator, float level = 0.5f) {
      const ModuleId osc = this->add("oscillator", generator);

      this->set(osc, "wave", (float)WAVE_TYPE::SIN);
      this->set(osc, "level", level);
      this->set(osc, "rand", 0.0f); // Синус с нулевой фазы: тесты сравнивают с ним

      return osc;
    }

    /**
     * Генератор с одним синусом, подключённый к выходу
     */
    ModuleId addSineGenerator(float polyphony = 16.0f, float level = 0.5f) {
      const ModuleId generator = this->add("generator");

      this->set(generator, "polyphony", polyphony);
      this->connect(this->addSine(generator, level), this->getVoiceOutput(generator));
      this->connect(generator, this->model->getOutputId());

      return generator;
    }

    void apply() {
      this->engine.applyModel(*this->model);
    }

    std::pair<Samples, Samples> runStereo(uint32_t frames, uint32_t blockSize = 256) {
      Samples left(frames);
      Samples right(frames);

      for (uint32_t done = 0; done < frames; done += blockSize) {
        const uint32_t count = std::min(blockSize, frames - done);

        this->engine.process(left.data() + done, right.data() + done, count);
      }

      return { std::move(left), std::move(right) };
    }

    Samples run(uint32_t frames, uint32_t blockSize = 256) {
      auto [left, right] = this->runStereo(frames, blockSize);

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

  float maxJump(const Samples& samples) {
    float result = 0.0f;

    for (size_t i = 1; i < samples.size(); i++) {
      result = std::fmax(result, std::fabs(samples[i] - samples[i - 1]));
    }

    return result;
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

  Samples sum(const Samples& a, const Samples& b) {
    Samples result = a;

    for (size_t i = 0; i < result.size(); i++) {
      result[i] += b[i];
    }

    return result;
  }
}

TEST_CASE("Движок без модели принимает события и молчит") {
  Engine engine;
  Samples left(100, 1.0f);
  Samples right(100, 1.0f);

  engine.noteOn(note(KEY_A4), 1.0f);
  engine.noteOff(note(KEY_A4));
  engine.choke(note(KEY_A4));
  engine.reset();
  engine.process(left.data(), right.data(), 100);

  CHECK(peak(left) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Без нот генератор молчит") {
  this->addSineGenerator();
  this->apply();

  CHECK(peak(this->run(1000)) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Высоту голоса задаёт нота") {
  this->addSineGenerator();
  this->apply();

  int16_t key = 0;
  int crossings = 0;

  SUBCASE("A4") {
    key = KEY_A4;
    crossings = 440;
  }

  SUBCASE("Октавой выше") {
    key = KEY_A5;
    crossings = 880;
  }

  SUBCASE("Октавой ниже") {
    key = KEY_A3;
    crossings = 220;
  }

  this->engine.noteOn(note(key), 1.0f);

  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - crossings) <= 1);
}

TEST_CASE_FIXTURE(PolyFixture, "Параметры semi, coarse и fine сдвигают высоту ноты") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addSine(generator);

  this->connect(osc, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());

  float crossings = 0.0f;

  SUBCASE("Semi: октава вверх") {
    this->set(osc, "semi", 12.0f);
    crossings = 880.0f;
  }

  // 6,6 и 7 полутонов различаются на 16 Гц, а ошибка счёта переходов не больше 1,5
  SUBCASE("Semi ходит только по полутонам") {
    this->set(osc, "semi", 6.6f);
    crossings = 440.0f * std::pow(2.0f, 7.0f / 12.0f);
  }

  SUBCASE("Coarse меняет высоту плавно (D-103)") {
    this->set(osc, "coarse", 6.6f);
    crossings = 440.0f * std::pow(2.0f, 6.6f / 12.0f);
  }

  SUBCASE("Fine: 50 центов вниз") {
    this->set(osc, "fine", -50.0f);
    crossings = 440.0f * std::pow(2.0f, -0.5f / 12.0f);
  }

  SUBCASE("Сдвиги складываются") {
    this->set(osc, "semi", 12.0f);
    this->set(osc, "coarse", -0.5f);
    this->set(osc, "fine", 50.0f);
    crossings = 880.0f;
  }

  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(std::fabs((float)countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - crossings) <= 1.5f);
}

TEST_CASE_FIXTURE(PolyFixture, "Осциллятор вне генератора звучит на A4 и не реагирует на ноты") {
  const ModuleId osc = this->add("oscillator");

  this->set(osc, "wave", (float)WAVE_TYPE::SIN);
  this->set(osc, "level", 0.5f);
  this->connect(osc, this->model->getOutputId());
  this->apply();
  this->engine.noteOn(note(KEY_A5), 1.0f);

  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - 440) <= 1);
}

TEST_CASE_FIXTURE(PolyFixture, "Панорама сохраняет мощность, в центре звук прежний (D-104)") {
  const ModuleId osc = this->add("oscillator");

  this->set(osc, "wave", (float)WAVE_TYPE::SIN);
  this->set(osc, "level", 0.5f);
  this->connect(osc, this->model->getOutputId());

  float pan = 0.0f;

  SUBCASE("Центр") {
    pan = 0.0f;
  }

  SUBCASE("Левый край") {
    pan = -100.0f;
  }

  SUBCASE("Половина вправо") {
    pan = 50.0f;
  }

  SUBCASE("Почти правый край") {
    pan = 90.0f;
  }

  CAPTURE(pan);
  this->set(osc, "pan", pan);
  this->apply();

  // Синус 440 Гц: за 4800 сэмплов пик успевает попасть почти на вершину
  const auto [left, right] = this->runStereo(SETTLE_FRAMES + 4800);
  const float leftPeak = peak(Samples(left.begin() + SETTLE_FRAMES, left.end()));
  const float rightPeak = peak(Samples(right.begin() + SETTLE_FRAMES, right.end()));
  const float angle = (pan / 100.0f + 1.0f) * std::numbers::pi_v<float> / 4.0f;

  CHECK(leftPeak == doctest::Approx(0.5f * std::numbers::sqrt2_v<float> * std::cos(angle)).epsilon(0.001));
  CHECK(rightPeak == doctest::Approx(0.5f * std::numbers::sqrt2_v<float> * std::sin(angle)).epsilon(0.001));

  // Сумма мощностей каналов как у прежнего моно, где каждый канал был 0,5
  CHECK(leftPeak * leftPeak + rightPeak * rightPeak == doctest::Approx(0.5f).epsilon(0.002));

  if (pan == 0.0f) {
    CHECK(left == right);
  }
}

TEST_CASE_FIXTURE(PolyFixture, "Панорама на лету меняется плавно, без ступенек на границах кусков") {
  const ModuleId osc = this->add("oscillator");

  this->set(osc, "wave", (float)WAVE_TYPE::SIN);
  this->set(osc, "level", 0.5f);
  this->connect(osc, this->model->getOutputId());
  this->apply();
  this->runStereo(SETTLE_FRAMES);

  this->setLive(osc, "pan", 100.0f);

  const auto [left, right] = this->runStereo(SETTLE_FRAMES);

  // L = s·(cos φ − sin φ), R = s·(cos φ + sin φ), поэтому (R − L) / (R + L) = tg φ, где сигнал не у нуля.
  // Сглаживание ~10 мс ведёт tg φ от 0 до 1 за ~15 кусков: без интерполяции внутри куска шаг был бы ~0,07
  float previous = 0.0f;
  float largestStep = 0.0f;

  for (size_t i = 0; i < left.size(); i++) {
    if (std::fabs(left[i] + right[i]) < 0.2f) {
      continue;
    }

    const float tangent = (right[i] - left[i]) / (right[i] + left[i]);

    CHECK(tangent >= previous - 1e-4f);
    largestStep = std::fmax(largestStep, tangent - previous);
    previous = tangent;
  }

  CHECK(previous == doctest::Approx(1.0f).epsilon(0.001));
  CHECK(largestStep < 0.035f);
}

namespace {
  // Шаг фазы синуса A4 за сэмпл, радианы
  const float A4_STEP = 2.0f * std::numbers::pi_v<float> * 440.0f / SAMPLE_RATE;

  /**
   * Фаза синуса в градусах 0..360 по двум соседним сэмплам: s0 = A·sin θ, s1 = A·sin(θ + ω).
   * Амплитуда не нужна, поэтому затухания и velocity не мешают
   */
  float measurePhase(float first, float second, float step) {
    const float radians = std::atan2(first * std::sin(step), second - first * std::cos(step));
    const float degrees = radians * 180.0f / std::numbers::pi_v<float>;

    return std::fmod(degrees + 720.0f, 360.0f);
  }
}

TEST_CASE_FIXTURE(PolyFixture, "Phase сдвигает волну, в том числе у звучащей ноты (D-105)") {
  const ModuleId first = this->add("oscillator");
  const ModuleId second = this->add("oscillator");

  for (const ModuleId osc : { first, second }) {
    this->set(osc, "wave", (float)WAVE_TYPE::SIN);
    this->set(osc, "level", 0.5f);
    this->set(osc, "rand", 0.0f);
    this->connect(osc, this->model->getOutputId());
  }

  // Сумма двух синусов со сдвигом φ: амплитуда 2·0,5·|cos(φ/2)|
  const auto check = [&](float shift, float expected) {
    SUBCASE("Задано до звука") {
      this->set(second, "phase", shift);
      this->apply();
    }

    SUBCASE("Повёрнуто во время звука") {
      this->apply();
      this->run(SETTLE_FRAMES);
      this->setLive(second, "phase", shift);
    }

    this->run(SETTLE_FRAMES);

    CHECK(std::fabs(peak(this->run(4800)) - expected) < 0.002f);
  };

  SUBCASE("В фазе") {
    check(0.0f, 1.0f);
  }

  SUBCASE("Четверть периода") {
    check(90.0f, std::numbers::sqrt2_v<float> / 2.0f);
  }

  SUBCASE("Противофаза") {
    check(180.0f, 0.0f);
  }
}

TEST_CASE_FIXTURE(PolyFixture, "Rand разбрасывает начальную фазу ноты от Phase на Rand × 360° (D-105)") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addSine(generator);

  this->connect(osc, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->set(osc, "phase", 90.0f);

  float rand = 0.0f;

  SUBCASE("Rand 0: каждая нота с Phase") {
    rand = 0.0f;
  }

  SUBCASE("Rand 50: в пределах половины периода") {
    rand = 50.0f;
  }

  CAPTURE(rand);
  this->set(osc, "rand", rand);
  this->apply();

  // Фаза меряется на 500-м сэмпле ноты и пересчитывается к её началу
  constexpr uint32_t MEASURE_FRAME = 500;
  constexpr int NOTE_COUNT = 64;
  float lowest = 360.0f;
  float highest = 0.0f;
  int upperHalf = 0;

  for (int noteIndex = 0; noteIndex < NOTE_COUNT; noteIndex++) {
    this->engine.noteOn(note(KEY_A4), 1.0f);

    const Samples audio = this->run(MEASURE_FRAME + 2);
    const float atFrame = measurePhase(audio[MEASURE_FRAME], audio[MEASURE_FRAME + 1], A4_STEP);
    const float elapsed = std::fmod((float)MEASURE_FRAME * A4_STEP * 180.0f / std::numbers::pi_v<float>, 360.0f);
    const float start = std::fmod(atFrame - elapsed - 90.0f + 720.0f + 0.5f, 360.0f) - 0.5f;

    lowest = std::fmin(lowest, start);
    highest = std::fmax(highest, start);
    upperHalf += start > rand / 100.0f * 180.0f ? 1 : 0;

    this->engine.noteOff(note(KEY_A4));
    this->run(2000);
  }

  CHECK(lowest >= -0.5f);
  CHECK(highest <= rand / 100.0f * 360.0f + 0.5f);

  // Новая фаза у каждой ноты: в верхнюю половину разброса попадает примерно половина нот
  if (rand > 0.0f) {
    CHECK(lowest < 20.0f);
    CHECK(highest > 160.0f);
    CHECK(upperHalf > NOTE_COUNT / 4);
    CHECK(upperHalf < NOTE_COUNT * 3 / 4);
  }
}

TEST_CASE_FIXTURE(PolyFixture, "Случайная фаза своя у каждого осциллятора голоса и у каждого голоса (D-105)") {
  const ModuleId generator = this->add("generator");
  const ModuleId first = this->addSine(generator, 0.25f);

  this->set(first, "rand", 100.0f);
  this->connect(first, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());

  // С одинаковыми фазами синусы сложились бы в пик ровно 2·0,25 на осциллятор
  SUBCASE("Два осциллятора в одном голосе") {
    const ModuleId second = this->addSine(generator, 0.25f);

    this->set(second, "rand", 100.0f);
    this->connect(second, this->getVoiceOutput(generator));
    this->apply();
    this->engine.noteOn(note(KEY_A4), 1.0f);
  }

  SUBCASE("Два голоса на одной клавише") {
    this->apply();
    this->engine.noteOn(note(KEY_A4, 1), 1.0f);
    this->engine.noteOn(note(KEY_A4, 2), 1.0f);
  }

  CHECK(peak(this->run(4800)) < 0.49f);
}

TEST_CASE_FIXTURE(PolyFixture, "Самая высокая нота с максимальным сдвигом остаётся в пределах сигнала") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addSine(generator);

  // Пила зависит от фазы напрямую: выше предела частоты её значение уходит за границы
  this->set(osc, "wave", (float)WAVE_TYPE::SAW);
  this->set(osc, "semi", 48.0f);
  this->set(osc, "coarse", 48.0f);
  this->connect(osc, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->apply();
  this->engine.noteOn(note(127), 1.0f);

  const Samples audio = this->run(4800);

  CHECK(peak(audio) < 1.5f);
  CHECK(peak(audio) > 0.1f);
}

TEST_CASE_FIXTURE(PolyFixture, "Отпущенная нота затихает за 10 мс и освобождает голос (D-043)") {
  this->addSineGenerator(1.0f);
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(1000);
  this->engine.noteOff(note(KEY_A4));

  const Samples tail = this->run(SETTLE_FRAMES);

  CHECK(peak(Samples(tail.begin(), tail.begin() + 100)) > 0.1f);
  CHECK(peak(Samples(tail.begin() + (long)FADE_FRAMES, tail.end())) == 0.0f);
  CHECK(peak(this->run(1000)) == 0.0f);
}

TEST_CASE("Длительность затухания не зависит от частоты дискретизации (D-043)") {
  const float sampleRate = GENERATE(44100.0f, 48000.0f, 96000.0f);
  const uint32_t fadeFrames = (uint32_t)(sampleRate * 0.01f);

  CAPTURE(sampleRate);

  PolyFixture fixture;

  fixture.engine.prepare(sampleRate);
  fixture.addSineGenerator(1.0f);
  fixture.apply();

  fixture.engine.noteOn(note(KEY_A4), 1.0f);
  fixture.run(1000);
  fixture.engine.noteOff(note(KEY_A4));

  const Samples tail = fixture.run(fadeFrames + CHUNK_FRAMES);

  // Последняя пятая часть затухания: голос ещё слышен, сразу после неё уже тишина
  const auto fadeEnd = tail.begin() + (long)fadeFrames;

  CHECK(peak(Samples(fadeEnd - (long)(fadeFrames / 5), fadeEnd - (long)(fadeFrames / 20))) > 0.001f);
  CHECK(peak(Samples(fadeEnd + (long)CHUNK_FRAMES / 2, tail.end())) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Затухание после отпускания идёт без щелчка") {
  this->addSineGenerator(1.0f);
  this->apply();

  // 110 Гц: четверть периода — около пика синуса, резкий обрыв дал бы скачок на всю амплитуду
  this->engine.noteOn(note(45), 1.0f);
  this->run(109);
  this->engine.noteOff(note(45));

  CHECK(maxJump(this->run(SETTLE_FRAMES)) < 0.02f);
}

TEST_CASE_FIXTURE(PolyFixture, "Голоса суммируются (ТЗ §5.3)") {
  this->addSineGenerator();
  this->apply();

  PolyFixture first;
  first.addSineGenerator();
  first.apply();

  PolyFixture second;
  second.addSineGenerator();
  second.apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->engine.noteOn(note(KEY_A5), 1.0f);
  first.engine.noteOn(note(KEY_A4), 1.0f);
  second.engine.noteOn(note(KEY_A5), 1.0f);

  checkClose(this->run(2000), sum(first.run(2000), second.run(2000)));
}

TEST_CASE_FIXTURE(PolyFixture, "У каждого голоса своё состояние подграфа") {
  const auto buildFiltered = [](PolyFixture& fixture) {
    const ModuleId generator = fixture.add("generator");
    const ModuleId osc = fixture.addSine(generator);
    const ModuleId filter = fixture.add("filter", generator);

    fixture.set(filter, "cutoff", 300.0f);
    fixture.connect(osc, filter);
    fixture.connect(filter, fixture.getVoiceOutput(generator));
    fixture.connect(generator, fixture.model->getOutputId());
    fixture.apply();
  };

  buildFiltered(*this);

  PolyFixture first;
  PolyFixture second;

  buildFiltered(first);
  buildFiltered(second);

  this->engine.noteOn(note(KEY_A3), 1.0f);
  first.engine.noteOn(note(KEY_A3), 1.0f);

  const Samples start = sum(first.run(500), second.run(500));

  checkClose(this->run(500), start);

  this->engine.noteOn(note(KEY_A5), 1.0f);
  second.engine.noteOn(note(KEY_A5), 1.0f);

  checkClose(this->run(2000), sum(first.run(2000), second.run(2000)));
}

TEST_CASE_FIXTURE(PolyFixture, "Занятый пул забирает самый старый голос, новая нота звучит (D-042)") {
  this->addSineGenerator(2.0f);
  this->apply();

  PolyFixture expected;
  expected.addSineGenerator(2.0f);
  expected.apply();

  this->engine.noteOn(note(KEY_A3), 1.0f);
  this->run(320);

  this->engine.noteOn(note(KEY_A4), 1.0f);
  expected.engine.noteOn(note(KEY_A4), 1.0f);

  this->run(320);
  expected.run(320);

  this->engine.noteOn(note(KEY_A5), 1.0f);
  expected.engine.noteOn(note(KEY_A5), 1.0f);

  // Пока забранный голос затихает, он ещё слышен, потом остаются только две новые ноты
  this->run(SETTLE_FRAMES);
  expected.run(SETTLE_FRAMES);

  checkClose(this->run(2000), expected.run(2000));
}

TEST_CASE_FIXTURE(PolyFixture, "Забранный голос затихает без щелчка (D-042)") {
  this->addSineGenerator(1.0f);
  this->apply();

  this->engine.noteOn(note(45), 1.0f);
  this->run(109);

  // Новая нота той же высоты начинается с нуля, поэтому щёлкнуть мог бы только старый голос
  this->engine.noteOn(note(45), 1.0f);

  CHECK(maxJump(this->run(SETTLE_FRAMES)) < 0.02f);
}

TEST_CASE_FIXTURE(PolyFixture, "Голос с прошлой нотой начинает новую с чистого состояния") {
  const auto build = [](PolyFixture& fixture) {
    const ModuleId generator = fixture.add("generator");
    const ModuleId osc = fixture.addSine(generator);
    const ModuleId filter = fixture.add("filter", generator);

    fixture.set(generator, "polyphony", 1.0f);
    fixture.connect(osc, filter);
    fixture.connect(filter, fixture.getVoiceOutput(generator));
    fixture.connect(generator, fixture.model->getOutputId());
    fixture.apply();
  };

  build(*this);

  PolyFixture fresh;
  build(fresh);

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(1003);
  this->engine.noteOff(note(KEY_A4));
  this->run(SETTLE_FRAMES);

  // Движки на разных позициях сетки: важно только, что голос начинает с нуля
  this->engine.noteOn(note(KEY_A5), 1.0f);
  fresh.engine.noteOn(note(KEY_A5), 1.0f);

  checkClose(this->run(1000), fresh.run(1000));
}

TEST_CASE_FIXTURE(PolyFixture, "Несколько генераторов одновременно: у каждого свои голоса (ТЗ §5.1)") {
  const ModuleId limited = this->addSineGenerator(1.0f, 0.3f);
  const ModuleId roomy = this->addSineGenerator(4.0f, 0.2f);

  CHECK(limited != roomy);
  this->apply();

  PolyFixture limitedOnly;
  limitedOnly.addSineGenerator(1.0f, 0.3f);
  limitedOnly.apply();

  PolyFixture roomyOnly;
  roomyOnly.addSineGenerator(4.0f, 0.2f);
  roomyOnly.apply();

  for (const int16_t key : { KEY_A3, KEY_A4 }) {
    this->engine.noteOn(note(key), 1.0f);
    limitedOnly.engine.noteOn(note(key), 1.0f);
    roomyOnly.engine.noteOn(note(key), 1.0f);
  }

  this->run(SETTLE_FRAMES);
  limitedOnly.run(SETTLE_FRAMES);
  roomyOnly.run(SETTLE_FRAMES);

  // Первому генератору хватает одного голоса, второй держит оба: лимиты независимы
  checkClose(this->run(2000), sum(limitedOnly.run(2000), roomyOnly.run(2000)));
}

TEST_CASE_FIXTURE(PolyFixture, "Сумма голосов идёт дальше по главному графу") {
  const ModuleId generator = this->add("generator");
  const ModuleId filter = this->add("filter");

  this->connect(this->addSine(generator), this->getVoiceOutput(generator));
  this->connect(generator, filter);
  this->connect(filter, this->model->getOutputId());
  this->set(filter, "cutoff", 20.0f);
  this->apply();

  this->engine.noteOn(note(KEY_A5), 1.0f);

  const float muffled = peak(this->run(2000));

  this->set(filter, "cutoff", 20000.0f);
  this->apply();

  CHECK(muffled < 0.05f);
  CHECK(peak(this->run(2000)) > 0.3f);
}

TEST_CASE_FIXTURE(PolyFixture, "Генератор без пути к выходу не получает ноты") {
  const ModuleId generator = this->add("generator");

  this->connect(this->addSine(generator), this->getVoiceOutput(generator));
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(500)) == 0.0f);

  // После подключения звучит только новая нота: старая в генератор не попала
  this->connect(generator, this->model->getOutputId());
  this->apply();

  CHECK(peak(this->run(500)) == 0.0f);

  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(500)) > 0.3f);
}

TEST_CASE_FIXTURE(PolyFixture, "Подграф без пути к выходу голоса молчит") {
  const ModuleId generator = this->add("generator");

  this->addSine(generator);
  this->connect(generator, this->model->getOutputId());
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(500)) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Note off по note_id отпускает только свой голос") {
  this->addSineGenerator();
  this->apply();

  this->engine.noteOn(note(KEY_A4, 1), 1.0f);
  this->engine.noteOn(note(KEY_A4, 2), 1.0f);

  const float both = peak(this->run(500));

  this->engine.noteOff(note(KEY_A4, 1));
  this->run(SETTLE_FRAMES);

  const float single = peak(this->run(500));

  CHECK(both == doctest::Approx(1.0f).epsilon(0.01));
  CHECK(single == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE_FIXTURE(PolyFixture, "Choke глушит ноту быстро и без щелчка") {
  this->addSineGenerator();
  this->apply();

  this->engine.noteOn(note(45), 1.0f);
  this->run(109);
  this->engine.choke(note(45));

  CHECK(maxJump(this->run(SETTLE_FRAMES)) < 0.02f);
  CHECK(peak(this->run(100)) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Pitch bend сдвигает высоту на диапазон своего генератора (D-027)") {
  const ModuleId first = this->addSineGenerator();

  this->set(first, "pitchBendRange", 12.0f);
  this->apply();

  PolyFixture wide;
  const ModuleId wideGenerator = wide.addSineGenerator();

  wide.set(wideGenerator, "pitchBendRange", 24.0f);
  wide.apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  wide.engine.noteOn(note(KEY_A4), 1.0f);

  this->engine.setPitchBend(1.0f);
  wide.engine.setPitchBend(1.0f);

  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - 880) <= 1);
  CHECK(std::abs(countRisingCrossings(wide.run((uint32_t)SAMPLE_RATE)) - 1760) <= 1);

  this->engine.setPitchBend(-1.0f);
  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - 220) <= 1);

  this->engine.setPitchBend(0.0f);
  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - 440) <= 1);
}

TEST_CASE_FIXTURE(PolyFixture, "Pitch bend не сдвигает осциллятор вне генератора (D-027)") {
  const ModuleId osc = this->add("oscillator");

  this->set(osc, "wave", (float)WAVE_TYPE::SIN);
  this->set(osc, "level", 0.5f);
  this->connect(osc, this->model->getOutputId());
  this->apply();
  this->engine.setPitchBend(1.0f);

  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - 440) <= 1);
}

TEST_CASE_FIXTURE(PolyFixture, "Amp умножает сигнал на gain и velocity (D-043)") {
  const ModuleId generator = this->add("generator");
  const ModuleId amp = this->add("amp", generator);

  this->connect(this->addSine(generator), amp);
  this->connect(amp, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());

  float gain = 1.0f;
  float sensitivity = 1.0f;
  float velocity = 1.0f;
  float expected = 0.5f;

  SUBCASE("Полная громкость") {}

  SUBCASE("Gain") {
    gain = 0.5f;
    expected = 0.25f;
  }

  SUBCASE("Velocity при полной чувствительности") {
    velocity = 0.5f;
    expected = 0.25f;
  }

  SUBCASE("Velocity без чувствительности не влияет") {
    sensitivity = 0.0f;
    velocity = 0.25f;
  }

  SUBCASE("Частичная чувствительность") {
    sensitivity = 0.5f;
    velocity = 0.0f;
    expected = 0.25f;
  }

  SUBCASE("Нулевая velocity при полной чувствительности молчит") {
    velocity = 0.0f;
    expected = 0.0f;
  }

  this->set(amp, "gain", gain);
  this->set(amp, "velocity", sensitivity);
  this->apply();
  this->engine.noteOn(note(KEY_A4), velocity);

  CHECK(peak(this->run(2000)) == doctest::Approx(expected).epsilon(0.01));
}

TEST_CASE_FIXTURE(PolyFixture, "У голосов своя velocity") {
  const ModuleId generator = this->add("generator");
  const ModuleId amp = this->add("amp", generator);

  this->connect(this->addSine(generator), amp);
  this->connect(amp, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->apply();

  this->engine.noteOn(note(KEY_A4, 1), 1.0f);
  this->engine.noteOn(note(KEY_A4, 2), 0.5f);

  // Голоса в фазе: 0.5 + 0.25
  CHECK(peak(this->run(2000)) == doctest::Approx(0.75f).epsilon(0.01));
}

TEST_CASE_FIXTURE(PolyFixture, "Отключённый генератор молчит и не получает ноты (D-040)") {
  const ModuleId generator = this->addSineGenerator();

  this->setEnabled(generator, false);
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(500)) == 0.0f);

  this->setEnabled(generator, true);
  this->apply();

  CHECK(peak(this->run(500)) == 0.0f);

  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(500)) > 0.3f);
}

TEST_CASE_FIXTURE(PolyFixture, "Отключённые модули подграфа работают как в главном графе (D-040)") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addSine(generator);
  const ModuleId filter = this->add("filter", generator);

  this->set(filter, "cutoff", 20.0f);
  this->connect(osc, filter);
  this->connect(filter, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());

  SUBCASE("Отключённый фильтр пропускает сигнал") {
    this->setEnabled(filter, false);
    this->apply();
    this->engine.noteOn(note(KEY_A5), 1.0f);

    CHECK(peak(this->run(2000)) > 0.3f);
  }

  SUBCASE("Отключённый осциллятор молчит") {
    this->setEnabled(osc, false);
    this->apply();
    this->engine.noteOn(note(KEY_A5), 1.0f);

    CHECK(peak(this->run(2000)) == 0.0f);
  }
}

TEST_CASE_FIXTURE(PolyFixture, "Reset сразу освобождает голоса и сбрасывает pitch bend") {
  this->addSineGenerator();
  this->apply();

  this->engine.setPitchBend(1.0f);
  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->run(100);
  this->engine.reset();

  CHECK(peak(this->run(100)) == 0.0f);

  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(std::abs(countRisingCrossings(this->run((uint32_t)SAMPLE_RATE)) - 440) <= 1);
}

TEST_CASE_FIXTURE(PolyFixture, "Смена плана не сбрасывает звучащие голоса (D-023)") {
  const ModuleId generator = this->addSineGenerator();

  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  PolyFixture whole;
  whole.addSineGenerator();
  whole.apply();
  whole.engine.noteOn(note(KEY_A4), 1.0f);

  Samples parts = this->run(400);

  this->set(generator, "pitchBendRange", 7.0f);
  this->apply();

  const Samples rest = this->run(600);

  parts.insert(parts.end(), rest.begin(), rest.end());

  CHECK(parts == whole.run(1000));
}

TEST_CASE_FIXTURE(PolyFixture, "Изменение полифонии меняет лимит") {
  const ModuleId generator = this->addSineGenerator(1.0f);

  this->apply();

  this->engine.noteOn(note(KEY_A4, 1), 1.0f);
  this->engine.noteOn(note(KEY_A4, 2), 1.0f);
  this->run(SETTLE_FRAMES);

  CHECK(peak(this->run(500)) == doctest::Approx(0.5f).epsilon(0.01));

  this->set(generator, "polyphony", 2.0f);
  this->apply();
  this->engine.noteOn(note(KEY_A4, 3), 1.0f);
  this->run(SETTLE_FRAMES);

  CHECK(peak(this->run(500)) > 0.6f);
}

TEST_CASE_FIXTURE(PolyFixture, "Новый модуль подграфа создаётся для всех голосов") {
  const ModuleId generator = this->add("generator");

  this->connect(generator, this->model->getOutputId());
  this->apply();

  this->connect(this->addSine(generator), this->getVoiceOutput(generator));
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);
  this->engine.noteOn(note(KEY_A5), 1.0f);

  CHECK(peak(this->run(1000)) > 0.5f);
}

TEST_CASE_FIXTURE(PolyFixture, "Удалённый модуль подграфа исчезает из всех голосов") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addSine(generator);

  this->connect(osc, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->apply();

  REQUIRE(this->model->apply(RemoveModule{ osc }).has_value());
  this->apply();

  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(1000)) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Удаление генератора убирает его из нот") {
  const ModuleId generator = this->addSineGenerator();

  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  REQUIRE(this->model->apply(RemoveModule{ generator }).has_value());
  this->apply();
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(peak(this->run(1000)) == 0.0f);
}

TEST_CASE_FIXTURE(PolyFixture, "Звук генератора после сохранения и загрузки не меняется") {
  const ModuleId generator = this->add("generator");
  const ModuleId filter = this->add("filter", generator);

  this->set(filter, "cutoff", 800.0f);
  this->connect(this->addSine(generator), filter);
  this->connect(filter, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->apply();

  auto loaded = loadModel(&this->registry, saveModel(*this->model));
  REQUIRE_MESSAGE(loaded.has_value(), loaded.error());

  Engine other;

  other.prepare(SAMPLE_RATE);
  other.applyModel(*loaded);
  other.noteOn(note(KEY_A4), 1.0f);

  Samples left(1000);
  Samples right(1000);

  other.process(left.data(), right.data(), 1000);
  this->engine.noteOn(note(KEY_A4), 1.0f);

  CHECK(this->run(1000) == left);
}

TEST_CASE_FIXTURE(PolyFixture, "Пресет по умолчанию: рабочая цепочка, которая сразу играет (ТЗ §9.1)") {
  auto model = createDefaultModel(&this->registry);

  REQUIRE_MESSAGE(model.has_value(), model.error());

  std::vector<std::string> types;

  for (const auto& [id, module] : model->getModules()) {
    types.push_back(module.descriptor->type);
  }

  std::sort(types.begin(), types.end());

  CHECK(types == std::vector<std::string>{ "amp", "env", "filter", "generator", "macros", "oscillator", "output", "voiceOutput" });
  CHECK(model->getRoutes().size() == 1);

  this->engine.applyModel(*model);

  CHECK(peak(this->run(1000)) == 0.0f);

  this->engine.noteOn(note(60), 1.0f);

  const float loud = peak(this->run(2000));

  this->engine.noteOff(note(60));

  // Release 200 мс: голос ещё слышен, спадает и освобождается (D-051). Базовый gain равен 0,
  // иначе вклад ENV упёрся бы в ограничение и спада не было бы слышно (D-025)
  CHECK(peak(this->run(4800)) > 0.0f);

  this->run(2400);

  CHECK(peak(this->run(2400)) < loud * 0.5f);

  this->run(SETTLE_FRAMES);

  CHECK(loud > 0.01f);
  CHECK(peak(this->run(1000)) == 0.0f);

  const auto reloaded = loadModel(&this->registry, saveModel(*model));

  REQUIRE(reloaded.has_value());
  CHECK(*reloaded == *model);
}

TEST_CASE_FIXTURE(PolyFixture, "События и обработка голосов не выделяют память (D-023)") {
  const ModuleId generator = this->add("generator");
  const ModuleId osc = this->addSine(generator);
  const ModuleId filter = this->add("filter", generator);
  const ModuleId amp = this->add("amp", generator);

  this->set(generator, "polyphony", 4.0f);
  this->connect(osc, filter);
  this->connect(osc, amp);
  this->connect(filter, amp);
  this->connect(amp, this->getVoiceOutput(generator));
  this->connect(generator, this->model->getOutputId());
  this->apply();

  Samples left(256);
  Samples right(256);

  const size_t before = getAllocationCount();

  for (int block = 0; block < 200; block++) {
    const int16_t key = (int16_t)(40 + block % 40);

    this->engine.noteOn(note(key), 1.0f);
    this->engine.noteOn(note((int16_t)(key + 1)), 0.5f);
    this->engine.setPitchBend(block % 2 == 0 ? 0.5f : -0.5f);
    this->engine.process(left.data(), right.data(), 256);
    this->engine.noteOff(note(key));
    this->engine.process(left.data(), right.data(), 13);
    this->engine.choke(note((int16_t)(key + 1)));
  }

  this->engine.reset();

  CHECK(getAllocationCount() == before);
}
