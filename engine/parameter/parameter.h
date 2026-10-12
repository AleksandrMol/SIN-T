#pragma once

class Parameter {
  public:
    Parameter(float base, float min, float max);

    void setBaseValue(float value);

    void resetModulation();
    void addAdditional(float amount, float value);

    void calculate();

    float getResult();

  private:
    float clampValue(float value);

    float baseValue;
    float additionalValue;
    float resultValue;

    float minValue;
    float maxValue;
};
