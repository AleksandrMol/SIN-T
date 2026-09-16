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

    void noteOn();
    void noteOff();

    float doSample();

    void setSampleRate(float sampleRate);

    void setAttack(u_int32_t seconds);
    void setRelease(u_int32_t seconds);

  private:
    ENV_STAGE stage;

    u_int32_t attack;
    u_int32_t release;

    float sampleRate;

    float attackStep;
    float releaseStep;

    void setAttackStep();
    void setReleaseStep();

};