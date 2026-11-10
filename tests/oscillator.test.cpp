#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

#include "../engine/oscillator/oscillator.h"

namespace {
  constexpr float SAMPLE_RATE = 48000.0f;
  constexpr float FREQ = 100.0f;

  /**
   * Секунда сигнала: число переходов через ноль вверх и наибольшая амплитуда
   */
  struct WaveStats {
    int risingCrossings = 0;
    float peak = 0.0f;
  };

  WaveStats measure(WAVE_TYPE wave, float volume) {
    Oscillator osc;
    osc.setSampleRate(SAMPLE_RATE);
    osc.setFreq(FREQ);
    osc.setVolume(volume);
    osc.setWave(wave);

    WaveStats stats;
    float previous = 0.0f;

    for (uint32_t i = 0; i < (uint32_t)SAMPLE_RATE; i++) {
      osc.doSample();

      if (previous < 0.0f && osc.currentSample >= 0.0f) {
        stats.risingCrossings++;
      }

      stats.peak = std::fmax(stats.peak, std::fabs(osc.currentSample));
      previous = osc.currentSample;
    }

    return stats;
  }
}

TEST_CASE("Осциллятор выдаёт заданную частоту и амплитуду на всех формах волны") {
  for (const WAVE_TYPE wave : { WAVE_TYPE::SIN, WAVE_TYPE::SQR, WAVE_TYPE::SAW, WAVE_TYPE::TNG }) {
    CAPTURE((int)wave);

    const WaveStats stats = measure(wave, 1.0f);

    // Волна может начинаться с нуля, тогда первый переход за секунду не считается
    CHECK(std::abs(stats.risingCrossings - (int)FREQ) <= 1);
    CHECK(stats.peak == doctest::Approx(1.0f).epsilon(0.05));
  }
}

TEST_CASE("Громкость масштабирует амплитуду") {
  CHECK(measure(WAVE_TYPE::SIN, 0.25f).peak == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("Форма волны для экрана совпадает со звуком осциллятора (D-108)") {
  // На низкой частоте сглаживание разрывов затрагивает только соседние с ними сэмплы
  constexpr float LOW_FREQ = 1.0f;
  constexpr int POINTS = 200;

  for (const WAVE_TYPE wave : { WAVE_TYPE::SIN, WAVE_TYPE::SQR, WAVE_TYPE::SAW, WAVE_TYPE::TNG }) {
    CAPTURE((int)wave);

    for (int index = 1; index < POINTS; index++) {
      const float phase = (float)index / (float)POINTS + 0.001f;

      Oscillator osc;
      osc.setSampleRate(SAMPLE_RATE);
      osc.setFreq(LOW_FREQ);
      osc.setVolume(1.0f);
      osc.setWave(wave);
      osc.setPhase(phase);
      osc.doSample();

      CAPTURE(phase);
      CHECK(osc.currentSample == doctest::Approx(shapeWave(wave, phase)).epsilon(1e-4));
    }
  }

  // В точке разрыва — значение после него, как у осциллятора на следующем сэмпле
  CHECK(shapeWave(WAVE_TYPE::SAW, 0.0f) == -1.0f);
  CHECK(shapeWave(WAVE_TYPE::SQR, 0.0f) == 1.0f);
  CHECK(shapeWave(WAVE_TYPE::SQR, 0.5f) == -1.0f);
  CHECK(shapeWave(WAVE_TYPE::TNG, 0.5f) == 1.0f);
}
