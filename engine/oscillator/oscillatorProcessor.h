#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "../random/random.h"
#include "./oscillator.h"

extern const ModuleDescriptor OSCILLATOR_DESCRIPTOR;

/**
 * Осциллятор как модуль графа. Волна моно, по каналам её разводит панорама (D-104)
 */
class OscillatorProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void seed(uint64_t seed) override;
    void reset() override;
    void rewind() override;
    void process(const ProcessChunk& chunk) override;

  private:
    Oscillator oscillator;
    Random random;
    float maxFrequency = 0.0f;
    bool isStarting = true; // Начальная фаза ставится в первом куске: значение Rand приходит только с ним

    float toFrequency(float pitch) const;
};
