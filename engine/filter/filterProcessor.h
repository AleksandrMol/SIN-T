#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "./filter.h"

extern const ModuleDescriptor FILTER_DESCRIPTOR;

/**
 * Фильтр как модуль графа: у каждого канала своя история
 */
class FilterProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void process(const ProcessChunk& chunk) override;

  private:
    Filter left;
    Filter right;
};
