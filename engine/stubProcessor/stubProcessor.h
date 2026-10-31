#pragma once

#include <memory>

#include "../moduleProcessor/moduleProcessor.h"

/**
 * Обработка заглушки (D-021): сумма входов идёт на каждый выход, без входов модуль молчит
 */
class StubProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void process(const ProcessChunk& chunk) override;
};

std::unique_ptr<ModuleProcessor> createStubProcessor();
