#include "./voice.h"
#include "../../tools/tools.h"

Voice::Voice() {
  this->isActive = false;
  this->isPlaying = false;
  key = 0;

  this->env.onEnd = [this]() {
    this->setIsPlaying(false);
  };
};

void Voice::setIsPlaying(bool isPlaying) {
  this->isPlaying = isPlaying;
}

bool Voice::getPlaying() {
  return this->isPlaying;
}

void Voice::setKey(int16_t key) {
  this->key = key;
  this->osc.setFreq(midiToFreq(key));
};

int16_t Voice::getKey() {
  return this->key;
};

void Voice::setActive(bool isActive) {
  this->isActive = isActive;

  if(isActive) {
    this->env.noteOn();
    this->setIsPlaying(true);
  } else {
    this->env.noteOff();
  }
};

bool Voice::getActive() {
  return this->isActive;
};

Envelope& Voice::getEnv() {
  return this->env;
}

Oscillator& Voice::getOsc() {
  return this->osc;
}

float Voice::getOutput() {
  return this->output;
}

void Voice::process() {
  this->output = 0.0;

  if (this->isPlaying) {
    this->env.doSample();
    this->osc.doSample();

    this->output = this->osc.currentSample * this->env.currentValue;
  }
};
