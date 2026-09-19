#include "./oscillator.h"

#include <cmath>

Oscillator::Oscillator() {
  this->sampleRate = 44100.0f;
  this->freq = 440.0f;
  this->volume = 0.1f;
  this->phase = 0.0f;
  this->currentSample = 0.0f;

  this->setPhaseStep();
  this->setWave(WAVE_TYPE::SIN);
}

/**
 * Получить значение текущего типа волны
 */
WAVE_TYPE Oscillator::getWave() {
  return this->waveType;
};

/**
 * Мягкое прекращение генерирования волны, возвращение фазы в начальное состояние
 */
void Oscillator::stopWave() {
  this->phase = 0.0f;
  this->currentSample = 0.0f;
};

/**
 * Установить значение громкости
 */
void Oscillator::setVolume(float volume) {
  this->volume = volume;
};

/**
 * Установить частоту дискретизации. Меняет и значение шага фазы.
 */
void Oscillator::setSampleRate(float sample_rate) {
  this->sampleRate = sample_rate;
  this->setPhaseStep();
}

/**
 * Установить значение частоты. Меняет и значение шага фазы.
 */
void Oscillator::setFreq(float freq) {
  this->freq = freq;
  this->setPhaseStep();
};

/**
 * Установить тип волны
 */
void Oscillator::setWave(WAVE_TYPE waveType) {
  this->waveType = waveType;
}

/**
 * Установить шаг фазы
 */
void Oscillator::setPhaseStep() {
  this->phaseStep = this->freq / this->sampleRate;
}

/**
 * Метод коррекции волны для избавления от алиаса
 */
float Oscillator::polyBlep(float phase, float phaseStep) {
  if(phase < phaseStep) {
    phase /= phaseStep;

    return phase + phase - phase * phase - 1.0f;
  }

  if (phase > 1.0f - phaseStep) {
    phase = (phase - 1.0f) / phaseStep;

    return phase * phase + phase + phase + 1.0f;
  }

  return 0.0f;
};

/**
 * Метод вычисления волны пилы
 */
float Oscillator::calculateSaw() {
  float nativeSaw = 2.0f * this->phase - 1.0f;

  float output = nativeSaw - polyBlep(this->phase, this->phaseStep);

  return output;
}

/**
 * Метод вычисления волны квадрата
 */
float Oscillator::calculateSquare()
{
  float value = this->phase < 0.5f ? 1.0f : -1.0f;

  value += polyBlep(this->phase, this->phaseStep);

  float shiftedPhase = this->phase + 0.5f;

  if (shiftedPhase >= 1.0f) {
    shiftedPhase -= 1.0f;
  }

  value -= polyBlep(shiftedPhase, this->phaseStep);

  return value;
}

/**
 * Метод для высчитывания волн
 */
float Oscillator::calculateWave() {
  switch (this->waveType) {
    case WAVE_TYPE::SIN:
      return sinf(2.0f * M_PI * this->phase) * this->volume;
    case WAVE_TYPE::SQR:
      return this->calculateSquare() * this->volume;
    case WAVE_TYPE::SAW:
      return this->calculateSaw() * this->volume;
    case WAVE_TYPE::TNG:
      return (this->phase < 0.5f ? (4.0f * this->phase - 1.0f) : (3.0f - 4.0f * this->phase)) * this->volume;
  }
  return 0.0f; 
}

/**
 * Высчитывает и меняет значение текущего семпла
 */
void Oscillator::doSample() {
  this->currentSample = this->calculateWave();

  this->phase += this->phaseStep;

  // Удерживаем фазу в пределах от 0.0 до 1.0, чтобы избежать переполнения float
  if (this->phase >= 1.0f) {
    this->phase -= 1.0f;
  }
}
