#include "./engine.h"

#include <algorithm>

namespace {
  void mixInputs(const PlanMix& mix, uint32_t frames) {
    std::fill_n(mix.target->left.begin(), frames, 0.0f);
    std::fill_n(mix.target->right.begin(), frames, 0.0f);

    for (const AudioBuffer* source : mix.sources) {
      for (uint32_t frame = 0; frame < frames; frame++) {
        mix.target->left[frame] += source->left[frame];
        mix.target->right[frame] += source->right[frame];
      }
    }
  }
}

void Engine::prepare(float sampleRate) {
  this->sampleRate = sampleRate;

  for (auto& [id, slot] : this->slots) {
    slot.processor->prepare(sampleRate);
  }
}

void Engine::applyModel(const SynthModel& model) {
  syncModuleSlots(model, this->slots, this->sampleRate);

  this->plan = compilePlan(model, this->slots);
}

void Engine::processChunk(uint32_t frames) {
  for (const PlanStep& step : this->plan->steps) {
    for (const PlanMix& mix : step.mixes) {
      mixInputs(mix, frames);
    }

    if (step.processor) {
      step.processor->process({
        .frames = frames,
        .inputCount = (uint32_t)step.inputs.size(),
        .outputCount = (uint32_t)step.outputs.size(),
        .inputs = step.inputs.data(),
        .outputs = step.outputs.data(),
        .parameters = step.parameters,
      });
    }
  }
}

void Engine::process(float* left, float* right, uint32_t frames) {
  if (!this->plan) {
    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);
    return;
  }

  uint32_t done = 0;

  while (done < frames) {
    const uint32_t count = std::min(MAX_CHUNK_FRAMES - this->chunkPosition, frames - done);

    this->processChunk(count);

    std::copy_n(this->plan->output->left.begin(), count, left + done);
    std::copy_n(this->plan->output->right.begin(), count, right + done);

    done += count;
    this->chunkPosition = (this->chunkPosition + count) % MAX_CHUNK_FRAMES;
  }
}
