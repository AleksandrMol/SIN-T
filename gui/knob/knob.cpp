#include "./knob.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "../meshBuilder/meshBuilder.h"

namespace {
  constexpr float PI = std::numbers::pi_v<float>;
  constexpr float SWEEP = 1.5f * PI;
  constexpr float START_ANGLE = 0.75f * PI;
  // Свет падает сверху слева, как на панелях редактора
  constexpr float LIGHT_ANGLE = -0.75f * PI;
  constexpr int KNURL_COUNT = 18;
  constexpr int RING_STEPS = 96;

  const Rml::Colourb TRACK_COLOR(11, 13, 16);
  const Rml::Colourb TRACK_EDGE_COLOR(255, 255, 255, 20);
  const Rml::Colourb VALUE_COLOR(61, 141, 232);
  const Rml::Colourb MODULATION_COLOR(240, 146, 58);
  const Rml::Colourb SHADOW_COLOR(0, 0, 0, 160);
  const Rml::Colourb SKIRT_INNER_COLOR(14, 15, 18);
  const Rml::Colourb SKIRT_DARK_COLOR(10, 11, 13);
  const Rml::Colourb SKIRT_LIGHT_COLOR(92, 101, 114);
  const Rml::Colourb KNURL_COLOR(0, 0, 0, 120);
  const Rml::Colourb CAP_DARK_COLOR(30, 33, 38);
  const Rml::Colourb CAP_LIGHT_COLOR(156, 167, 180);
  const Rml::Colourb BEVEL_DARK_COLOR(16, 18, 21);
  const Rml::Colourb BEVEL_LIGHT_COLOR(196, 205, 216);
  const Rml::Colourb POINTER_COLOR(238, 242, 246);
  const Rml::Colourb POINTER_SHADOW_COLOR(0, 0, 0, 150);

  float toAngle(float position) {
    return START_ANGLE + std::clamp(position, 0.0f, 1.0f) * SWEEP;
  }

  Rml::Colourb mix(Rml::Colourb from, Rml::Colourb to, float amount) {
    const auto channel = [amount](Rml::byte a, Rml::byte b) {
      return (Rml::byte)std::lround((float)a + ((float)b - (float)a) * std::clamp(amount, 0.0f, 1.0f));
    };

    return Rml::Colourb(channel(from.red, to.red), channel(from.green, to.green), channel(from.blue, to.blue), channel(from.alpha, to.alpha));
  }

  Rml::Colourb transparent(Rml::Colourb color) {
    color.alpha = 0;

    return color;
  }

  /**
   * Насколько грань под этим углом смотрит на свет: 1 к свету, 0 от него
   */
  float facing(float angle) {
    return 0.5f + 0.5f * std::cos(angle - LIGHT_ANGLE);
  }

  /**
   * Блеск шлифованного торца. Риски шлифовки идут от центра, поэтому свет отражается
   * двумя секторами: со стороны света и напротив, как conic-gradient. Мелкая рябь — неровность шлифовки
   */
  float brushed(float angle) {
    const float delta = angle - LIGHT_ANGLE;
    const float lobes = 0.5f + 0.5f * std::cos(2.0f * delta);
    const float lit = lobes * (0.65f + 0.35f * std::cos(delta));

    return std::clamp(lit + 0.05f * std::sin(13.0f * angle), 0.0f, 1.0f);
  }

  using RingColors = std::array<Rml::Colourb, RING_STEPS + 1>;

  /**
   * Блики от значения не зависят, поэтому цвета колец считаются один раз на все ручки
   */
  struct Palette {
    RingColors skirt;
    RingColors cap;
    RingColors bevel;
    RingColors bevelInner;

    Palette() {
      for (int step = 0; step <= RING_STEPS; step++) {
        const float angle = MeshBuilder::getRingAngle(step, RING_STEPS);
        const float light = facing(angle);

        this->skirt[step] = mix(SKIRT_DARK_COLOR, SKIRT_LIGHT_COLOR, light * light);
        this->cap[step] = mix(CAP_DARK_COLOR, CAP_LIGHT_COLOR, brushed(angle));
        this->bevel[step] = mix(BEVEL_DARK_COLOR, BEVEL_LIGHT_COLOR, light);
        this->bevelInner[step] = transparent(this->bevel[step]);
      }
    }
  };

