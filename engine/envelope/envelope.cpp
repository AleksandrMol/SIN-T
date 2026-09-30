#include "./envelope.h"

Envelope::Envelope() {
  this->currentValue = 0.0f;

  this->setNodes({
    {1, 0.0f, ENV_STAGE::IDLE},
    {5, 1.0f, ENV_STAGE::ATTACK},
    {30, 0.5f, ENV_STAGE::DECAY},
    {1, 0.5f, ENV_STAGE::SUSTAIN},
    {500, 0.0f, ENV_STAGE::RELEASE},
    {1, 0.0f, ENV_STAGE::IDLE},
  });

  this->setSampleRate(44100);
  this->setCurrentNode();
  this->setNextNode();
  this->setStep();
};

/**
 * Метод нажатия клавиши
*/
void Envelope::noteOn() {
  this->nodeIndex = 0;
  this->setCurrentNode();
  this->setNextNode();
  this->setStep();
};

/**
 * Метод отпускания клавиши
 */
void Envelope::noteOff() {
  console.log("OFF");
  this->nodeIndex = 3;
  this->setCurrentNode();
  this->setNextNode();
  this->setStep();
};

// void Envelope::doAttack() {
//   if (this->currentValue >= this->sustainValue) {
//     this->currentValue = this->sustainValue;
//     this->stage = ENV_STAGE::SUSTAIN;
//   }
// };

void Envelope::doRelease() {
  if (this->currentValue <= 0.0f) {
    this->currentValue = 0.0f;
    if(this->onEnd) {
      console.log("End");
      this->onEnd();
    }
  }
};

void Envelope::doSample() {
  switch (this->getNextNode()->stage) {
    case ENV_STAGE::IDLE:
      return;
    case ENV_STAGE::ATTACK:
      this->process();
      return;
    case ENV_STAGE::DECAY:
      this->process();
      return;
    case ENV_STAGE::SUSTAIN:
      return;
    case ENV_STAGE::RELEASE:
      this->process();
      this->doRelease();
      return;
  }
};

