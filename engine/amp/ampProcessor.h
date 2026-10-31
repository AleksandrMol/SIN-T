#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"

extern const ModuleDescriptor AMP_DESCRIPTOR;

/**
 * Усилитель (D-043): умножает сигнал на gain и на чувствительность к velocity.
 * Velocity учитывается здесь, потому что вклады модуляции складываются, а не умножаются (D-025)
 */
class AmpProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void reset() override;
    void process(const ProcessChunk& chunk) override;
};
