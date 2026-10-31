#pragma once

#include <cstdint>
#include <vector>

/**
 * Адрес ноты в CLAP. В адресе события -1 означает «любое значение»
 */
struct NoteAddress {
  int32_t noteId;
  int16_t port;
  int16_t channel;
  int16_t key;
};

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
};

/**
 * Пул голосов генератора: только учёт, без DSP. Перед освобождением голос затухает, поэтому
 * слотов вдвое больше лимита: новая нота получает свободный слот, пока забранный голос затихает
 */
class VoicePool {
  public:
    VoicePool();

    /**
     * Задаёт лимит одновременно звучащих голосов, не меньше 1. Вызывается не из аудиопотока
     */
    void setLimit(uint32_t limit);

    /**
     * Занимает голос и возвращает номер его слота. Если лимит исчерпан, забирает голос (D-042):
     * самый старый из отпущенных, а если таких нет, самый старый из удерживаемых
     */
    uint32_t noteOn(const NoteAddress& note, float velocity);

    /**
     * Отпускает удерживаемые голоса с подходящим адресом
     */
    void noteOff(const NoteAddress& note);

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

  private:
    uint32_t limit = 0;
    uint64_t nextOrder = 0;
    std::vector<VoiceState> voices;

    uint32_t countSounding() const;
    uint32_t findVictim() const;
    uint32_t findFreeSlot() const;
};
