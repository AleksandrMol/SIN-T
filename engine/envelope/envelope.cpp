#include "./envelope.h"
#include <sys/types.h>

Envelope::Envelope() {
  this->stage = ENV_STAGE::IDLE;
  this->currentValue = 0.0;

  this->sampleRate = 44100;
  this->attack = 5;
  this->release = 5;

  this->setAttackStep();
  this->setReleaseStep();
};

void Envelope::noteOn() {

};
void Envelope::noteOff() {

};

float Envelope::doSample() {

};

void Envelope::setSampleRate(float sampleRate) {
  this->sampleRate = sampleRate;
};

void Envelope::setAttack(u_int32_t ms) {

};

void Envelope::setAttackStep() {
  this->attackStep = 44.1f ;
};
void Envelope::setReleaseStep() {

};