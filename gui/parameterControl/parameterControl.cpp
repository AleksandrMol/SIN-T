#include "./parameterControl.h"

#include "../meshBuilder/meshBuilder.h"

void ParameterControl::change(float& field, float next) {
  if (field != next) {
    field = next;
    this->isDirty = true;
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

void ParameterControl::OnResize() {
  this->isDirty = true;
}

void ParameterControl::OnRender() {
  if (!this->GetRenderManager()) {
    return;
  }

  if (this->isDirty) {
    const float scale = this->GetContext() ? this->GetContext()->GetDensityIndependentPixelRatio() : 1.0f;
    MeshBuilder builder(this->GetComputedValues().opacity());

    this->build(builder, this->GetBox().GetSize(Rml::BoxArea::Border), scale);
    this->geometry = this->GetRenderManager()->MakeGeometry(builder.takeMesh());
    this->isDirty = false;
  }

  this->geometry.Render(this->GetAbsoluteOffset(Rml::BoxArea::Border));
}
