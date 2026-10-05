#pragma once
#include "../lfo/lfo.h"
#include "../oscillator/oscillator.h"
#include "../envelope/envelope.h"
#include <cstdint>

class Voice {
  public:
    Voice();
    void setKey(int16_t key);
    int16_t getKey();
    void setActive(bool isActive);
    bool getActive();
    void setSampleRate(float sampleRate);
    bool getPlaying();
    Envelope& getEnv();
    Oscillator& getOsc();
    void process();
    float getOutput();

  private:
    float output;

    Oscillator osc;
    Envelope env;
    LFO lfo;

    bool isActive;
    bool isPlaying;

    int16_t key;

    void setIsPlaying(bool isPlaying);
};
