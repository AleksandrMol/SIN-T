#include "./modulation.h"

Modulation::Modulation() {
  this->stage = MOD_STAGE::STOP;
  this->currentValue = 0.0f;
  this->countOfNodes = 3;
  this->nodes = {
    {0, 0},
    {2000, 1},
    {2000, 0},
  };

  this->nodeIndex = 0;
  this->nodeStep = 0;
  this->nodeTime = 0;

  this->setSampleRate(44100);
  this->setStep();
};

/**
 * Метод нажатия клавиши
*/
void Modulation::noteOn() {
  this->stage = MOD_STAGE::PROCESS;
};

/**
 * Метод отпускания клавиши
 */
void Modulation::noteOff() {
  this->stage = MOD_STAGE::STOP;
};

void Modulation::setCurrentNode() {
  this->currentNode = &this->nodes.at(this->nodeIndex);
  this->setStep();
  this->nodeTime = 0;
}

void Modulation::setNextNode() {
  this->nextNode = &this->nodes.at(this->nodeIndex + 1);
}

void Modulation::doSample() {
  if (this->stage == MOD_STAGE::PROCESS) {
    this->currentValue += this->nodeStep;
    this->nodeTime += 1/this->samplesPerMillisecond;

    if (this->nodeTime >= currentNode->time) {
      this->currentValue = this->nextNode->value;
      this->nodeIndex ++;
      this->setCurrentNode();
      this->setNextNode();
    }
  }
};

void Modulation::setSampleRate(float sampleRate) {
  this->sampleRate = sampleRate;
  this->samplesPerMillisecond = sampleRate / 1000;
};

/**
 * Установить шаг изменения громкости между текущим и следующим узлом
 */
void Modulation::setStep() {
  if (this->nodeIndex == this->countOfNodes) {
    this->nodeStep = 0;
    return;
  }

  this->nodeStep = -1 * (this->currentNode->value-this->nextNode->value) / (this->samplesPerMillisecond * nextNode->time);
};
