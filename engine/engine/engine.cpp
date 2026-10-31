#include "./engine.h"

#include <algorithm>

#include "../generator/generatorProcessor.h"

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

void Engine::reset() {
  for (auto& [id, slot] : this->slots) {
    slot.processor->reset();
  }

  this->pitchBend = 0.0f;
  this->chunkPosition = 0;
}

void Engine::noteOn(const NoteAddress& note, float velocity) {
  if (!this->plan) {
    return;
  }

  for (GeneratorProcessor* generator : this->plan->generators) {
    generator->noteOn(note, velocity);
  }
}

void Engine::noteOff(const NoteAddress& note) {
  if (!this->plan) {
    return;
  }

  for (GeneratorProcessor* generator : this->plan->generators) {
    generator->noteOff(note);
  }
}

void Engine::choke(const NoteAddress& note) {
  if (!this->plan) {
    return;
  }

  for (GeneratorProcessor* generator : this->plan->generators) {
    generator->choke(note);
  }
}

void Engine::setPitchBend(float value) {
  this->pitchBend = value;
}

void Engine::processChunk(uint32_t frames) {
  const VoiceContext context = {
    .pitch = REFERENCE_PITCH,
    .velocity = 1.0f,
    .pitchBend = this->pitchBend,
  };

  runGraph(this->plan->main, 0, frames, context);
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

    std::copy_n(this->plan->main.output->left.begin(), count, left + done);
    std::copy_n(this->plan->main.output->right.begin(), count, right + done);

    done += count;
    this->chunkPosition = (this->chunkPosition + count) % MAX_CHUNK_FRAMES;
  }
}
