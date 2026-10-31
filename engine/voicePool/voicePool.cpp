#include "./voicePool.h"

#include <algorithm>

namespace {
  constexpr uint32_t SLOTS_PER_VOICE = 2;

  /**
   * Подходит ли голос под адрес события. Адрес голоса без note_id подходит под любой:
   * хост мог выдать ID только в одном из событий пары, а зависшая нота хуже лишнего совпадения
   */
  bool matches(const NoteAddress& event, const NoteAddress& voice) {
    return (event.noteId == -1 || voice.noteId == -1 || event.noteId == voice.noteId)
      && (event.port == -1 || event.port == voice.port)
      && (event.channel == -1 || event.channel == voice.channel)
      && (event.key == -1 || event.key == voice.key);
  }

  bool isSounding(const VoiceState& voice) {
    return voice.stage == VOICE_STAGE::HELD || voice.stage == VOICE_STAGE::RELEASED;
  }

  /**
   * Номер самого старого голоса на заданной стадии или -1, если таких нет
   */
  int findOldest(const std::vector<VoiceState>& voices, VOICE_STAGE stage) {
    int oldest = -1;

    for (size_t slot = 0; slot < voices.size(); slot++) {
      if (voices[slot].stage == stage && (oldest < 0 || voices[slot].order < voices[(size_t)oldest].order)) {
        oldest = (int)slot;
      }
    }

    return oldest;
  }
}

VoicePool::VoicePool() {
  this->setLimit(1);
}

void VoicePool::setLimit(uint32_t limit) {
  this->limit = std::max(limit, 1u);
  this->voices.resize(this->limit * SLOTS_PER_VOICE);
}

uint32_t VoicePool::noteOn(const NoteAddress& note, float velocity) {
  while (this->countSounding() >= this->limit) {
    this->startFade(this->findVictim());
  }

  const uint32_t slot = this->findFreeSlot();

  this->voices[slot] = {
    .stage = VOICE_STAGE::HELD,
    .note = note,
    .velocity = velocity,
    .order = this->nextOrder++,
    .fade = 1.0f,
  };

  return slot;
}

void VoicePool::noteOff(const NoteAddress& note) {
  for (VoiceState& voice : this->voices) {
    if (voice.stage == VOICE_STAGE::HELD && matches(note, voice.note)) {
      voice.stage = VOICE_STAGE::RELEASED;
    }
  }
}

void VoicePool::choke(const NoteAddress& note) {
  for (VoiceState& voice : this->voices) {
    if (isSounding(voice) && matches(note, voice.note)) {
      voice.stage = VOICE_STAGE::FADING;
    }
  }
}

void VoicePool::startFade(uint32_t slot) {
  this->voices[slot].stage = VOICE_STAGE::FADING;
}

void VoicePool::setFade(uint32_t slot, float fade) {
  VoiceState& voice = this->voices[slot];

  voice.fade = fade;

  if (fade <= 0.0f) {
    voice.stage = VOICE_STAGE::IDLE;
  }
}

void VoicePool::clear() {
  std::fill(this->voices.begin(), this->voices.end(), VoiceState{});
}

const std::vector<VoiceState>& VoicePool::getVoices() const {
  return this->voices;
}

uint32_t VoicePool::countSounding() const {
  return (uint32_t)std::count_if(this->voices.begin(), this->voices.end(), isSounding);
}

uint32_t VoicePool::findVictim() const {
  const int released = findOldest(this->voices, VOICE_STAGE::RELEASED);

  return (uint32_t)(released >= 0 ? released : findOldest(this->voices, VOICE_STAGE::HELD));
}

/**
 * Первый свободный слот. Если все заняты затуханиями, забирается самое старое из них: это щелчок,
 * но такое возможно, только если за время затухания пришло больше нот, чем лимит голосов
 */
uint32_t VoicePool::findFreeSlot() const {
  const auto free = std::find_if(this->voices.begin(), this->voices.end(), [](const VoiceState& voice) {
    return voice.stage == VOICE_STAGE::IDLE;
  });

  return free != this->voices.end()
    ? (uint32_t)(free - this->voices.begin())
    : (uint32_t)findOldest(this->voices, VOICE_STAGE::FADING);
}
