#pragma once

#include <vector>

#include "../executionPlan/executionPlan.h"
#include "../moduleProcessor/moduleProcessor.h"
#include "../voicePool/voicePool.h"

/**
 * Генератор как модуль главного графа (D-004): пул голосов и по набору состояний подграфа
 * на каждый слот. План подграфа приходит с куском, поэтому состояния живут отдельно от него (D-023).
 * Выход генератора — сумма его голосов
 */
class GeneratorProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;

    /**
     * Сразу освобождает все голоса. Их состояния сбросит следующая нота
     */
    void reset() override;
    void process(const ProcessChunk& chunk) override;

    /**
     * События нот. Вызываются из аудиопотока между кусками
     */
    void noteOn(const NoteAddress& note, float velocity);
    void noteOff(const NoteAddress& note);
    void choke(const NoteAddress& note);

    /**
     * Меняет лимит голосов. Вызывается не из аудиопотока
     */
    void setPolyphony(uint32_t polyphony);

    /**
     * Состояния подграфа по слотам голосов. Размер задаёт setPolyphony
     */
    std::vector<ModuleSlots>& getVoiceSlots();

  private:
    VoicePool pool;
    std::vector<ModuleSlots> voiceSlots;
    float fadeStep = 0.0f;

    void resetVoice(uint32_t slot);
    void addVoice(uint32_t slot, const AudioBuffer& voice, AudioBuffer& output, uint32_t frames);
};
