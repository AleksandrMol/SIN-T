#include "./moduleCatalog.h"

#include "../amp/ampProcessor.h"
#include "../builtinModules/builtinModules.h"
#include "../filter/filterProcessor.h"
#include "../oscillator/oscillatorProcessor.h"

std::expected<void, std::string> addAllModules(ModuleRegistry& registry) {
  if (const auto result = addBuiltinModules(registry); !result) {
    return result;
  }

  for (const ModuleDescriptor* descriptor : { &OSCILLATOR_DESCRIPTOR, &FILTER_DESCRIPTOR, &AMP_DESCRIPTOR }) {
    if (const auto result = registry.add(descriptor); !result) {
      return result;
    }
  }

  return {};
}
