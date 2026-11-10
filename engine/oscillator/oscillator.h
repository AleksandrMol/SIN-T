#pragma once

enum class WAVE_TYPE {
  SIN = 1,
  SQR = 2,
  SAW = 3,
  TNG = 4,
};

/**
 * Форма волны без сглаживания разрывов: фаза 0..1, значение -1..1. В точке разрыва — значение после него.
 * Из неё же звучит осциллятор, по ней экран редактора рисует волну (D-094, D-108)
 */
float shapeWave(WAVE_TYPE type, float phase);

/**
 * Первая реализация осциллятора
 *
 * Осциллятор генерирует волну, и больше ничего не делает
 */
class Oscillator {
  public:
    Oscillator();
    float currentSample;
    void setWave(WAVE_TYPE);
    WAVE_TYPE getWave();
    void doSample();
    void stopWave();
    void setSampleRate(float sampleRate);
    void setVolume(float volume);
    void setFreq(float freq);
    void setPhase(float phase);
    void setPhaseOffset(float offset);

  private:
    WAVE_TYPE waveType;
    float sampleRate;
    float freq;
    float phase;
    float phaseOffset;
    float phaseStep;
    float volume;

    void setPhaseStep();
    float polyBlep(float phase, float phaseStep);
    float calculateWave(float phase);
};
