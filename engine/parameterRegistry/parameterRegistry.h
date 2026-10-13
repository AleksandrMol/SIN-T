#pragma once
#include <cstdint>
#include <unordered_map>
#include "../parameter/parameter.h"

class ParameterRegistry {
  public:
    ParameterRegistry();
    Parameter* getParamById(uint32_t id);
    std::optional<uint32_t> registerNewParam(Parameter* param);
    void deleteParamById(uint32_t id);
    bool isParamValidById(uint32_t id);

  private:
    uint32_t nextId;
    std::unordered_map<uint32_t, Parameter*> registry;
};

