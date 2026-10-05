#include "./envelope.h"

Envelope::Envelope() {
  this->setNodes({
    {1,    0.0f, ENV_STAGE::IDLE},
    {5,    1.0f, ENV_STAGE::ATTACK},
    {4000, 1.0f, ENV_STAGE::DECAY},
    {1,    0.01f, ENV_STAGE::SUSTAIN},
    {1200,  0.0f, ENV_STAGE::RELEASE},
    {1,    0.0f, ENV_STAGE::IDLE},
  });

  this->setSampleRate(44100.0f);
  this->reset(0);
}

/**
 * Метод нажатия клавиши
 */
void Envelope::noteOn() {
  this->reset(0);
};

/**
 * Метод отпускания клавиши
 */
void Envelope::noteOff() {
  this->reset(3);
};

void Envelope::doRelease() {
  if (this->currentValue <= 0.0f) {
    this->currentValue = 0.0f;
    if(this->onEnd) {
      this->onEnd();
    }
  }
};

void Envelope::doSample() {
  auto* nextNode = this->getNextNode();

  if (!nextNode) {
    return;
  }

  switch (nextNode->stage) {
    case ENV_STAGE::ATTACK:
      this->process();
      break;
    case ENV_STAGE::DECAY:
      this->process();
      break;
    case ENV_STAGE::RELEASE:
      this->process();
      this->doRelease();
      break;
    case ENV_STAGE::SUSTAIN:
      break;
    case ENV_STAGE::IDLE:
      break;
  }
}
