#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"

extern const ModuleDescriptor LFO_DESCRIPTOR;

/**
 * Низкочастотный осциллятор, выход -1..1 (D-050). В главном графе у него одна фаза на всех,
 * внутри генератора — своя на каждый голос, и она начинается заново с нотой (D-048)
 */
class LfoProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void reset() override;
    void process(const ProcessChunk& chunk) override;
    void modulate(const ModulationChunk& chunk) override;

  private:
    float sampleRate = 44100.0f;
    double phase = 0.0; // 0..1. В double, иначе на низких частотах шаг теряется при сложении
};
