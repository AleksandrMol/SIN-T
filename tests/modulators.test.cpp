#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "../engine/builtinModules/builtinModules.h"
#include "../engine/env/envProcessor.h"
#include "../engine/lfo/lfoProcessor.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr float TOLERANCE = 1e-3f;

  /**
   * ENV с заданными параметрами: продвигается напрямую, без движка
   */
  struct EnvFixture {
    EnvProcessor env;
    std::vector<ParameterRamp> parameters;
    VoiceContext context = { REFERENCE_PITCH, 1.0f, 0.0f, true };

    EnvFixture() {
      this->env.prepare(SAMPLE_RATE);
      this->setParameters(0.01f, 0.02f, 0.5f, 0.04f);
      this->env.reset();
    }

    void setParameters(float attack, float decay, float sustain, float release) {
      this->parameters = { { attack, attack }, { decay, decay }, { sustain, sustain }, { release, release } };
    }

    float advance(uint32_t frames) {
      float output = -1.0f;

      this->env.modulate({
        .frames = frames,
        .parameters = this->parameters.data(),
        .voice = &this->context,
        .tempo = 120.0f,
        .outputs = &output,
      });

      return output;
    }

    void release() {
      this->context.gate = false;
    }
  };

  /**
   * LFO с заданными параметрами: продвигается напрямую, без движка
   */
  struct LfoFixture {
    LfoProcessor lfo;
    std::vector<ParameterRamp> parameters;
    VoiceContext context = { REFERENCE_PITCH, 1.0f, 0.0f, true };
    float tempo = 120.0f;

    LfoFixture() {
      this->lfo.prepare(SAMPLE_RATE);
      this->setParameters(0.0f, 1.0f, 0.0f, 7.0f);
    }

    void setParameters(float wave, float rate, float sync, float division) {
      this->parameters = { { wave, wave }, { rate, rate }, { sync, sync }, { division, division } };
    }

    float advance(uint32_t frames) {
      float output = -2.0f;

      this->lfo.modulate({
        .frames = frames,
        .parameters = this->parameters.data(),
        .voice = &this->context,
        .tempo = this->tempo,
        .outputs = &output,
      });

      return output;
    }

    /**
     * Сколько раз пила прошла полный период за заданное время
     */
    int countCycles(uint32_t frames) {
      int cycles = 0;
      float previous = this->advance(0);

      for (uint32_t done = 0; done < frames; done += 32) {
        const float value = this->advance(32);

        cycles += value < previous ? 1 : 0;
        previous = value;
      }

      return cycles;
    }
  };
}

TEST_CASE("ENV проходит attack, decay и sustain за заданное время (D-049)") {
  EnvFixture fixture;

  // Attack 10 мс, decay 20 мс от 1 до 0,5
  CHECK(fixture.advance(0) == 0.0f);
  CHECK(fixture.advance(240) == doctest::Approx(0.5f).epsilon(TOLERANCE));
  CHECK(fixture.advance(240) == doctest::Approx(1.0f).epsilon(TOLERANCE));
  CHECK(fixture.advance(480) == doctest::Approx(0.75f).epsilon(TOLERANCE));
  CHECK(fixture.advance(480) == doctest::Approx(0.5f).epsilon(TOLERANCE));
  CHECK(fixture.advance(4800) == 0.5f);
  CHECK_FALSE(fixture.env.isFinished());
}

TEST_CASE("ENV за один шаг проходит несколько стадий и не теряет остаток кадров") {
  EnvFixture fixture;

  // 480 кадров attack, остальные 480 — половина decay
  CHECK(fixture.advance(960) == doctest::Approx(0.75f).epsilon(TOLERANCE));
  CHECK(fixture.advance(480) == doctest::Approx(0.5f).epsilon(TOLERANCE));
}

TEST_CASE("Release линейно спадает до нуля за заданное время и завершает ENV (D-049)") {
  EnvFixture fixture;

  fixture.advance(2000);
  fixture.release();

  CHECK_FALSE(fixture.env.isFinished());
  CHECK(fixture.advance(960) == doctest::Approx(0.25f).epsilon(TOLERANCE));
  CHECK_FALSE(fixture.env.isFinished());
  CHECK(fixture.advance(900) > 0.0f);
  CHECK_FALSE(fixture.env.isFinished());
  CHECK(fixture.advance(100) == 0.0f);
  CHECK(fixture.env.isFinished());
  CHECK(fixture.advance(100) == 0.0f);
}

