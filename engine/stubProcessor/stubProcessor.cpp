#include "./stubProcessor.h"

void StubProcessor::prepare(float) {}

void StubProcessor::reset() {}

void StubProcessor::process(const ProcessChunk& chunk) {
  for (uint32_t output = 0; output < chunk.outputCount; output++) {
    AudioBuffer& target = *chunk.outputs[output];

    for (uint32_t frame = 0; frame < chunk.frames; frame++) {
      target.left[frame] = 0.0f;
      target.right[frame] = 0.0f;

      for (uint32_t input = 0; input < chunk.inputCount; input++) {
        target.left[frame] += chunk.inputs[input]->left[frame];
        target.right[frame] += chunk.inputs[input]->right[frame];
      }
    }
  }
}

std::unique_ptr<ModuleProcessor> createStubProcessor() {
  return std::make_unique<StubProcessor>();
}
