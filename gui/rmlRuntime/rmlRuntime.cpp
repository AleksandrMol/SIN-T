#include "./rmlRuntime.h"

#include <RmlUi/Core.h>

#include "../knob/knob.h"
#include "../wireLayer/wireLayer.h"

extern const unsigned char embeddedFont[];
extern const unsigned int embeddedFontSize;

namespace {
  struct RmlRuntime {
    int users = 0;
    Rml::ElementInstancerGeneric<Knob> knobInstancer;
    Rml::ElementInstancerGeneric<WireLayer> wireInstancer;
  };

  RmlRuntime& getRuntime() {
    static RmlRuntime runtime;
    return runtime;
  }
}

void acquireRmlRuntime() {
  RmlRuntime& runtime = getRuntime();

  if (runtime.users++ > 0) {
    return;
  }

  Rml::Initialise();
  Rml::Factory::RegisterElementInstancer("knob", &runtime.knobInstancer);
  Rml::Factory::RegisterElementInstancer("wire-layer", &runtime.wireInstancer);
  Rml::LoadFontFace(Rml::Span<const Rml::byte>(embeddedFont, embeddedFontSize), "Noto Sans", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
}

void releaseRmlRuntime() {
  RmlRuntime& runtime = getRuntime();

  if (--runtime.users == 0) {
    Rml::Shutdown();
  }
}