TEST_CASE("Release из attack идёт от текущего уровня, а его время не зависит от уровня") {
  EnvFixture fixture;

  fixture.advance(120);
  fixture.release();

  CHECK(fixture.advance(960) == doctest::Approx(0.125f).epsilon(TOLERANCE));
  CHECK_FALSE(fixture.env.isFinished());
  CHECK(fixture.advance(960) == 0.0f);
  CHECK(fixture.env.isFinished());
}

TEST_CASE("Отпускание с нулевого уровня сразу завершает ENV") {
  EnvFixture fixture;

  fixture.release();

  CHECK(fixture.advance(1) == 0.0f);
  CHECK(fixture.env.isFinished());
}

TEST_CASE("Нулевой шаг выдаёт уровень и не двигает ENV") {
  EnvFixture fixture;

  const float level = fixture.advance(100);

  CHECK(fixture.advance(0) == level);
  CHECK(fixture.advance(0) == level);
}

TEST_CASE("Новый ENV ещё не запущен и считается завершённым") {
  EnvProcessor env;

  CHECK(env.isFinished());
}

TEST_CASE("Сброс перезапускает ENV с нуля") {
  EnvFixture fixture;

  fixture.advance(2000);
  fixture.release();
  fixture.advance(4800);

  REQUIRE(fixture.env.isFinished());

  fixture.context.gate = true;
  fixture.env.reset();

  CHECK_FALSE(fixture.env.isFinished());
  CHECK(fixture.advance(0) == 0.0f);
  CHECK(fixture.advance(240) == doctest::Approx(0.5f).epsilon(TOLERANCE));
}

TEST_CASE("Sustain равный единице: decay сразу переходит к удержанию") {
  EnvFixture fixture;

  fixture.setParameters(0.01f, 0.02f, 1.0f, 0.04f);

  CHECK(fixture.advance(4800) == 1.0f);
}

TEST_CASE("Sustain следует за модуляцией, в том числе вверх") {
  EnvFixture fixture;

  fixture.advance(4800);
  fixture.setParameters(0.01f, 0.02f, 0.8f, 0.04f);

  CHECK(fixture.advance(32) == 0.8f);

  fixture.setParameters(0.01f, 0.02f, 0.3f, 0.04f);

  CHECK(fixture.advance(32) == 0.3f);
}

TEST_CASE("Sustain, поднятый во время decay выше уровня, останавливает спад") {
  EnvFixture fixture;

  fixture.advance(720);
  fixture.setParameters(0.01f, 0.02f, 0.95f, 0.04f);

  CHECK(fixture.advance(32) == 0.95f);
}

TEST_CASE("Выход ENV не зависит от длины шага") {
  EnvFixture coarse;
  EnvFixture fine;

  coarse.advance(3200);

  float level = 0.0f;

  for (int chunk = 0; chunk < 100; chunk++) {
    level = fine.advance(32);
  }

  CHECK(level == doctest::Approx(coarse.advance(0)).epsilon(TOLERANCE));
}

TEST_CASE("Время ENV не зависит от частоты дискретизации") {
  EnvFixture fixture;

  fixture.env.prepare(SAMPLE_RATE * 2.0f);

  CHECK(fixture.advance(480) == doctest::Approx(0.5f).epsilon(TOLERANCE));
}

TEST_CASE("Формы волны LFO (D-050)") {
  const uint32_t quarter = (uint32_t)(SAMPLE_RATE / 4.0f);
  LfoFixture fixture;

  SUBCASE("Синус") {
    CHECK(fixture.advance(0) == 0.0f);
    CHECK(fixture.advance(quarter) == doctest::Approx(1.0f));
    CHECK(fixture.advance(quarter) == doctest::Approx(0.0f).epsilon(TOLERANCE));
    CHECK(fixture.advance(quarter) == doctest::Approx(-1.0f));
  }

  SUBCASE("Треугольник") {
    fixture.setParameters(1.0f, 1.0f, 0.0f, 7.0f);

    CHECK(fixture.advance(0) == 0.0f);
    CHECK(fixture.advance(quarter / 2) == doctest::Approx(0.5f));
    CHECK(fixture.advance(quarter / 2) == doctest::Approx(1.0f));
    CHECK(fixture.advance(quarter) == doctest::Approx(0.0f));
    CHECK(fixture.advance(quarter) == doctest::Approx(-1.0f));
    CHECK(fixture.advance(quarter / 2) == doctest::Approx(-0.5f));
  }

  SUBCASE("Пила") {
    fixture.setParameters(2.0f, 1.0f, 0.0f, 7.0f);

    CHECK(fixture.advance(0) == -1.0f);
    CHECK(fixture.advance(quarter) == doctest::Approx(-0.5f));
    CHECK(fixture.advance(quarter) == doctest::Approx(0.0f));
    CHECK(fixture.advance(quarter) == doctest::Approx(0.5f));
  }

  SUBCASE("Меандр") {
    fixture.setParameters(3.0f, 1.0f, 0.0f, 7.0f);

    CHECK(fixture.advance(0) == 1.0f);
    CHECK(fixture.advance(quarter) == 1.0f);
    CHECK(fixture.advance(quarter + 1) == -1.0f);
    CHECK(fixture.advance(quarter) == -1.0f);
  }
}

