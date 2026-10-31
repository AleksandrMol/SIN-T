#pragma once

#include "../moduleProcessor/moduleProcessor.h"

/**
 * Макросы: выход каждого равен его параметру, значение задаёт хост (D-011, D-055).
 * Звука нет, поэтому модуль живёт в глобальной зоне как глобальный LFO
 */
class MacrosProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void reset() override;
    void process(const ProcessChunk& chunk) override;
    void modulate(const ModulationChunk& chunk) override;
};
