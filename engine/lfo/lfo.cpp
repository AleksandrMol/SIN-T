#include "./lfo.h"

LFO::LFO() {
  this->setNodes({
    {
      1,
      0.0f
    },
    {
      1200,
      1.0f
    },
    {
      1200,
      0.0f
    },
  });

  this->setSampleRate(44100.0f);
  this->reset(0);
};

void LFO::noteOn() {
  this->reset(0);
  this->play();
};

void LFO::noteOff() {
  this->stop();
  this->reset(0);
};

void LFO::doSample() {
  if (this->isActive) {
    if (!this->getNextNode()) {
      this->reset(0);
    }

    this->process();
  }
};

void LFO::play() {
  this->isActive = true;
};

void LFO::stop() {
  this->isActive = false;
};

bool LFO::getActive() {
  return this->isActive;
};
