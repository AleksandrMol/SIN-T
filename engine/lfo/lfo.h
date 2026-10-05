#pragma once
#include "../modulation/modulation.h"

class LFO : public Modulation<> {
  public:
    LFO();

    void noteOn() override;
    void noteOff() override;

    void doSample() override;

    void play();
    void stop();

    bool getActive();

  private:
    bool isActive;
};
