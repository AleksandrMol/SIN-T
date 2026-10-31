#include "./engine.h"

#include <algorithm>
#include <cmath>

#include "../generator/generatorProcessor.h"
#include "../modulationCompiler/modulationCompiler.h"

namespace {
  // Сглаживание базового значения (D-025)
  constexpr float SMOOTHING_SECONDS = 0.01f;
}

void Engine::prepare(float sampleRate) {
  this->sampleRate = sampleRate;

  for (auto& [id, slot] : this->slots) {
    slot.processor->prepare(sampleRate);
  }
}

void Engine::applyModel(const SynthModel& model) {
  syncModuleSlots(model, this->slots, this->sampleRate);
  syncParameterBases(model, this->bases);

  this->plan = compilePlan(model, this->slots, this->bases);
  this->startMainModulation();
}

void Engine::reset() {
  for (auto& [id, slot] : this->slots) {
    slot.processor->reset();
  }

  this->pitchBend = 0.0f;
  this->lastNoteGate = false;
  this->chunkPosition = 0;

  if (this->plan) {
    this->startMainModulation();
  }
}

/**
 * Значения главного графа принимаются сразу: иначе первый шаг сетки скользил бы от устаревших
 */
void Engine::startMainModulation() {
  startModulation(this->plan->main.modulation[0], this->makeContext(), this->tempo);
}

void Engine::noteOn(const NoteAddress& note, float velocity) {
  if (!this->plan) {
    return;
  }

  const ModulationClock clock = { this->tempo, this->pitchBend };

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->noteOn(note, velocity, generator, this->chunkPosition, clock);
  }

  // Глобальный экземпляр общего модулятора перезапускает каждая нота (D-005)
  for (ModuleProcessor* modulator : this->plan->sharedModulators) {
    modulator->reset();
  }

  this->lastNote = note;
  this->lastNoteGate = true;
}

void Engine::releaseLastNote(const NoteAddress& note) {
  if (matchesNote(note, this->lastNote)) {
    this->lastNoteGate = false;
  }
}

void Engine::noteOff(const NoteAddress& note) {
  if (!this->plan) {
    return;
  }

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->noteOff(note);
  }

  this->releaseLastNote(note);
}

void Engine::choke(const NoteAddress& note) {
  if (!this->plan) {
    return;
  }

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->choke(note);
  }

  this->releaseLastNote(note);
}

void Engine::setPitchBend(float value) {
  this->pitchBend = value;
}

void Engine::setTempo(float tempo) {
  this->tempo = tempo > 0.0f ? tempo : DEFAULT_TEMPO;
}

void Engine::setParameter(ModuleId module, uint32_t parameter, float value) {
  const auto values = this->bases.find(module);

  if (values == this->bases.end() || parameter >= values->second.size()) {
    return;
  }

  BaseValue& base = values->second[parameter];

  base.target = toNormalized(*base.descriptor, value);

  // Промежуточные ступени были бы слышны: тип волны прошёл бы через все волны между старой и новой
  const float chunks = std::max(std::round(SMOOTHING_SECONDS * this->sampleRate / MAX_CHUNK_FRAMES), 1.0f);

  base.step = base.descriptor->isStepped ? 1.0f : std::fabs(base.target - base.value) / chunks;

  this->isSmoothing = true;
}

void Engine::advanceBases() {
  if (!this->isSmoothing) {
    return;
  }

  this->isSmoothing = false;

  for (auto& [id, values] : this->bases) {
    for (BaseValue& base : values) {
      base.previous = base.value;

      if (base.value != base.target) {
        const float distance = base.target - base.value;

        base.value = std::fabs(distance) <= base.step ? base.target : base.value + std::copysign(base.step, distance);

        this->isSmoothing = true;
      }
    }
  }
}

VoiceContext Engine::makeContext() const {
  return {
    .pitch = REFERENCE_PITCH,
    .velocity = 1.0f,
    .pitchBend = this->pitchBend,
    .gate = this->lastNoteGate,
  };
}

/**
 * Считает модуляцию на следующий шаг сетки. Голоса идут после глобальных модуляторов и видят их текущие
 * значения, а глобальные модуляторы видят значения голосов с прошлого шага. Параметры главного графа
 * считаются последними и видят всё текущее (D-009, D-010)
 */
void Engine::beginChunk() {
  this->advanceBases();

  const VoiceModulation& modulation = this->plan->main.modulation[0];
  const ModulationClock clock = { this->tempo, this->pitchBend };

  runModulators(modulation, this->makeContext(), MAX_CHUNK_FRAMES, this->tempo);

  for (const PlanGenerator& generator : this->plan->generators) {
    generator.processor->modulateVoices(generator, clock);
  }

  updateParameters(modulation);
}

void Engine::processChunk(uint32_t frames) {
  const ChunkSpan span = { frames, this->chunkPosition, MAX_CHUNK_FRAMES };

  runGraph(this->plan->main, 0, span, this->makeContext());
}

void Engine::process(float* left, float* right, uint32_t frames) {
  if (!this->plan) {
    std::fill_n(left, frames, 0.0f);
    std::fill_n(right, frames, 0.0f);
    return;
  }

  uint32_t done = 0;

  while (done < frames) {
    if (this->chunkPosition == 0) {
      this->beginChunk();
    }

    const uint32_t count = std::min(MAX_CHUNK_FRAMES - this->chunkPosition, frames - done);

    this->processChunk(count);

    std::copy_n(this->plan->main.output->left.begin(), count, left + done);
    std::copy_n(this->plan->main.output->right.begin(), count, right + done);

    done += count;
    this->chunkPosition = (this->chunkPosition + count) % MAX_CHUNK_FRAMES;
  }
}
