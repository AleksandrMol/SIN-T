#include "./rmlRuntime.h"

#include <RmlUi/Core.h>

#include <memory>

#include "../gridDecorator/gridDecorator.h"
#include "../knob/knob.h"
#include "../plateDecorator/plateDecorator.h"
#include "../wireLayer/wireLayer.h"

extern const unsigned char embeddedFont[];
extern const unsigned int embeddedFontSize;

namespace {
  struct RmlRuntime {
    int users = 0;
    Rml::ElementInstancerGeneric<Knob> knobInstancer;
    Rml::ElementInstancerGeneric<WireLayer> wireInstancer;
    // Разборщики свойств появляются только в Rml::Initialise, поэтому декоратор создаётся после неё
    std::unique_ptr<PlateDecoratorInstancer> plateInstancer;
    GridDecoratorInstancer gridInstancer;
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
  runtime.plateInstancer = std::make_unique<PlateDecoratorInstancer>();
  Rml::Factory::RegisterDecoratorInstancer("plate", runtime.plateInstancer.get());
  Rml::Factory::RegisterDecoratorInstancer("grid", &runtime.gridInstancer);
  // Вариативный шрифт: без указания насыщенности RmlUi загружает все его начертания, стили выбирают их через font-weight
  Rml::LoadFontFace(Rml::Span<const Rml::byte>(embeddedFont, embeddedFontSize), "Jura", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto);
}

void releaseRmlRuntime() {
  RmlRuntime& runtime = getRuntime();

  if (--runtime.users == 0) {
    Rml::Shutdown();
    runtime.plateInstancer.reset();
  }
}
