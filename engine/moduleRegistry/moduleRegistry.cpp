#include "./moduleRegistry.h"

#include <algorithm>

std::expected<void, std::string> ModuleRegistry::add(const ModuleDescriptor* descriptor) {
  if (const auto result = validateModule(*descriptor); !result) {
    return result;
  }

  if (this->find(descriptor->type)) {
    return std::unexpected("тип \"" + descriptor->type + "\" уже добавлен");
  }

  this->descriptors.push_back(descriptor);

  return {};
}

const ModuleDescriptor* ModuleRegistry::find(std::string_view type) const {
  const auto it = std::find_if(this->descriptors.begin(), this->descriptors.end(), [type](const ModuleDescriptor* descriptor) {
    return descriptor->type == type;
  });

  return it == this->descriptors.end() ? nullptr : *it;
}

const std::vector<const ModuleDescriptor*>& ModuleRegistry::getAll() const {
  return this->descriptors;
}
