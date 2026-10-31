#pragma once

#include <memory>

#include "../executionPlan/executionPlan.h"
#include "../synthModel/synthModel.h"

/**
 * Движок: исполняет план главного графа. Блок хоста режется на куски по сетке MAX_CHUNK_FRAMES,
 * отсчитанной от начала работы, а не от начала блока: границы кусков не зависят от буфера DAW (D-008).
 *
 * Подмена плана во время звука появится на этапе 5 (D-023). Пока prepare и applyModel
 * можно вызывать только тогда, когда process не работает
 */
class Engine {
  public:
    void prepare(float sampleRate);

    /**
     * Приводит движок в соответствие с моделью. Состояния существующих модулей сохраняются
     */
    void applyModel(const SynthModel& model);

    /**
     * Записывает блок звука в каналы. Вызывается из аудиопотока
     */
    void process(float* left, float* right, uint32_t frames);

  private:
    float sampleRate = 44100.0f;
    ModuleSlots slots;
    std::unique_ptr<ExecutionPlan> plan;
    uint32_t chunkPosition = 0; // Положение внутри текущего куска сетки

    void processChunk(uint32_t frames);
};
