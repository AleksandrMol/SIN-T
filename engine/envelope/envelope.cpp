#include "./envelope.h"

Envelope::Envelope() {
  this->stage = ENV_STAGE::IDLE;
  this->currentValue = 0.0f;
  this->sustainValue = 1.0f;

  this->attack = 5;
  this->release = 650;

  this->isSound=false;

  this->setSampleRate(44100);
  this->setAttackStep();
  this->setReleaseStep();
};

/**
 * Метод нажатия клавиши
*/
void Envelope::noteOn() {
  this->isSound=true;
  this->stage = ENV_STAGE::ATTACK;
};

/**
 * Метод отпускания клавиши
 */
void Envelope::noteOff() {
  this->stage = ENV_STAGE::RELEASE;
};

void Envelope::onStopWave(void(*callback)()) {
  callback();
}

void Envelope::doAttack() {
  this->currentValue += this->attackStep;

  if (this->currentValue >= this->sustainValue) {
    this->currentValue = this->sustainValue;
    this->stage = ENV_STAGE::SUSTAIN;
  }
};

void Envelope::doSustain() {};

void Envelope::doRelease() {
  this->currentValue -= this->releaseStep;

  if (this->currentValue <= 0.0f) {
    this->currentValue = 0.0f;
    this->isSound = false;
    this->stage = ENV_STAGE::IDLE;
  }
};

void Envelope::doSample() {
  switch (this->stage) {
    case ENV_STAGE::IDLE:
      return;
    case ENV_STAGE::ATTACK:
      this->doAttack();
      return;
    case ENV_STAGE::SUSTAIN:
      this->doSustain();
      return;
    case ENV_STAGE::RELEASE:
      this->doRelease();
      return;
  }
};

void Envelope::setSampleRate(float sampleRate) {
  this->sampleRate = sampleRate;
  this->samplesPerMillisecond = sampleRate / 1000;
};

/**
 * Установить время атаки
 */
void Envelope::setAttack(u_int32_t ms) {
  this->attack = ms;
  this->setAttackStep();
};

/**
 * Установить время релиза
 */
void Envelope::setRelease(u_int32_t ms) {
  this->release = ms;
  this->setReleaseStep();
};

/**
 * Шаг изменения громкости во время атаки
 */
void Envelope::setAttackStep() {
  this->attackStep = this->sustainValue / (this->samplesPerMillisecond * this->attack);
};

/**
 * Шаг изменения громкости во время релиза
 */
void Envelope::setReleaseStep() {
  this->releaseStep = this->sustainValue / (this->samplesPerMillisecond * this->release);
};
