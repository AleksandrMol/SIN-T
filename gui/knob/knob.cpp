#include "./knob.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../meshBuilder/meshBuilder.h"

namespace {
  constexpr float SWEEP = 1.5f * std::numbers::pi_v<float>;
  constexpr float START_ANGLE = 0.75f * std::numbers::pi_v<float>;

  const Rml::Colourb TRACK_COLOR(64, 70, 86);
  const Rml::Colourb VALUE_COLOR(122, 162, 247);
  const Rml::Colourb POINTER_COLOR(230, 232, 236);
  const Rml::Colourb MODULATION_COLOR(255, 158, 100);

  float toAngle(float position) {
    return START_ANGLE + std::clamp(position, 0.0f, 1.0f) * SWEEP;
  }
}

void Knob::change(float& field, float next) {
  if (field != next) {
    field = next;
    this->isDirty = true;
  }
}

void Knob::setValue(float value) {
  this->change(this->value, value);
}

void Knob::setOrigin(float origin) {
  this->change(this->origin, origin);
}

void Knob::setModulation(float low, float high) {
  this->change(this->modulationLow, low);
  this->change(this->modulationHigh, high);
}

float Knob::getValue() const {
  return this->value;
}

void Knob::OnResize() {
  this->isDirty = true;
}

void Knob::rebuild() {
  const Rml::Vector2f size = this->GetBox().GetSize(Rml::BoxArea::Content);
  const float scale = this->GetContext() ? this->GetContext()->GetDensityIndependentPixelRatio() : 1.0f;
  const Rml::Vector2f center = size * 0.5f;
  const float radius = std::min(size.x, size.y) * 0.5f - 4.5f * scale;
  const float angle = toAngle(this->value);
  const float direction[2] = { std::cos(angle), std::sin(angle) };
  const Rml::Vector2f pointer[2] = {
    center + Rml::Vector2f(direction[0], direction[1]) * (radius * 0.35f),
    center + Rml::Vector2f(direction[0], direction[1]) * radius,
  };

  MeshBuilder builder(this->GetComputedValues().opacity());

  builder.addArc(center, radius, toAngle(0.0f), toAngle(1.0f), 3.0f * scale, TRACK_COLOR);
  builder.addArc(center, radius, toAngle(this->origin), angle, 3.0f * scale, VALUE_COLOR);
  builder.addLine(pointer, 2.0f * scale, POINTER_COLOR);

  if (this->modulationHigh > this->modulationLow) {
    builder.addArc(center, radius + 3.5f * scale, toAngle(this->modulationLow), toAngle(this->modulationHigh), 2.0f * scale, MODULATION_COLOR);
  }

  this->geometry = this->GetRenderManager()->MakeGeometry(builder.takeMesh());
  this->isDirty = false;
}

void Knob::OnRender() {
  if (!this->GetRenderManager()) {
    return;
  }

  if (this->isDirty) {
    this->rebuild();
  }

  this->geometry.Render(this->GetAbsoluteOffset(Rml::BoxArea::Content));
}
