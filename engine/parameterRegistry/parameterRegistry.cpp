#include "./parameterRegistry.h"
#include <optional>

ParameterRegistry::ParameterRegistry() {
  this->nextId = 0;
};

std::optional<uint32_t> ParameterRegistry::registerNewParam(Parameter* param) {
  if (param == nullptr || param->getId()) {
      return std::nullopt;
  }

  auto [it, inserted] = this->registry.try_emplace(this->nextId, param);

  if (!inserted) {
      return std::nullopt;
  }

  param->setId(this->nextId);
  this->nextId ++;

  return param->getId();
};

void ParameterRegistry::deleteParamById(uint32_t id) {
  auto it = this->registry.find(id);

  if (it == this->registry.end()) {
      return;
  }

  it->second->setId(std::nullopt);
  this->registry.erase(it);
};

bool ParameterRegistry::isParamValidById(uint32_t id) {
  return this->registry.contains(id);
};

Parameter* ParameterRegistry::getParamById(uint32_t id) {
  auto it = this->registry.find(id);

  if (it == this->registry.end()) {
      return nullptr;
  }

  return it->second;
};

