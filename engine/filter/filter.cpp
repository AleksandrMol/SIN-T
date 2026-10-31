#include "./filter.h"
#include <cmath>
#include <numbers>

Filter::Filter() {
  this->previousOut = 0.0f;
  this->currentOut = 0.0f;
  this->cutoff = 300;
  this->setSampleRate(44100.0f);
};

void Filter::setSampleRate(float sampleRate) {
  this->sampleRate = sampleRate;
  this->calculateAlpha();
};

void Filter::setCutoff(uint32_t cutoff) {
  this->cutoff = cutoff;
  this->calculateAlpha();
}

void Filter::calculateAlpha() {
  this->alpha = this->getAlpha((float)this->cutoff);
};

float Filter::getAlpha(float cutoff) const {
  return 1.0f - std::exp(
    -2.0f * std::numbers::pi_v<float> * cutoff / this->sampleRate
  );
};

float Filter::calculateOut(float in) {
  return this->calculateOut(in, this->alpha);
};

float Filter::calculateOut(float in, float alpha) {
  float output = this->previousOut + alpha * (in - this->previousOut);
  this->previousOut = output;
  return output;
};

