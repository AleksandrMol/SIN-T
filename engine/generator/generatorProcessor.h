#pragma once

#include <map>
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
    GeneratorProcessor();

    void prepare(float sampleRate) override;

    /**
     * Сразу освобождает все голоса. Их состояния сбросит следующая нота
     */
    void reset() override;
    void process(const ProcessChunk& chunk) override;

    /**
     * События нот. Вызываются из аудиопотока между кусками. position — положение на сетке кусков:
     * модуляция новой ноты начинается с её сэмпла и сразу догоняет общую сетку (D-008)
     */
    void noteOn(const NoteAddress& note, float velocity, const PlanGenerator& plan, uint32_t position, const ModulationClock& clock);
    void noteOff(const NoteAddress& note);
    void choke(const NoteAddress& note);

    /**
     * Пересчитывает модуляцию всех голосов на следующий участок сетки. Вызывается из аудиопотока
     */
    void modulateVoices(
      const PlanGenerator& plan,
      const ModulationClock& clock,
      PARAMETER_UPDATE update = PARAMETER_UPDATE::CHANGED
    );

    /**
     * Готовит состояния голосов под лимит. Только добавляет слоты: на старых могут звучать голоса.
     * Вызывается на главном потоке
     */
    void reserveVoices(uint32_t polyphony);

    /**
     * Меняет лимит голосов, лишние затихают (D-042). Слоты должны быть готовы и уже быть в плане.
     * Вызывается из аудиопотока вместе со сменой плана
     */
    void setLimit(uint32_t polyphony);

    /**
     * Состояния подграфа по слотам голосов. Читает и меняет только главный поток:
     * аудиопоток получает готовые адреса через план
     */
    std::vector<ModuleSlots>& getVoiceSlots();

    /**
     * Velocity по слотам голосов: источник модуляции (D-027)
     */
    std::vector<float>& getVelocities();

    /**
     * Значения голосовых источников для чужих зон: их берёт самый поздний голос (D-004).
     * Ключ — ID модуля, а у velocity — ID самого генератора. Содержимое меняет аудиопоток
     * через адреса из плана, а саму таблицу только главный поток
     */
    std::map<ModuleId, std::vector<float>>& getLastOutputs();

  private:
    VoicePool pool;
    std::vector<ModuleSlots> voiceSlots;
    std::vector<float> velocities; // Размером с предел голосов и не меняются: на них указывают планы
    std::vector<uint32_t> segmentStarts; // С какого положения сетки начат текущий участок голоса
    std::vector<uint32_t> silentFrames; // Сколько сэмплов отпущенный голос уже тихий
    std::map<ModuleId, std::vector<float>> lastOutputs;
    float fadeStep = 0.0f;
    uint32_t silenceLimit = 0;

    VoiceContext makeContext(uint32_t slot, const ParameterRamp* parameters, float pitchBend) const;
    void modulateVoice(
      uint32_t slot,
      const PlanGenerator& plan,
      const ModulationClock& clock,
      uint32_t frames,
      PARAMETER_UPDATE update
    );
    void updateLastValues(const GraphPlan& subgraph);
    bool areHoldersFinished(uint32_t slot, const GraphPlan& subgraph) const;
    void countSilence(uint32_t slot, const AudioBuffer& voice, uint32_t frames);
    void addVoice(uint32_t slot, const AudioBuffer& voice, AudioBuffer& output, uint32_t frames);
};
