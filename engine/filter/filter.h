#include <cstdint>

class Filter {
  public:
    Filter();
    void setSampleRate(float sampleRate);
    float calculateOut(float in);

  private:
    float alpha;
    float previousOut;
    float currentOut;
    uint32_t cutoff;
    float sampleRate;

    void setCutoff(uint32_t cutoff);
    void calculateAlpha();
};

