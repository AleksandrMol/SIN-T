#include "./colorTools.h"

#include <algorithm>
#include <cmath>

Rml::Colourb mixColor(Rml::Colourb from, Rml::Colourb to, float amount) {
  const float clamped = std::clamp(amount, 0.0f, 1.0f);
  const auto channel = [clamped](Rml::byte a, Rml::byte b) {
    return (Rml::byte)std::lround((float)a + ((float)b - (float)a) * clamped);
  };

  return Rml::Colourb(channel(from.red, to.red), channel(from.green, to.green), channel(from.blue, to.blue), channel(from.alpha, to.alpha));
}

Rml::Colourb withAlpha(Rml::Colourb color, Rml::byte alpha) {
  color.alpha = alpha;

  return color;
}