  const Palette& getPalette() {
    static const Palette palette;

    return palette;
  }

  std::span<const Rml::Colourb> single(const Rml::Colourb& color) {
    return { &color, 1 };
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

float Knob::getOrigin() const {
  return this->origin;
}

float Knob::getModulationLow() const {
  return this->modulationLow;
}

float Knob::getModulationHigh() const {
  return this->modulationHigh;
}

void Knob::OnResize() {
  this->isDirty = true;
}

/**
 * Слои снизу вверх: тень, дорожка с дугами значения и модуляции, юбка с рифлением,
 * шлифованный торец с фаской, риска. Блики стоят на месте, вращаются только риска и рифление:
 * у настоящей ручки свет в комнате тоже не поворачивается вместе с ней
 */
void Knob::rebuild() {
  const Rml::Vector2f size = this->GetBox().GetSize(Rml::BoxArea::Content);
  const float scale = this->GetContext() ? this->GetContext()->GetDensityIndependentPixelRatio() : 1.0f;
  const Rml::Vector2f center = size * 0.5f;
  const float radius = std::min(size.x, size.y) * 0.5f - 4.5f * scale;
  const float skirtRadius = std::max(radius - 3.0f * scale, 1.0f);
  const float capRadius = std::max(skirtRadius - 2.5f * scale, 1.0f);
  const float angle = toAngle(this->value);
  const Rml::Vector2f direction(std::cos(angle), std::sin(angle));

  const Palette& palette = getPalette();

  MeshBuilder builder(this->GetComputedValues().opacity());

  builder.addRing(
    { .center = center + Rml::Vector2f(0.0f, 1.5f * scale), .outerRadius = skirtRadius, .feather = 3.0f * scale, .steps = RING_STEPS / 2 },
    single(SHADOW_COLOR),
    single(SHADOW_COLOR)
  );

  // Дорожка — прорезь в металле: светлая кромка снизу видна и там, где прорезь сливается с тенью ручки
  builder.addArc(center + Rml::Vector2f(0.0f, 0.8f * scale), radius, toAngle(0.0f), toAngle(1.0f), 2.5f * scale, TRACK_EDGE_COLOR);
  builder.addArc(center, radius, toAngle(0.0f), toAngle(1.0f), 2.5f * scale, TRACK_COLOR);
  builder.addArc(center, radius, toAngle(this->origin), angle, 2.5f * scale, VALUE_COLOR);

  if (this->modulationHigh > this->modulationLow) {
    builder.addArc(center, radius + 3.5f * scale, toAngle(this->modulationLow), toAngle(this->modulationHigh), 2.0f * scale, MODULATION_COLOR);
  }

  builder.addRing(
    { .center = center, .innerRadius = capRadius - scale, .outerRadius = skirtRadius, .steps = RING_STEPS },
    single(SKIRT_INNER_COLOR),
    palette.skirt
  );

  for (int index = 0; index < KNURL_COUNT; index++) {
    const float knurlAngle = angle + 2.0f * PI * (float)index / (float)KNURL_COUNT;
    const Rml::Vector2f knurlDirection(std::cos(knurlAngle), std::sin(knurlAngle));
    const Rml::Vector2f knurl[2] = {
      center + knurlDirection * (capRadius + 0.6f * scale),
      center + knurlDirection * (skirtRadius - 0.4f * scale),
    };

    builder.addLine(knurl, scale, KNURL_COLOR);
  }

  builder.addRing({ .center = center, .outerRadius = capRadius, .steps = RING_STEPS }, palette.cap, palette.cap);
  builder.addRing(
    { .center = center, .innerRadius = capRadius - 1.2f * scale, .outerRadius = capRadius, .steps = RING_STEPS },
    palette.bevelInner,
    palette.bevel
  );

  const Rml::Vector2f pointer[2] = {
    center + direction * (capRadius * 0.2f),
    center + direction * (skirtRadius - 0.5f * scale),
  };
  const Rml::Vector2f pointerShadow[2] = {
    pointer[0] + Rml::Vector2f(0.0f, scale),
    pointer[1] + Rml::Vector2f(0.0f, scale),
  };

  builder.addLine(pointerShadow, 2.5f * scale, POINTER_SHADOW_COLOR);
  builder.addLine(pointer, 1.8f * scale, POINTER_COLOR);

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
