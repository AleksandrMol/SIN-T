#include "./generatorProcessor.h"

#include <algorithm>
#include <cmath>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_PITCH_BEND_RANGE = 1;

  // Длительность быстрого затухания голоса (D-042, D-051)
  constexpr float FADE_SECONDS = 0.01f;

  // Страховка для отпущенного голоса: -100 дБ дольше 0,5 с (D-051)
  constexpr float SILENCE_LEVEL = 1e-5f;
  constexpr float SILENCE_SECONDS = 0.5f;
}

void GeneratorProcessor::prepare(float sampleRate) {
  this->fadeStep = 1.0f / (FADE_SECONDS * sampleRate);
  this->silenceLimit = (uint32_t)(SILENCE_SECONDS * sampleRate);

  for (ModuleSlots& voice : this->voiceSlots) {
    for (auto& [id, slot] : voice) {
      slot.processor->prepare(sampleRate);
    }
  }
}

void GeneratorProcessor::reset() {
  this->pool.clear();
}

void GeneratorProcessor::setPolyphony(uint32_t polyphony) {
  this->pool.setLimit(polyphony);

  const size_t slots = this->pool.getVoices().size();

  this->voiceSlots.resize(slots);
  this->velocities.resize(slots);
  this->segmentStarts.resize(slots);
  this->silentFrames.resize(slots);
}

std::vector<ModuleSlots>& GeneratorProcessor::getVoiceSlots() {
  return this->voiceSlots;
}

std::vector<float>& GeneratorProcessor::getVelocities() {
  return this->velocities;
}

std::map<ModuleId, std::vector<float>>& GeneratorProcessor::getLastOutputs() {
  return this->lastOutputs;
}

VoiceContext GeneratorProcessor::makeContext(uint32_t slot, const ParameterRamp* parameters, float pitchBend) const {
  const VoiceState& voice = this->pool.getVoices()[slot];

  return {
    .pitch = (float)voice.note.key + pitchBend * parameters[PARAM_PITCH_BEND_RANGE].end,
    .velocity = voice.velocity,
    .pitchBend = pitchBend,
    .gate = voice.stage == VOICE_STAGE::HELD,
  };
}

void GeneratorProcessor::noteOn(
  const NoteAddress& note,
  float velocity,
  const PlanGenerator& plan,
  uint32_t position,
  const ModulationClock& clock
) {
  const uint32_t slot = this->pool.noteOn(note, velocity);

  this->resetVoice(slot);
  this->velocities[slot] = velocity;
  this->silentFrames[slot] = 0;
  this->segmentStarts[slot] = position;

  startModulation(
    plan.subgraph->modulation[slot],
    this->makeContext(slot, plan.parameters, clock.pitchBend),
    clock.tempo
  );

  // Участок голоса заканчивается на границе сетки. Если нота попала на границу, участок начнётся сам
  if (position > 0) {
    this->modulateVoice(slot, plan, clock, MAX_CHUNK_FRAMES - position);
  }
}

void GeneratorProcessor::noteOff(const NoteAddress& note) {
  this->pool.noteOff(note);
}

void GeneratorProcessor::choke(const NoteAddress& note) {
  this->pool.choke(note);
}

void GeneratorProcessor::resetVoice(uint32_t slot) {
  for (auto& [id, moduleSlot] : this->voiceSlots[slot]) {
    moduleSlot.processor->reset();
  }
}

void GeneratorProcessor::modulateVoice(
  uint32_t slot,
  const PlanGenerator& plan,
  const ModulationClock& clock,
  uint32_t frames
) {
  const VoiceModulation& modulation = plan.subgraph->modulation[slot];

  runModulators(modulation, this->makeContext(slot, plan.parameters, clock.pitchBend), frames, clock.tempo);
  updateParameters(modulation);
}

