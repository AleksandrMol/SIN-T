#include "./genetaror.h"
#include <cstdint>

Generator::Generator() {
  this->setMaxVoice(16);
  this->setSampleRate(44100);
};

void Generator::setMaxVoice(uint16_t count) {
  maxVoice = count;
  voices.resize(count);
};

void Generator::noteOn(int16_t key) {
  for (uint16_t i = 0; i < maxVoice; i++) {
    if (!voices[i].getActive() && !voices[i].getEnv().isSound) {
      voices[i].setKey(key);
      voices[i].setActive(true);
      return;
    }
  }
}

void Generator::noteOff(int16_t key) {
  for (uint16_t i = 0; i < maxVoice; i++) {
    if(this->voices[i].getKey() == key) {
      this->voices[i].setActive(false);
    }
  }
};

void Generator::setSampleRate(float sampleRate) {
  this->sampleRate = sampleRate;

  for(int16_t i = 0; i < maxVoice; i++) {
    this->voices[i].getOsc().setSampleRate(sampleRate);
    this->voices[i].getEnv().setSampleRate(sampleRate);
  }
};

float Generator::getOutput() {
  return this->output;
}

void Generator::process() {
  output = 0.0f;

  for (uint16_t i = 0; i < maxVoice; i++) {
    if (this->voices[i].getEnv().isSound) {
      voices[i].process();
      output += voices[i].getOutput();
    }
  }
}
