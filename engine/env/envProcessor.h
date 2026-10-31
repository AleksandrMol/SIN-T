#pragma once

#include "../moduleDescriptor/moduleDescriptor.h"
#include "../moduleProcessor/moduleProcessor.h"

extern const ModuleDescriptor ENV_DESCRIPTOR;

/**
 * Огибающая ADSR. У каждой стадии своя кривая, по умолчанию линейная (D-049). Запускается сбросом, а отпускание видит по gate
 */
class EnvProcessor : public ModuleProcessor {
  public:
    void prepare(float sampleRate) override;
    void reset() override;
    void process(const ProcessChunk& chunk) override;
    void modulate(const ModulationChunk& chunk) override;
    bool isFinished() const override;

  private:
    enum class STAGE {
      IDLE,
      ATTACK,
      DECAY,
      SUSTAIN,
      RELEASE,
    };

    float sampleRate = 44100.0f;
    STAGE stage = STAGE::IDLE;
    float level = 0.0f;
    float output = 0.0f; // Уровень после кривой стадии
    float releaseLevel = 0.0f; // Уровень в момент отпускания: Release — время спада от него до нуля

    bool moveTo(float target, float seconds, float distance, float& frames);
    float shapeLevel(const ModulationChunk& chunk, float sustain) const;
};
