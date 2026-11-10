#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "./oscillator.h"

extern const ModuleDescriptor OSCILLATOR_DESCRIPTOR;

/**
 * Осциллятор как модуль графа. Волна моно, по каналам её разводит панорама (D-104)
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
