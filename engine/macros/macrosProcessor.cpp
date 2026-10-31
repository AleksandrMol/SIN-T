#include "./macrosProcessor.h"

#include "../builtinModules/builtinModules.h"

void MacrosProcessor::prepare(float) {}

void MacrosProcessor::reset() {}

void MacrosProcessor::process(const ProcessChunk&) {}

void MacrosProcessor::modulate(const ModulationChunk& chunk) {
  for (uint32_t index = 0; index < MACRO_COUNT; index++) {
    chunk.outputs[index] = chunk.parameters[index].end;
  }
}
