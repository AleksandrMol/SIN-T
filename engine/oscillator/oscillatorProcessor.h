#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "./oscillator.h"

extern const ModuleDescriptor OSCILLATOR_DESCRIPTOR;

/**
 * Осциллятор как модуль графа. Сигнал моно, в оба канала идёт одно и то же
 */
class OscillatorProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void reset() override;
    void process(const ProcessChunk& chunk) override;

  private:
    Oscillator oscillator;
    float maxFrequency = 0.0f;

    float toFrequency(float pitch) const;
};
