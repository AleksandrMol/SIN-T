#pragma once
#include <cstdint>

class Filter {
  public:
    Filter();
    void setSampleRate(float sampleRate);
    float calculateOut(float in);

    /**
     * Шаг обработки с готовым коэффициентом: так куску не нужно считать экспоненту на каждый сэмпл
     */
    float calculateOut(float in, float alpha);

    /**
     * Коэффициент для частоты среза
     */
    float getAlpha(float cutoff) const;

  private:
    float alpha;
    float previousOut;
    float currentOut;
    uint32_t cutoff;
    float sampleRate;

    void setCutoff(uint32_t cutoff);
    void calculateAlpha();
};

