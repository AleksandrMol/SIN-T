#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Предел параметра polyphony. Память под голоса задаётся этим числом заранее: аудиопоток её не выделяет
constexpr uint32_t MAX_POLYPHONY = 64;

// Слотов вдвое больше лимита: новая нота получает свободный слот, пока забранный голос затихает (D-044)
constexpr uint32_t SLOTS_PER_VOICE = 2;
constexpr uint32_t MAX_VOICE_SLOTS = MAX_POLYPHONY * SLOTS_PER_VOICE;

// Педаль sustain нажата на канале MIDI
constexpr uint32_t MIDI_CHANNEL_COUNT = 16;
using SustainPedals = std::array<bool, MIDI_CHANNEL_COUNT>;

/**
 * Адрес ноты в CLAP. В адресе события -1 означает «любое значение»
 */
struct NoteAddress {
  int32_t noteId;
  int16_t port;
  int16_t channel;
  int16_t key;
};

/**
 * Подходит ли нота под адрес события. Нота без note_id подходит под любой:
 * хост мог выдать ID только в одном из событий пары, а зависшая нота хуже лишнего совпадения
 */
bool matchesNote(const NoteAddress& event, const NoteAddress& note);

/**
 * Нажата ли педаль на канале. Канал вне MIDI педали не имеет
 */
bool isPedalDown(const SustainPedals& pedals, int16_t channel);

enum class VOICE_STAGE {
  IDLE, // Свободен
  HELD, // Клавиша нажата
  RELEASED, // Клавиша отпущена, голос дозвучивает
  FADING, // Быстрое затухание перед освобождением
};

struct VoiceState {
  VOICE_STAGE stage = VOICE_STAGE::IDLE;
  NoteAddress note = { -1, -1, -1, -1 };
  float velocity = 0.0f;
  uint64_t order = 0; // Порядковый номер ноты: чем меньше, тем голос старше
  float fade = 1.0f; // Текущая громкость затухания
  bool isSustained = false; // Клавиша отпущена, но голос держит педаль: для нот он всё ещё нажат
};

/**
 * Пул голосов генератора: только учёт, без DSP. Перед освобождением голос затухает, поэтому
 * слотов вдвое больше лимита: новая нота получает свободный слот, пока забранный голос затихает
 */
class VoicePool {
  public:
    VoicePool();

    /**
     * Задаёт лимит одновременно звучащих голосов, 1..MAX_POLYPHONY. Лишние голоса затихают в порядке
     * D-042. Слоты только прибавляются: на старших могут звучать голоса. Память не выделяет,
     * поэтому вызывается и из аудиопотока
     */
    void setLimit(uint32_t limit);

    /**
     * Занимает голос и возвращает номер его слота. Если лимит исчерпан, забирает голос (D-042):
     * самый старый из отпущенных, а если таких нет, самый старый из удерживаемых
     */
    uint32_t noteOn(const NoteAddress& note, float velocity);

    /**
     * Отпускает удерживаемые голоса с подходящим адресом. Голос на канале с нажатой педалью остаётся
     * удерживаемым, пока педаль не отпустят (D-058)
     */
    void noteOff(const NoteAddress& note, const SustainPedals& pedals = {});

    /**
     * Педаль канала отпущена: голоса, которые она держала, отпускаются
     */
    void releaseSustained(int16_t channel);

    /**
     * Запускает затухание голосов с подходящим адресом, в каком бы состоянии они ни были
     */
    void choke(const NoteAddress& note);

    void startFade(uint32_t slot);

    /**
     * Записывает громкость затухания. На нуле голос освобождается
     */
    void setFade(uint32_t slot, float fade);

    /**
     * Сразу освобождает все голоса
     */
    void clear();

    const std::vector<VoiceState>& getVoices() const;

    /**
     * Слот голоса с самой поздней нотой среди звучащих или -1, если голосов нет (D-004)
     */
    int findLastPressed() const;

  private:
    uint32_t limit = 0;
    uint64_t nextOrder = 0;
    std::vector<VoiceState> voices;

    uint32_t countSounding() const;
    uint32_t findVictim() const;
    uint32_t findFreeSlot() const;
};
