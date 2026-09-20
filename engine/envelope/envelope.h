#include <functional>
#include <sys/types.h>

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

    void setAttack(u_int32_t seconds);
    void setRelease(u_int32_t seconds);

  private:
    ENV_STAGE stage;

    u_int32_t attack;
    u_int32_t release;

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