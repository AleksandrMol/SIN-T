#include <doctest/doctest.h>

#include <cstdint>

#include "../engine/envelope/envelope.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr uint32_t SAMPLES_PER_MS = 48;

  // Значения из настроек огибающей по умолчанию
  constexpr uint32_t ATTACK_MS = 5;
  constexpr uint32_t RELEASE_MS = 1200;

  void advance(Envelope& env, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
      env.doSample();
    }
  }
}

TEST_CASE("Огибающая нарастает за время атаки и затухает за время релиза") {
  Envelope env;
  env.setSampleRate(SAMPLE_RATE);

  bool ended = false;
  env.onEnd = [&ended]() {
    ended = true;
  };

  env.noteOn();
  advance(env, ATTACK_MS * SAMPLES_PER_MS / 2);
  CHECK(env.currentValue == doctest::Approx(0.5f).epsilon(0.02));

  advance(env, ATTACK_MS * SAMPLES_PER_MS);
  CHECK(env.currentValue == doctest::Approx(1.0f));
  CHECK_FALSE(ended);

  env.noteOff();
  advance(env, RELEASE_MS * SAMPLES_PER_MS / 2);
  CHECK(env.currentValue == doctest::Approx(0.5f).epsilon(0.02));
  CHECK_FALSE(ended);

  advance(env, RELEASE_MS * SAMPLES_PER_MS);
  CHECK(env.currentValue == 0.0f);
  CHECK(ended);
}
