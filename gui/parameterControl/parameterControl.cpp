#include "./parameterControl.h"

void ParameterControl::change(float& field, float next) {
  if (field != next) {
    field = next;
    this->markDirty();
  }
}

void ParameterControl::setValue(float value) {
  this->change(this->value, value);
}

void ParameterControl::setOrigin(float origin) {
  this->change(this->origin, origin);
}

void ParameterControl::setModulation(float low, float high) {
  this->change(this->modulationLow, low);
  this->change(this->modulationHigh, high);
}

float ParameterControl::getValue() const {
  return this->value;
}

float ParameterControl::getOrigin() const {
  return this->origin;
}

float ParameterControl::getModulationLow() const {
  return this->modulationLow;
}

float ParameterControl::getModulationHigh() const {
  return this->modulationHigh;
}
