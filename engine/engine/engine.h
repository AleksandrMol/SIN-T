#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "../executionPlan/executionPlan.h"
#include "../planExchange/planExchange.h"
#include "../synthModel/synthModel.h"
#include "../voicePool/voicePool.h"

// Темп, если хост его не сообщает (D-050)
constexpr float DEFAULT_TEMPO = 120.0f;

/**
 * Движок: исполняет план главного графа. Блок хоста режется на куски по сетке MAX_CHUNK_FRAMES,
 * отсчитанной от начала работы, а не от начала блока: границы кусков не зависят от буфера DAW (D-008).
 * События нот применяются между вызовами process, поэтому они тоже разрезают кусок.
 * Модуляция считается раз на шаг сетки, сразу на весь шаг и перед звуком (D-008, D-009).
 *
 * Потоки. Модель и сборка планов живут на главном потоке, он же освобождает всю память.
 * Аудиопоток читает только текущий план и забирает новый на границе сетки (D-023, D-053).
 * Пока включён живой режим (setLive), process может идти параллельно с applyModel и replaceModel
 */
class Engine {
  public:
    /**
     * Сообщает частоту дискретизации. Вызывается на главном потоке вне живого режима
     */
    void prepare(float sampleRate);

    /**
     * Приводит движок в соответствие с моделью. Состояния существующих модулей сохраняются.
     * В живом режиме изменения идут плавно и через план, а параметры сразу и без плана. Пока идёт
     * один переход, новые правки ждут его конца и применяются вместе (D-053).
     * Вне живого режима всё применяется сразу. Вызывается на главном потоке
     */
    void applyModel(const SynthModel& model);

    /**
     * Заменяет всё состояние: так меняется пресет. Старый план затухает, новый нарастает,
     * голоса старого плана заканчиваются (D-054). Вызывается на главном потоке
     */
    void replaceModel(const SynthModel& model);

    /**
     * Включает и выключает живой режим. Включается до первого process, выключается после последнего:
     * незавершённый переход заканчивается сразу. Вызывается на главном потоке
     */
    void setLive(bool isLive);

    /**
     * Освобождает память и продолжает переходы. Вызывается на главном потоке, когда
     * takeUpdateRequest вернул true, и может вызываться в любое другое время
     */
    void update();

    /**
     * Нужен ли главному потоку вызов update. Вызывается из аудиопотока
     */
    bool takeUpdateRequest();

    /**
     * Сбрасывает состояния всех модулей и освобождает все голоса. Параметры не меняются.
     * Хост может вызвать его из аудиопотока, поэтому работает только с текущим планом
     */
    void reset();

    /**
     * Записывает блок звука в каналы. Вызывается из аудиопотока
     */
    void process(float* left, float* right, uint32_t frames);

    /**
     * События нот. Каждый генератор обрабатывает их сам (ТЗ §5.1). Вызываются из аудиопотока
     */
    void noteOn(const NoteAddress& note, float velocity);
    void noteOff(const NoteAddress& note);
    void choke(const NoteAddress& note);

    /**
     * Положение колеса -1..1. Глобальное значение, диапазон задаёт каждый генератор (D-027)
     */
    void setPitchBend(float value);

    /**
     * Темп в BPM. Вызывается из аудиопотока. Неверное значение заменяется темпом по умолчанию
     */
    void setTempo(float tempo);

    /**
     * Меняет базовое значение параметра, изменение сглаживается ~10 мс (D-025). Индекс — в порядке
     * описания модуля, значение в реальных единицах. Вызывается из аудиопотока
     */
    void setParameter(ModuleId module, uint32_t parameter, float value);

  private:
    // Главный поток
    float sampleRate = 44100.0f;
    ModuleSlots slots;
    ParameterBases bases;
    PlanExchange exchange;
    bool isLive = false;
    std::optional<SynthModel> applied; // Что исполняется сейчас. Держит описания заглушек, на которые ссылаются планы
    std::optional<SynthModel> desired; // Что должно исполняться
    std::optional<SynthModel> settling; // Во что превратится applied в конце перехода. Пусто — перехода нет
    uint64_t transitionGeneration = 0;

    // Общие для потоков
    std::atomic<bool> hasParameterRequests = false;
    std::atomic<uint64_t> settledGeneration = 0; // Поколение плана, переход которого окончен
    std::atomic<bool> isUpdateRequested = false;

    // Аудиопоток. Вне живого режима им пользуется главный
    float pitchBend = 0.0f;
    float tempo = DEFAULT_TEMPO;
    bool isSmoothing = false;
    bool isPlanNew = false; // Новый план мог начать исполнять то, что давно не считалось
    NoteAddress lastNote = { -1, -1, -1, -1 }; // Последняя нота: ею живут глобальные экземпляры общих модуляторов (D-005)
    bool lastNoteGate = false;
    ExecutionPlan* plan = nullptr;
    ExecutionPlan* outgoing = nullptr; // План, который затухает после замены состояния
    uint32_t chunkPosition = 0; // Положение внутри текущего куска сетки

    uint32_t countFadeChunks() const;
    void applyDirectly(const SynthModel& model);
    void sendParameters(const SynthModel& model);
    void setApplied(SynthModel model);
    void startTransition();
    void publishPlan(const SynthModel& scheme, const TransitionFades& fades);

    void takePlan();
    void switchPlan(ExecutionPlan* next);
    void startReplacement(ExecutionPlan* next);
    void finishReplacement();
    void readParameterRequests();
    void smoothBase(BaseValue& base, float target);
    VoiceContext makeContext() const;
    void startMainModulation();
    void advanceBases();
    void advanceFades(ExecutionPlan& target);
    void advanceCurrentFades();
    void modulate(ExecutionPlan& target, PARAMETER_UPDATE update);
    void beginChunk(bool hasRequests);
    void releaseLastNote(const NoteAddress& note);
    void writeOutput(float* left, float* right, uint32_t frames) const;
};
