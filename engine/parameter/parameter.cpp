#include "./parameter.h"
#include <algorithm>
#include <cstdint>

Parameter::Parameter(float base, float min, float max) {
  this->id = std::nullopt;
  this->baseValue = 0.0f;
  this->additionalValue = 0.0f;
  this->maxValue = max;
  this->minValue = min;
  this->setBaseValue(base);
  this->resultValue = this->baseValue;
}

void Parameter::setId(std::optional<uint32_t> id) {
  this->id = id;
};

std::optional<uint32_t> Parameter::getId() {
  return this->id;
};

float Parameter::clampValue(float value) {
  return std::clamp(value, minValue, maxValue);
};

void Parameter::setBaseValue(float value) {
  this->baseValue = this->clampValue(value);
};

void Parameter::resetModulation() {
  this->additionalValue = 0.0f;
};

void Parameter::addAdditional(float amount, float modulationValue) {
  this->additionalValue = this->additionalValue + amount * modulationValue;
};

void Parameter::calculate() {
  this->resultValue = this->clampValue(this->baseValue + this->additionalValue);
};

float Parameter::getResult() {
  return this->resultValue;
};

