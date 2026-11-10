#include "./oscillator.h"

#include <cmath>
#include <numbers>

Oscillator::Oscillator() {
  this->sampleRate = 44100.0f;
  this->freq = 440.0f;
  this->volume = 0.1f;
  this->phase = 0.0f;
  this->phaseOffset = 0.0f;
  this->currentSample = 0.0f;

  this->setPhaseStep();
  this->setWave(WAVE_TYPE::SAW);
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
 * Поставить фазу, доля периода 0..1. Начало ноты (D-105)
 */
void Oscillator::setPhase(float phase) {
  this->phase = phase;
}

/**
 * Сдвиг фазы, доля периода 0..1. Действует сразу, в том числе на звучащую ноту (D-105)
 */
void Oscillator::setPhaseOffset(float offset) {
  this->phaseOffset = offset;
}

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
float Oscillator::calculateSaw(float phase) {
  float nativeSaw = 2.0f * phase - 1.0f;

  float output = nativeSaw - polyBlep(phase, this->phaseStep);

  return output;
}

/**
 * Метод вычисления волны квадрата
 */
float Oscillator::calculateSquare(float phase)
{
  float value = phase < 0.5f ? 1.0f : -1.0f;

  value += polyBlep(phase, this->phaseStep);

  float shiftedPhase = phase + 0.5f;

  if (shiftedPhase >= 1.0f) {
    shiftedPhase -= 1.0f;
  }

  value -= polyBlep(shiftedPhase, this->phaseStep);

  return value;
}

/**
 * Метод для высчитывания волн
 */
float Oscillator::calculateWave(float phase) {
  switch (this->waveType) {
    case WAVE_TYPE::SIN:
      return std::sin(2.0f * std::numbers::pi_v<float> * phase) * this->volume;
    case WAVE_TYPE::SQR:
      return this->calculateSquare(phase) * this->volume;
    case WAVE_TYPE::SAW:
      return this->calculateSaw(phase) * this->volume;
    case WAVE_TYPE::TNG:
      return (phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase)) * this->volume;
  }
  return 0.0f; 
}

/**
 * Высчитывает и меняет значение текущего семпла
 */
void Oscillator::doSample() {
  // Сдвиг не больше периода, поэтому одного вычитания хватает. При нулевом сдвиге фаза та же до бита.
  // Пока сдвиг меняется, настоящий шаг фазы отличается от phaseStep и polyBLEP сглаживает разрыв чуть неточно
  float shifted = this->phase + this->phaseOffset;

  if (shifted >= 1.0f) {
    shifted -= 1.0f;
  }

  this->currentSample = this->calculateWave(shifted);

  this->phase += this->phaseStep;

  // Удерживаем фазу в пределах от 0.0 до 1.0, чтобы избежать переполнения float
  if (this->phase >= 1.0f) {
    this->phase -= 1.0f;
  }
}
