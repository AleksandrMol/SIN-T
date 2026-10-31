#pragma once

#include <array>
#include <cstdint>

// Максимальная длина куска обработки в сэмплах (D-008)
constexpr uint32_t MAX_CHUNK_FRAMES = 32;

/**
 * Аудиосигнал одного соединения: всегда стерео (D-026)
 */
struct AudioBuffer {
  std::array<float, MAX_CHUNK_FRAMES> left;
  std::array<float, MAX_CHUNK_FRAMES> right;
};

/**
 * Значения параметра в реальных единицах на концах куска. Внутри куска модуль
 * интерполирует их линейно (D-008, D-025)
 */
struct ParameterRamp {
  float start;
  float end;

  float at(uint32_t frame, uint32_t frames) const {
    return this->start + (this->end - this->start) * (float)frame / (float)frames;
  }
};

/**
 * Всё, что модуль получает на один кусок. Порты и параметры идут в порядке его описания.
 * Вход без соединений — буфер тишины
 */
struct ProcessChunk {
  uint32_t frames;
  uint32_t inputCount;
  uint32_t outputCount;
  const AudioBuffer* const* inputs;
  AudioBuffer* const* outputs;
  const ParameterRamp* parameters;
};

/**
 * DSP-часть модуля. Хранит только своё состояние (фазы, историю фильтра): оно отделено
 * от плана исполнения и переживает его смену (D-023)
 */
class ModuleProcessor {
  public:
    virtual ~ModuleProcessor() = default;

    /**
     * Сообщает частоту дискретизации. Вызывается не из аудиопотока
     */
    virtual void prepare(float sampleRate) = 0;

    /**
     * Обрабатывает кусок. Вызывается из аудиопотока: без блокировок и выделения памяти
     */
    virtual void process(const ProcessChunk& chunk) = 0;
};