TEST_CASE("Rate задаёт частоту LFO, а без Sync темп не влияет") {
  LfoFixture fixture;

  fixture.setParameters(2.0f, 10.0f, 0.0f, 7.0f);
  fixture.tempo = 60.0f;

  CHECK(std::abs(fixture.countCycles((uint32_t)SAMPLE_RATE) - 10) <= 1);
}

TEST_CASE("Sync: скорость задают Division и темп, Rate не влияет (D-050)") {
  LfoFixture fixture;
  float division = 0.0f;
  int cycles = 0;

  // За 8 секунд при 120 BPM проходит 16 четвертей
  SUBCASE("1/4") {
    division = 7.0f;
    cycles = 16;
  }

  SUBCASE("1/1") {
    division = 1.0f;
    cycles = 4;
  }

  SUBCASE("1/1 с точкой") {
    division = 0.0f;
    cycles = 3;
  }

  SUBCASE("1/16") {
    division = 13.0f;
    cycles = 64;
  }

  SUBCASE("Триоль 1/8") {
    division = 12.0f;
    cycles = 48;
  }

  SUBCASE("Триоль 1/32") {
    division = 17.0f;
    cycles = 192;
  }

  fixture.setParameters(2.0f, 50.0f, 1.0f, division);

  CHECK(std::abs(fixture.countCycles((uint32_t)(SAMPLE_RATE * 8.0f)) - cycles) <= 1);
}

TEST_CASE("Sync следует за темпом") {
  LfoFixture fixture;

  fixture.setParameters(2.0f, 1.0f, 1.0f, 7.0f);
  fixture.tempo = 240.0f;

  CHECK(std::abs(fixture.countCycles((uint32_t)(SAMPLE_RATE * 8.0f)) - 32) <= 1);
}

TEST_CASE("Сброс возвращает LFO к началу волны, нулевой шаг фазу не двигает") {
  LfoFixture fixture;

  const float value = fixture.advance(1000);

  CHECK(value != 0.0f);
  CHECK(fixture.advance(0) == value);

  fixture.lfo.reset();

  CHECK(fixture.advance(0) == 0.0f);
}

TEST_CASE("Частота LFO не зависит от частоты дискретизации") {
  LfoFixture fixture;

  fixture.lfo.prepare(SAMPLE_RATE * 2.0f);
  fixture.setParameters(2.0f, 10.0f, 0.0f, 7.0f);

  CHECK(std::abs(fixture.countCycles((uint32_t)(SAMPLE_RATE * 2.0f)) - 10) <= 1);
}

TEST_CASE("Самая низкая частота LFO не теряет фазу") {
  LfoFixture fixture;

  fixture.setParameters(2.0f, 0.01f, 0.0f, 7.0f);

  float value = 0.0f;

  // 25 секунд из 100: четверть периода пилы
  for (uint32_t chunk = 0; chunk < 37500; chunk++) {
    value = fixture.advance(32);
  }

  CHECK(value == doctest::Approx(-0.5f).epsilon(TOLERANCE));
}

TEST_CASE("Описания ENV и LFO задают вид модулятора (D-048)") {
  CHECK(ENV_DESCRIPTOR.isVoiceModulator);
  CHECK_FALSE(LFO_DESCRIPTOR.isVoiceModulator);
  CHECK(ENV_DESCRIPTOR.isAllowedInPoly);
  CHECK(LFO_DESCRIPTOR.isAllowedInPoly);
  CHECK(ENV_DESCRIPTOR.modOutputs[0].polarity == MOD_POLARITY::UNIPOLAR);
  CHECK(LFO_DESCRIPTOR.modOutputs[0].polarity == MOD_POLARITY::BIPOLAR);
  CHECK(GENERATOR_DESCRIPTOR.modOutputs[0].key == "velocity");
  CHECK(GENERATOR_DESCRIPTOR.modOutputs[0].polarity == MOD_POLARITY::UNIPOLAR);
}
