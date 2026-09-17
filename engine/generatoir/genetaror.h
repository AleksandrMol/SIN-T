#include "../voice/voice.h"
#include <cstdint>
#include <vector>

class Generator {
  public:
    Generator();
    void setMaxVoice(u_int16_t);
    void noteOn(int16_t key);
    void noteOff(int16_t key);
    void setSampleRate(float sampleRate);
    float getOutput();
    void process();

  private:
    float sampleRate;
    float output;
    u_int16_t maxVoice;
    std::vector<Voice> voices;
};