void GeneratorProcessor::modulateVoices(const PlanGenerator& plan, const ModulationClock& clock) {
  const std::vector<VoiceState>& voices = this->pool.getVoices();

  for (uint32_t slot = 0; slot < voices.size(); slot++) {
    if (voices[slot].stage == VOICE_STAGE::IDLE) {
      continue;
    }

    // Тишина считается только у отпущенных голосов. Проверка здесь, а не в process:
    // только граница сетки не зависит от буфера DAW
    if (this->silentFrames[slot] >= this->silenceLimit) {
      this->pool.startFade(slot);
    }

    this->segmentStarts[slot] = 0;
    this->modulateVoice(slot, plan, clock, MAX_CHUNK_FRAMES);
  }

  this->updateLastValues(*plan.subgraph);
}

/**
 * Обновляет значения для глобальных получателей. Когда голосов нет, значение равно 0
 */
void GeneratorProcessor::updateLastValues(const GraphPlan& subgraph) {
  const int last = this->pool.findLastPressed();

  if (last >= 0) {
    for (const ValueCopy& copy : subgraph.modulation[(size_t)last].lastValues) {
      *copy.target = *copy.source;
    }
  } else {
    for (const ValueCopy& copy : subgraph.modulation[0].lastValues) {
      *copy.target = 0.0f;
    }
  }
}

bool GeneratorProcessor::areHoldersFinished(uint32_t slot, const GraphPlan& subgraph) const {
  return std::ranges::all_of(subgraph.modulation[slot].holders, [](const ModuleProcessor* holder) {
    return holder->isFinished();
  });
}

/**
 * Считает по сэмплам, а не по кускам: разрезы кусков зависят от буфера DAW
 */
void GeneratorProcessor::countSilence(uint32_t slot, const AudioBuffer& voice, uint32_t frames) {
  uint32_t silent = this->silentFrames[slot];

  for (uint32_t frame = 0; frame < frames; frame++) {
    const bool isQuiet = std::fabs(voice.left[frame]) <= SILENCE_LEVEL && std::fabs(voice.right[frame]) <= SILENCE_LEVEL;

    silent = isQuiet ? silent + 1 : 0;
  }

  this->silentFrames[slot] = silent;
}

/**
 * Прибавляет голос к выходу. Затухающий голос меняет громкость посэмплово, поэтому щелчка нет
 */
void GeneratorProcessor::addVoice(uint32_t slot, const AudioBuffer& voice, AudioBuffer& output, uint32_t frames) {
  const VoiceState& state = this->pool.getVoices()[slot];
  const float step = state.stage == VOICE_STAGE::FADING ? this->fadeStep : 0.0f;
  float gain = state.fade;

  for (uint32_t frame = 0; frame < frames; frame++) {
    output.left[frame] += voice.left[frame] * gain;
    output.right[frame] += voice.right[frame] * gain;
    gain = std::max(gain - step, 0.0f);
  }

  this->pool.setFade(slot, gain);
}

void GeneratorProcessor::process(const ProcessChunk& chunk) {
  AudioBuffer& output = *chunk.outputs[0];

  std::fill_n(output.left.begin(), chunk.frames, 0.0f);
  std::fill_n(output.right.begin(), chunk.frames, 0.0f);

  const std::vector<VoiceState>& voices = this->pool.getVoices();

  for (uint32_t slot = 0; slot < voices.size(); slot++) {
    const VoiceState& voice = voices[slot];

    if (voice.stage == VOICE_STAGE::IDLE) {
      continue;
    }

    // Конец всегда идёт через затухание: ENV мог стоять не на громкости (D-051)
    if (voice.stage == VOICE_STAGE::RELEASED && this->areHoldersFinished(slot, *chunk.subgraph)) {
      this->pool.startFade(slot);
    }

    const uint32_t segmentStart = this->segmentStarts[slot];
    const VoiceContext context = this->makeContext(slot, chunk.parameters, chunk.voice->pitchBend);

    runGraph(*chunk.subgraph, slot, {
      .frames = chunk.frames,
      .rampOffset = chunk.rampOffset - segmentStart,
      .rampFrames = MAX_CHUNK_FRAMES - segmentStart,
    }, context);

    const AudioBuffer& voiceOutput = *chunk.subgraph->output;

    if (voice.stage == VOICE_STAGE::RELEASED) {
      this->countSilence(slot, voiceOutput, chunk.frames);
    }

    this->addVoice(slot, voiceOutput, output, chunk.frames);
  }
}
