#include "./voicePool.h"

#include <algorithm>

namespace {
  bool isSounding(const VoiceState& voice) {
    return voice.stage == VOICE_STAGE::HELD || voice.stage == VOICE_STAGE::RELEASED;
  }

  /**
   * Номер самого старого голоса, подходящего под условие, или -1, если таких нет
   */
  template <typename Predicate>
  int findOldestMatching(const std::vector<VoiceState>& voices, Predicate matches) {
    int oldest = -1;

    for (size_t slot = 0; slot < voices.size(); slot++) {
      if (matches(voices[slot]) && (oldest < 0 || voices[slot].order < voices[(size_t)oldest].order)) {
        oldest = (int)slot;
      }
    }

    return oldest;
  }

  int findOldest(const std::vector<VoiceState>& voices, VOICE_STAGE stage) {
    return findOldestMatching(voices, [stage](const VoiceState& voice) {
      return voice.stage == stage;
    });
  }
}

bool isPedalDown(const SustainPedals& pedals, int16_t channel) {
  return channel >= 0 && channel < (int16_t)MIDI_CHANNEL_COUNT && pedals[(size_t)channel];
}

bool matchesNote(const NoteAddress& event, const NoteAddress& note) {
  return (event.noteId == -1 || note.noteId == -1 || event.noteId == note.noteId)
    && (event.port == -1 || event.port == note.port)
    && (event.channel == -1 || event.channel == note.channel)
    && (event.key == -1 || event.key == note.key);
}

VoicePool::VoicePool() {
  this->voices.reserve(MAX_VOICE_SLOTS);
  this->setLimit(1);
}

void VoicePool::setLimit(uint32_t limit) {
  this->limit = std::clamp(limit, 1u, MAX_POLYPHONY);

  if (this->limit * SLOTS_PER_VOICE > this->voices.size()) {
    this->voices.resize(this->limit * SLOTS_PER_VOICE);
  }

  while (this->countSounding() > this->limit) {
    this->startFade(this->findVictim());
  }
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

void VoicePool::noteOff(const NoteAddress& note, const SustainPedals& pedals) {
  for (VoiceState& voice : this->voices) {
    if (voice.stage != VOICE_STAGE::HELD || !matchesNote(note, voice.note)) {
      continue;
    }

    if (isPedalDown(pedals, voice.note.channel)) {
      voice.isSustained = true;
    } else {
      voice.stage = VOICE_STAGE::RELEASED;
    }
  }
}

void VoicePool::releaseSustained(int16_t channel) {
  for (VoiceState& voice : this->voices) {
    if (voice.stage == VOICE_STAGE::HELD && voice.isSustained && voice.note.channel == channel) {
      voice.stage = VOICE_STAGE::RELEASED;
    }
  }
}

void VoicePool::choke(const NoteAddress& note) {
  for (VoiceState& voice : this->voices) {
    if (isSounding(voice) && matchesNote(note, voice.note)) {
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

int VoicePool::findLastPressed() const {
  int last = -1;

  for (size_t slot = 0; slot < this->voices.size(); slot++) {
    const VoiceState& voice = this->voices[slot];

    if (voice.stage != VOICE_STAGE::IDLE && (last < 0 || voice.order > this->voices[(size_t)last].order)) {
      last = (int)slot;
    }
  }

  return last;
}

uint32_t VoicePool::countSounding() const {
  return (uint32_t)std::count_if(this->voices.begin(), this->voices.end(), isSounding);
}

uint32_t VoicePool::findVictim() const {
  const int released = findOldest(this->voices, VOICE_STAGE::RELEASED);

  if (released >= 0) {
    return (uint32_t)released;
  }

  // Голос под педалью уже не под пальцами: удерживаемый аккорд важнее (D-042, D-058)
  const int sustained = findOldestMatching(this->voices, [](const VoiceState& voice) {
    return voice.stage == VOICE_STAGE::HELD && voice.isSustained;
  });

  return (uint32_t)(sustained >= 0 ? sustained : findOldest(this->voices, VOICE_STAGE::HELD));
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
