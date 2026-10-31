#include "./moduleDescriptor.h"

#include <set>
#include <string_view>

namespace {
  /**
   * Ключи внутри одного списка должны быть непустыми и неповторяющимися
   */
  template <typename T>
  std::expected<void, std::string> validateKeys(const std::vector<T>& items, std::string_view listName) {
    std::set<std::string_view> keys;

    for (const T& item : items) {
      if (item.key.empty()) {
        return std::unexpected("пустой ключ в списке " + std::string(listName));
      }

      if (!keys.insert(item.key).second) {
        return std::unexpected("ключ \"" + item.key + "\" повторяется в списке " + std::string(listName));
      }
    }

    return {};
  }
}

std::expected<void, std::string> validateModule(const ModuleDescriptor& module) {
  if (module.type.empty()) {
    return std::unexpected("у модуля пустой тип");
  }

  const auto withLabel = [&module](const std::string& error) {
    return std::unexpected("модуль \"" + module.type + "\": " + error);
  };

  if (module.name.empty()) {
    return withLabel("пустое имя");
  }

  if (module.typeVersion == 0) {
    return withLabel("версия типа начинается с 1");
  }

  for (const auto& result : {
    validateKeys(module.inputs, "inputs"),
    validateKeys(module.outputs, "outputs"),
    validateKeys(module.modOutputs, "modOutputs"),
    validateKeys(module.parameters, "parameters"),
  }) {
    if (!result) {
      return withLabel(result.error());
    }
  }

  for (const ParameterDescriptor& parameter : module.parameters) {
    if (const auto result = validateParameter(parameter); !result) {
      return withLabel(result.error());
    }
  }

  return {};
}
