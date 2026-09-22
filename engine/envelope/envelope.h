#include <cstdint>
#include <functional>
#include <sys/types.h>
#include "../modulation/modulation.h"

enum class ENV_STAGE {
  IDLE,
  ATTACK,
  SUSTAIN,
  RELEASE
};

class Envelope {
  public:
    Envelope();
    float currentValue;
    float sustainValue;

    void noteOn();
    void noteOff();
    std::function<void()> onEnd;

    void doSample();

    void setSampleRate(float sampleRate);

    void setAttack(uint32_t ml);
    void setRelease(uint32_t ml);

  private:
    ENV_STAGE stage;

    uint32_t attack;
    uint32_t release;

    float sampleRate;
    float samplesPerMillisecond;

    float attackStep;
    float releaseStep;

    void setAttackStep();
    void setReleaseStep();

    void doAttack();
    void doSustain();
    void doRelease();

};