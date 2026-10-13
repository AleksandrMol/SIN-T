#pragma once

#include <cstdint>
#include <optional>
class Parameter {
  public:
    Parameter(float base, float min, float max);
 
    void setId(std::optional<uint32_t> id);
    std::optional<uint32_t> getId();

    void setBaseValue(float value);

    void resetModulation();
    void addAdditional(float amount, float value);

    void calculate();

    float getResult();

  private:
    float clampValue(float value);

    std::optional<uint32_t> id;

    float baseValue;
    float additionalValue;
    float resultValue;

    float minValue;
    float maxValue;
};

