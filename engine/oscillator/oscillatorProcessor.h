#pragma once

#include <array>
#include <cstdint>

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "../random/random.h"
#include "./oscillator.h"

extern const ModuleDescriptor OSCILLATOR_DESCRIPTOR;

// Наибольшее число подголосов унисона (D-106)
constexpr uint32_t MAX_UNISON = 16;

/**
 * Осциллятор как модуль графа. Унисон из нескольких подголосов, расстроенных и разведённых
 * по стерео (D-106). Волна каждого моно, по каналам её разводит панорама (D-104)
 */
class OscillatorProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void seed(uint64_t seed) override;
    void reset() override;
    void rewind() override;
    void process(const ProcessChunk& chunk) override;

  private:
    /**
     * Подголос унисона. Громкость и место меняются только на границах участков сетки,
     * внутри участка идут по прямой: звук не зависит от буфера DAW (D-008)
     */
    struct UnisonVoice {
      Oscillator oscillator;
      float gain = 0.0f; // 0..1: подголос появляется и уходит плавно
      float spread = 0.0f; // Место в унисоне -1..1: от него расстройка и положение в стерео
      bool isActive = false;
    };

    std::array<UnisonVoice, MAX_UNISON> voices{};
    Random random;
    float maxFrequency = 0.0f;
    float fadeStep = 0.0f; // Изменение громкости подголоса за сэмпл
    bool isStarting = true; // Начальная фаза ставится в первом куске: значение Rand приходит только с ним

    float toFrequency(float pitch) const;
    void startVoice(UnisonVoice& voice, float rand);
};
