#include "./generatorProcessor.h"

#include <algorithm>

namespace {
  // Индексы в порядке parameters из описания
  constexpr uint32_t PARAM_PITCH_BEND_RANGE = 1;

  // Длительность быстрого затухания голоса (D-042, D-043)
  constexpr float FADE_SECONDS = 0.01f;
}

void GeneratorProcessor::prepare(float sampleRate) {
  this->fadeStep = 1.0f / (FADE_SECONDS * sampleRate);

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
  this->voiceSlots.resize(this->pool.getVoices().size());
}

std::vector<ModuleSlots>& GeneratorProcessor::getVoiceSlots() {
  return this->voiceSlots;
}

void GeneratorProcessor::noteOn(const NoteAddress& note, float velocity) {
  this->resetVoice(this->pool.noteOn(note, velocity));
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

  const float pitchBendRange = chunk.parameters[PARAM_PITCH_BEND_RANGE].end;
  const std::vector<VoiceState>& voices = this->pool.getVoices();

  for (uint32_t slot = 0; slot < voices.size(); slot++) {
    const VoiceState& voice = voices[slot];

    if (voice.stage == VOICE_STAGE::IDLE) {
      continue;
    }

    // ENV появится на этапе 4 (D-043). Без него отпущенному голосу нечего дозвучивать
    if (voice.stage == VOICE_STAGE::RELEASED) {
      this->pool.startFade(slot);
    }

    const VoiceContext context = {
      .pitch = (float)voice.note.key + chunk.voice->pitchBend * pitchBendRange,
      .velocity = voice.velocity,
      .pitchBend = chunk.voice->pitchBend,
    };

    runGraph(*chunk.subgraph, slot, chunk.frames, context);

    this->addVoice(slot, *chunk.subgraph->output, output, chunk.frames);
  }
}
